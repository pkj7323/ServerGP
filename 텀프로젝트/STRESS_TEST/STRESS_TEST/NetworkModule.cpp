#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <WinSock2.h>
#include <winsock.h>
#include <Windows.h>
#include <iostream>
#include <thread>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <chrono>
#include <queue>
#include <array>
#include <memory>
#include <unordered_map>

using namespace std;
using namespace chrono;

extern HWND		hWnd;

const static int MAX_TEST = 30000;
const static int MAX_CLIENTS = MAX_TEST * 2;
const static int INVALID_ID = -1;
const static int MAX_PACKET_SIZE = 255;
const static int MAX_BUFF_SIZE = 255;

#pragma comment (lib, "ws2_32.lib")

#include "../../Server/Server/Protocol.h"

HANDLE g_hiocp;
char g_server_ip[16] = "127.0.0.1";

enum OPTYPE { OP_SEND, OP_RECV, OP_DO_MOVE };

high_resolution_clock::time_point last_connect_time;

struct OverlappedEx {
	WSAOVERLAPPED over;
	WSABUF wsabuf;
	unsigned char IOCP_buf[MAX_BUFF_SIZE];
	OPTYPE event_type;
	int event_target;
};

struct CLIENT {
	int id;
	int x;
	int y;
	atomic_bool connected;

	SOCKET client_socket;
	OverlappedEx recv_over;
	unsigned char packet_buf[MAX_PACKET_SIZE];
	int prev_packet_data;
	int curr_packet_size;
	high_resolution_clock::time_point last_move_time;
};

unordered_map<int, int> client_map; // server_player_id -> local client index
mutex client_map_mutex;
array<CLIENT, MAX_CLIENTS> g_clients;
atomic_int num_connections;
atomic_int client_to_close;
atomic_int active_clients;

int			global_delay;				

vector <thread*> worker_threads;
thread test_thread;

float point_cloud[MAX_TEST * 2];

struct ALIEN {
	int id;
	int x, y;
	int visible_count;
};

void error_display(const char* msg, int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	// std::cout << msg;
	// std::wcout << L"에러" << lpMsgBuf << std::endl;

	// MessageBox(hWnd, lpMsgBuf, L"ERROR", 0);
	LocalFree(lpMsgBuf);
	// while (true);
}

void DisconnectClient(int ci)
{
	bool status = true;
	if (true == atomic_compare_exchange_strong(&g_clients[ci].connected, &status, false)) {
		closesocket(g_clients[ci].client_socket);
		active_clients--;
	}
	// cout << "Client [" << ci << "] Disconnected!\n";
}

void SendPacket(int cl, void* packet)
{
	int psize = reinterpret_cast<unsigned char*>(packet)[0];
	int ptype = reinterpret_cast<unsigned char*>(packet)[1];
	OverlappedEx* over = new OverlappedEx;
	over->event_type = OP_SEND;
	memcpy(over->IOCP_buf, packet, psize);
	ZeroMemory(&over->over, sizeof(over->over));
	over->wsabuf.buf = reinterpret_cast<CHAR*>(over->IOCP_buf);
	over->wsabuf.len = psize;
	int ret = WSASend(g_clients[cl].client_socket, &over->wsabuf, 1, NULL, 0,
		&over->over, NULL);
	if (0 != ret) {
		int err_no = WSAGetLastError();
		if (WSA_IO_PENDING != err_no)
			error_display("Error in SendPacket:", err_no);
	}
	// std::cout << "Send Packet [" << ptype << "] To Client : " << cl << std::endl;
}

void ProcessPacket(int ci, unsigned char packet[])
{
	PACKET_TYPE ptype = static_cast<PACKET_TYPE>(packet[1]);
	switch (ptype) {
	case S2C_LOGIN_RESULT:
	{
		S2C_LoginResult* p = reinterpret_cast<S2C_LoginResult*>(packet);
		if (p->success) {
			// 이미 연결 시 C2S_LOGIN을 보냈으므로 여기서 다시 보내면 무한 루프 발생. 생략함.
		}
		else {
			DisconnectClient(ci);
		}
		break;
	}
	case S2C_AVATAR_INFO:
	{
		// 스폰 성공 → 연결 완료로 처리
		g_clients[ci].connected = true;
		++active_clients;
		S2C_AvatarInfo* info = reinterpret_cast<S2C_AvatarInfo*>(packet);
		{
			lock_guard<mutex> lock(client_map_mutex);
			client_map[info->playerId] = ci; // server player id -> local index
		}
		g_clients[ci].id = info->playerId;
		g_clients[ci].x  = info->x;
		g_clients[ci].y  = info->y;
		break;
	}
	case S2C_MOVE_OBJECT:
	{
		S2C_MoveObject* move_packet = reinterpret_cast<S2C_MoveObject*>(packet);
		
		int my_id = -1;
		{
			lock_guard<mutex> lock(client_map_mutex);
			if (client_map.count(move_packet->object_id) > 0) {
				my_id = client_map[move_packet->object_id];
			}
		}
		
		// client_map에 없는 object_id는 다른 플레이어 이동이므로 무시
		if (my_id == -1) break;
		if (my_id < 0 || my_id >= MAX_CLIENTS) break;

		// 내 캐릭터 이동이면 RTT 측정
		if (my_id == ci && move_packet->move_time != 0) {
			long long now_ms = duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch()).count();
			long long d_ms = now_ms - (long long)move_packet->move_time;
			if (d_ms > 0 && d_ms < 10000) { // 비정상 값 필터링
				if (global_delay < d_ms) global_delay++;
				else if (global_delay > d_ms) global_delay--;
			}
		}
		g_clients[my_id].x = move_packet->x;
		g_clients[my_id].y = move_packet->y;
		break;
	}
	// 나머지 패킷(ADD_OBJECT, REMOVE_OBJECT, STATUS_CHANGE 등)은 무시
	default: break;
	}
}

void Worker_Thread()
{
	while (true) {
		DWORD io_size;
		unsigned long long ci;
		OverlappedEx* over;
		BOOL ret = GetQueuedCompletionStatus(g_hiocp, &io_size, &ci,
			reinterpret_cast<LPWSAOVERLAPPED*>(&over), INFINITE);
		// std::cout << "GQCS :";
		int client_id = static_cast<int>(ci);
		if (FALSE == ret) {
			int err_no = WSAGetLastError();
			if (64 == err_no) DisconnectClient(client_id);
			else {
				// error_display("GQCS : ", WSAGetLastError());
				DisconnectClient(client_id);
			}
			if (OP_SEND == over->event_type) delete over;
		}
		if (0 == io_size) {
			DisconnectClient(client_id);
			continue;
		}
		if (OP_RECV == over->event_type) {
			//std::cout << "RECV from Client :" << ci;
			//std::cout << "  IO_SIZE : " << io_size << std::endl;
			unsigned char* buf = g_clients[ci].recv_over.IOCP_buf;
			unsigned psize = g_clients[ci].curr_packet_size;
			unsigned pr_size = g_clients[ci].prev_packet_data;
			while (io_size > 0) {
				if (0 == psize) psize = buf[0];
				if (io_size + pr_size >= psize) {
					// ���� ��Ŷ �ϼ� ����
					unsigned char packet[MAX_PACKET_SIZE];
					memcpy(packet, g_clients[ci].packet_buf, pr_size);
					memcpy(packet + pr_size, buf, psize - pr_size);
					ProcessPacket(static_cast<int>(ci), packet);
					io_size -= psize - pr_size;
					buf += psize - pr_size;
					psize = 0; pr_size = 0;
				}
				else {
					memcpy(g_clients[ci].packet_buf + pr_size, buf, io_size);
					pr_size += io_size;
					io_size = 0;
				}
			}
			g_clients[ci].curr_packet_size = psize;
			g_clients[ci].prev_packet_data = pr_size;
			DWORD recv_flag = 0;
			int ret = WSARecv(g_clients[ci].client_socket,
				&g_clients[ci].recv_over.wsabuf, 1,
				NULL, &recv_flag, &g_clients[ci].recv_over.over, NULL);
			if (SOCKET_ERROR == ret) {
				int err_no = WSAGetLastError();
				if (err_no != WSA_IO_PENDING)
				{
					//error_display("RECV ERROR", err_no);
					DisconnectClient(client_id);
				}
			}
		}
		else if (OP_SEND == over->event_type) {
			if (io_size != over->wsabuf.len) {
				// std::cout << "Send Incomplete Error!\n";
				DisconnectClient(client_id);
			}
			delete over;
		}
		else if (OP_DO_MOVE == over->event_type) {
			// Not Implemented Yet
			delete over;
		}
		else {
			std::cout << "Unknown GQCS event!\n";
			while (true);
		}
	}
}

constexpr int DELAY_LIMIT = 100;
constexpr int DELAY_LIMIT2 = 150;
constexpr int ACCEPT_DELY = 30;

void Adjust_Number_Of_Client()
{
	static int delay_multiplier = 1;
	static int max_limit = MAX_TEST;
	static auto last_limit_increase = high_resolution_clock::now();

	if (active_clients >= MAX_TEST) return;
	if (num_connections >= MAX_CLIENTS) return;

	auto duration = high_resolution_clock::now() - last_connect_time;
	if (ACCEPT_DELY * delay_multiplier > duration_cast<milliseconds>(duration).count()) return;

	int t_delay = global_delay;
	if (DELAY_LIMIT2 < t_delay) {
		// 딜레이 한계 돌파: 상한선을 현재 인원의 95%로 동적 하향 조정
		max_limit = active_clients - (active_clients / 20);
		if (100 > active_clients) return;
		if (ACCEPT_DELY * 10 > duration_cast<milliseconds>(duration).count()) return;
		last_connect_time = high_resolution_clock::now();
		DisconnectClient(client_to_close);
		++client_to_close;
		return;
	}
	else if (DELAY_LIMIT < t_delay) {
		delay_multiplier = 10;
		// 주의 구간: 상한선을 넘으면 추가 접속 금지
		if (active_clients >= max_limit) return;
	}
	else {
		delay_multiplier = 1;
		// 안정 구간(<100ms): 서버가 여유가 있으므로 상한선을 초당 50명씩 서서히 올려 다시 한계 돌파 시도
		auto limit_duration = high_resolution_clock::now() - last_limit_increase;
		if (duration_cast<milliseconds>(limit_duration).count() > 20) {
			max_limit++;
			last_limit_increase = high_resolution_clock::now();
		}
		if (active_clients >= max_limit) return;
	}
	last_connect_time = high_resolution_clock::now();
	g_clients[num_connections].client_socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN ServerAddr;
	ZeroMemory(&ServerAddr, sizeof(SOCKADDR_IN));
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(PORT);
	ServerAddr.sin_addr.s_addr = inet_addr(g_server_ip);


	int Result = WSAConnect(g_clients[num_connections].client_socket, (sockaddr*)&ServerAddr, sizeof(ServerAddr), NULL, NULL, NULL, NULL);
	if (0 != Result) {
		error_display("WSAConnect : ", GetLastError());
	}

	g_clients[num_connections].curr_packet_size = 0;
	g_clients[num_connections].prev_packet_data = 0;
	ZeroMemory(&g_clients[num_connections].recv_over, sizeof(g_clients[num_connections].recv_over));
	g_clients[num_connections].recv_over.event_type = OP_RECV;
	g_clients[num_connections].recv_over.wsabuf.buf =
		reinterpret_cast<CHAR*>(g_clients[num_connections].recv_over.IOCP_buf);
	g_clients[num_connections].recv_over.wsabuf.len = sizeof(g_clients[num_connections].recv_over.IOCP_buf);

	DWORD recv_flag = 0;
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_clients[num_connections].client_socket), g_hiocp, num_connections, 0);

	int ret = WSARecv(g_clients[num_connections].client_socket, &g_clients[num_connections].recv_over.wsabuf, 1,
		NULL, &recv_flag, &g_clients[num_connections].recv_over.over, NULL);
	if (SOCKET_ERROR == ret) {
		int err_no = WSAGetLastError();
		if (err_no != WSA_IO_PENDING)
		{
			error_display("RECV ERROR", err_no);
			goto fail_to_connect;
		}
	}

	// 연결 직후 즉시 C2S_LOGIN 전송 (서버는 먼저 보내지 않음)
	{
		C2S_Login l_packet;
		memset(&l_packet, 0, sizeof(l_packet));
		sprintf_s(l_packet.username, "DUMMY_%d", (int)num_connections);
		sprintf_s(l_packet.user_id,  "DUMMY_%d", (int)num_connections);
		l_packet.size = sizeof(l_packet);
		l_packet.type = C2S_LOGIN;
		SendPacket(num_connections, &l_packet);
	}

	num_connections++;
fail_to_connect:
	return;
}

void Test_Thread()
{
	while (true) {
		//Sleep(max(20, global_delay));
		Adjust_Number_Of_Client();

		for (int i = 0; i < num_connections; ++i) {
			if (false == g_clients[i].connected) continue;
			if (g_clients[i].last_move_time + 1s > high_resolution_clock::now()) continue;
			g_clients[i].last_move_time = high_resolution_clock::now();
			C2S_Move my_packet;
			my_packet.size = sizeof(my_packet);
			my_packet.type = C2S_MOVE;
			int dx = 0; int dy = 0;
			switch (rand() % 4) {
			case 0: dx = -1; break;
			case 1: dx = 1; break;
			case 2: dy = -1; break;
			case 3: dy = 1; break;
			}
			my_packet.x = dx;
			my_packet.y = dy;
			my_packet.move_time = static_cast<unsigned>(duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch()).count());
			SendPacket(i, &my_packet);
		}
	}
}

void InitializeNetwork()
{
	for (auto& cl : g_clients) {
		cl.connected = false;
		cl.id = INVALID_ID;
	}

	client_map.clear();
	num_connections = 0;
	last_connect_time = high_resolution_clock::now();

	WSADATA	wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	g_hiocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, NULL, 0);

	for (int i = 0; i < 6; ++i)
		worker_threads.push_back(new std::thread{ Worker_Thread });

	test_thread = thread{ Test_Thread };
}

void ShutdownNetwork()
{
	test_thread.join();
	for (auto pth : worker_threads) {
		pth->join();
		delete pth;
	}
}

void Do_Network()
{
	return;
}

void GetPointCloud(int* size, float** points)
{
	int index = 0;
	for (int i = 0; i < num_connections; ++i)
		if (true == g_clients[i].connected) {
			point_cloud[index * 2] = static_cast<float>(g_clients[i].x);
			point_cloud[index * 2 + 1] = static_cast<float>(g_clients[i].y);
			index++;
		}

	*size = index;
	*points = point_cloud;
}

