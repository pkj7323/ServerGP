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
#include <deque>
#include <string>
#include <fstream>

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
	high_resolution_clock::time_point login_start;
	atomic_int login_state; // DB 부하 봇 전용. 0: 대기, 1: 성공, 2: 실패
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

// --- 성능 비교 실험 설정. EXP_BOTS가 없으면 기존 자동 조절 동작을 그대로 쓴다. ---
struct ExpBotConfig {
	int bots = 0;           // EXP_BOTS: 고정 인원
	int connect_ms = 30;    // EXP_CONNECT_MS: 접속 간격
	int duration_s = 0;     // EXP_DURATION: 지나면 프로세스 종료
	int seed = 1;           // EXP_SEED: 이동 난수 시드
	int db_rate = 0;        // EXP_DB_RATE: 초당 로그인 수 (DBSIM_ 계정)
	int db_users = 1000;    // EXP_DB_USERS: 시드해 둔 DBSIM_ 계정 수
	int db_hold_ms = 1000;  // EXP_DB_HOLD_MS: 로그인 후 유지 시간
	int adaptive = 0;       // EXP_ADAPTIVE: 1이면 기존 자동 조절로 접속 수를 정한다 (EXP_BOTS는 상한)
	int stop_gain = 0;      // EXP_STOP_GAIN: 60초 동안 늘어난 접속이 이 값 미만이면 종료 (0: 끔)
	string csv = "bot_metrics.csv"; // EXP_BOT_CSV
} g_exp;

static int Exp_Env_Int(const char* name, int fallback)
{
	char value[32];
	DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
	return (len > 0 && len < sizeof(value)) ? atoi(value) : fallback;
}

constexpr int EXP_BUCKET_US = 100;
constexpr int EXP_BUCKETS = 100000; // 100us 단위로 10초까지

struct ExpHistogram {
	vector<atomic<int>> buckets;

	ExpHistogram() : buckets(EXP_BUCKETS) {}

	void Add(long long us)
	{
		int i = static_cast<int>(us / EXP_BUCKET_US);
		if (i >= EXP_BUCKETS) i = EXP_BUCKETS - 1;
		buckets[i].fetch_add(1, memory_order_relaxed);
	}
};
ExpHistogram g_rtt_hist;
ExpHistogram g_login_hist;
atomic<long long> g_moves_sent{ 0 };
atomic<long long> g_recv_bytes{ 0 };
atomic<long long> g_login_ok{ 0 };
atomic<long long> g_login_fail{ 0 };
atomic<long long> g_login_timeout{ 0 };

struct ExpPercentiles {
	long long count = 0;
	double avg = 0, p50 = 0, p95 = 0, p99 = 0, max = 0;
};

// 히스토그램을 비우면서 ms 단위 백분위를 구한다.
static ExpPercentiles Exp_Drain(ExpHistogram& hist)
{
	static thread_local vector<int> counts(EXP_BUCKETS);
	ExpPercentiles r;
	double total_us = 0;
	for (int i = 0; i < EXP_BUCKETS; ++i) {
		counts[i] = hist.buckets[i].exchange(0, memory_order_relaxed);
		r.count += counts[i];
		total_us += counts[i] * (i + 0.5) * EXP_BUCKET_US;
	}
	if (r.count == 0) return r;
	r.avg = total_us / r.count / 1000.0;
	const long long p50_at = (r.count * 50 + 99) / 100;
	const long long p95_at = (r.count * 95 + 99) / 100;
	const long long p99_at = (r.count * 99 + 99) / 100;
	long long seen = 0;
	bool got50 = false, got95 = false, got99 = false;
	for (int i = 0; i < EXP_BUCKETS; ++i) {
		if (counts[i] == 0) continue;
		seen += counts[i];
		const double ms = (i + 0.5) * EXP_BUCKET_US / 1000.0;
		if (!got50 && seen >= p50_at) { r.p50 = ms; got50 = true; }
		if (!got95 && seen >= p95_at) { r.p95 = ms; got95 = true; }
		if (!got99 && seen >= p99_at) { r.p99 = ms; got99 = true; }
		r.max = ms;
	}
	return r;
}

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
			if (ci >= MAX_TEST) {
				g_login_fail++;
				g_clients[ci].login_state = 2;
			}
			DisconnectClient(ci);
		}
		break;
	}
	case S2C_AVATAR_INFO:
	{
		S2C_AvatarInfo* info = reinterpret_cast<S2C_AvatarInfo*>(packet);
		if (ci >= MAX_TEST) {
			// DB 부하 봇: 로그인 지연만 기록하고 이동은 하지 않는다.
			g_login_hist.Add(duration_cast<microseconds>(high_resolution_clock::now() - g_clients[ci].login_start).count());
			g_login_ok++;
			g_clients[ci].login_state = 1;
			break;
		}
		{
			lock_guard<mutex> lock(client_map_mutex);
			client_map[info->playerId] = ci; // server player id -> local index
		}
		g_clients[ci].id = info->playerId;
		g_clients[ci].x  = info->x;
		g_clients[ci].y  = info->y;
		// 스폰 성공 → 연결 완료로 처리
		g_clients[ci].connected = true;
		++active_clients;
		break;
	}
	case S2C_MOVE_OBJECT:
	{
		S2C_MoveObject* move_packet = reinterpret_cast<S2C_MoveObject*>(packet);
		
		// 다른 플레이어 이동은 무시한다. 봇 쪽 공용 락이 병목이 되지 않도록 락을 잡지 않는다.
		if (move_packet->object_id != g_clients[ci].id) break;
		const int my_id = ci;

		// 내 캐릭터 이동이면 RTT 측정 (move_time은 us 하위 32비트, 차이는 부호 없는 연산으로 구한다)
		if (move_packet->move_time != 0) {
			const unsigned now_us = static_cast<unsigned>(duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count());
			const unsigned rtt_us = now_us - static_cast<unsigned>(move_packet->move_time);
			if (rtt_us < 10000000u) { // 비정상 값 필터링
				g_rtt_hist.Add(rtt_us);
				const int d_ms = static_cast<int>(rtt_us / 1000);
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
			g_recv_bytes.fetch_add(io_size, memory_order_relaxed);
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

// 기존 자동 조절: 지금 접속을 하나 더 늘려도 되는지 판단한다.
static bool Should_Connect_Adaptive()
{
	static int delay_multiplier = 1;
	static int max_limit = MAX_TEST;
	static auto last_limit_increase = high_resolution_clock::now();

	if (active_clients >= MAX_TEST) return false;
	if (num_connections >= MAX_TEST) return false;

	auto duration = high_resolution_clock::now() - last_connect_time;
	if (ACCEPT_DELY * delay_multiplier > duration_cast<milliseconds>(duration).count()) return false;

	int t_delay = global_delay;
	if (DELAY_LIMIT2 < t_delay) {
		// 딜레이 한계 돌파: 상한선을 현재 인원의 95%로 동적 하향 조정
		max_limit = active_clients - (active_clients / 20);
		if (100 > active_clients) return false;
		if (ACCEPT_DELY * 10 > duration_cast<milliseconds>(duration).count()) return false;
		last_connect_time = high_resolution_clock::now();
		DisconnectClient(client_to_close);
		++client_to_close;
		return false;
	}
	else if (DELAY_LIMIT < t_delay) {
		delay_multiplier = 10;
		// 주의 구간: 상한선을 넘으면 추가 접속 금지
		if (active_clients >= max_limit) return false;
	}
	else {
		delay_multiplier = 1;
		// 안정 구간(<100ms): 서버가 여유가 있으므로 상한선을 초당 50명씩 서서히 올려 다시 한계 돌파 시도
		auto limit_duration = high_resolution_clock::now() - last_limit_increase;
		if (duration_cast<milliseconds>(limit_duration).count() > 20) {
			max_limit++;
			last_limit_increase = high_resolution_clock::now();
		}
		if (active_clients >= max_limit) return false;
	}
	return true;
}

// 소켓을 만들어 접속하고 C2S_LOGIN을 보낸다.
static bool Connect_Slot(int slot, const char* user_id)
{
	g_clients[slot].client_socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

	// 서버와 같게 맞춘다. 없으면 Nagle과 지연 ACK가 겹쳐 RTT에 200ms대 튐이 섞일 수 있다.
	BOOL no_delay = TRUE;
	setsockopt(g_clients[slot].client_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&no_delay, sizeof(no_delay));

	SOCKADDR_IN ServerAddr;
	ZeroMemory(&ServerAddr, sizeof(SOCKADDR_IN));
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(PORT);
	ServerAddr.sin_addr.s_addr = inet_addr(g_server_ip);


	int Result = WSAConnect(g_clients[slot].client_socket, (sockaddr*)&ServerAddr, sizeof(ServerAddr), NULL, NULL, NULL, NULL);
	if (0 != Result) {
		error_display("WSAConnect : ", GetLastError());
	}

	g_clients[slot].curr_packet_size = 0;
	g_clients[slot].prev_packet_data = 0;
	ZeroMemory(&g_clients[slot].recv_over, sizeof(g_clients[slot].recv_over));
	g_clients[slot].recv_over.event_type = OP_RECV;
	g_clients[slot].recv_over.wsabuf.buf =
		reinterpret_cast<CHAR*>(g_clients[slot].recv_over.IOCP_buf);
	g_clients[slot].recv_over.wsabuf.len = sizeof(g_clients[slot].recv_over.IOCP_buf);

	DWORD recv_flag = 0;
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_clients[slot].client_socket), g_hiocp, slot, 0);

	int ret = WSARecv(g_clients[slot].client_socket, &g_clients[slot].recv_over.wsabuf, 1,
		NULL, &recv_flag, &g_clients[slot].recv_over.over, NULL);
	if (SOCKET_ERROR == ret) {
		int err_no = WSAGetLastError();
		if (err_no != WSA_IO_PENDING)
		{
			error_display("RECV ERROR", err_no);
			return false;
		}
	}

	// 연결 직후 즉시 C2S_LOGIN 전송 (서버는 먼저 보내지 않음)
	C2S_Login l_packet;
	memset(&l_packet, 0, sizeof(l_packet));
	sprintf_s(l_packet.username, "%s", user_id);
	sprintf_s(l_packet.user_id, "%s", user_id);
	l_packet.size = sizeof(l_packet);
	l_packet.type = C2S_LOGIN;
	SendPacket(slot, &l_packet);
	return true;
}

void Adjust_Number_Of_Client()
{
	if (g_exp.bots > 0 && !g_exp.adaptive) {
		if (num_connections >= g_exp.bots) return;
		auto since_last = high_resolution_clock::now() - last_connect_time;
		if (g_exp.connect_ms > duration_cast<milliseconds>(since_last).count()) return;
	}
	else if (!Should_Connect_Adaptive()) {
		return;
	}
	if (g_exp.adaptive && num_connections >= g_exp.bots) return;
	last_connect_time = high_resolution_clock::now();

	char user_id[32];
	sprintf_s(user_id, "DUMMY_%d", (int)num_connections);
	if (Connect_Slot(num_connections, user_id)) {
		num_connections++;
	}
}

void Test_Thread()
{
	srand(g_exp.seed);
	while (true) {
		//Sleep(max(20, global_delay));
		if (g_exp.bots > 0) Sleep(1); // 고정 인원 측정 때는 봇이 코어 하나를 다 먹지 않게 한다
		else Adjust_Number_Of_Client();

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
			my_packet.move_time = static_cast<unsigned>(duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count());
			SendPacket(i, &my_packet);
			g_moves_sent.fetch_add(1, memory_order_relaxed);
		}
	}
}

// 측정 모드: WSAConnect가 이동 전송을 막지 않도록 접속을 따로 돌린다.
void Connect_Thread()
{
	// 자동 조절은 상한에 닿은 뒤에도 RTT가 나빠지면 접속을 끊어야 하므로 계속 돈다.
	while (g_exp.adaptive || num_connections < g_exp.bots) {
		Sleep(1);
		Adjust_Number_Of_Client();
	}
}

// DB 부하: DBSIM_ 계정으로 접속 → 로그인(DB 조회) → 유지 → 해제(DB 저장)를 일정 속도로 반복한다.
// 슬롯은 재사용하지 않는다. 닫힌 소켓의 완료 통지가 새 연결과 섞이지 않게 하기 위해서다.
void DB_Churn_Thread()
{
	struct Live { int slot; high_resolution_clock::time_point start; };
	deque<Live> live;
	int next_slot = MAX_TEST;
	int seq = 0;
	const auto interval = microseconds(1000000 / g_exp.db_rate);
	auto next_time = high_resolution_clock::now();

	while (next_slot < MAX_CLIENTS || !live.empty()) {
		auto now = high_resolution_clock::now();
		if (now >= next_time && next_slot < MAX_CLIENTS) {
			next_time += interval;
			int slot = next_slot++;
			char user_id[32];
			sprintf_s(user_id, "DBSIM_%d", seq++ % g_exp.db_users);
			g_clients[slot].login_state = 0;
			g_clients[slot].login_start = now;
			if (Connect_Slot(slot, user_id)) {
				live.push_back({ slot, now });
			}
		}
		while (!live.empty()) {
			const Live& front = live.front();
			auto elapsed = duration_cast<milliseconds>(now - front.start).count();
			const int state = g_clients[front.slot].login_state;
			const bool timed_out = elapsed >= 10000;
			if (!(timed_out || (state != 0 && elapsed >= g_exp.db_hold_ms))) break;
			if (state == 0) g_login_timeout++;
			closesocket(g_clients[front.slot].client_socket);
			live.pop_front();
		}
		Sleep(1);
	}
}

// 최대 동접 측정: 접속이 더 늘지 않으면 끝낸다.
static bool Exp_Should_Stop(int active)
{
	static deque<int> active_window;

	active_window.push_back(active);
	if (active_window.size() > 61) active_window.pop_front();

	return g_exp.stop_gain > 0 && active_window.size() == 61
		&& active_window.back() - active_window.front() < g_exp.stop_gain;
}

void Exp_Report_Thread()
{
	ofstream csv(g_exp.csv);
	csv << "sec,active,rtt_samples,rtt_avg_ms,rtt_p50_ms,rtt_p95_ms,rtt_p99_ms,rtt_max_ms,moves_sent,recv_bytes,"
		"login_ok,login_fail,login_timeout,login_p50_ms,login_p99_ms\n";
	for (int sec = 1;; ++sec) {
		Sleep(1000);
		const ExpPercentiles rtt = Exp_Drain(g_rtt_hist);
		const ExpPercentiles login = Exp_Drain(g_login_hist);
		csv << sec << ',' << active_clients.load() << ',' << rtt.count << ',' << rtt.avg << ',' << rtt.p50 << ','
			<< rtt.p95 << ',' << rtt.p99 << ',' << rtt.max << ',' << g_moves_sent.exchange(0) << ','
			<< g_recv_bytes.exchange(0) << ',' << g_login_ok.exchange(0) << ',' << g_login_fail.exchange(0) << ','
			<< g_login_timeout.exchange(0) << ',' << login.p50 << ',' << login.p99 << endl;
		if (g_exp.duration_s > 0 && sec >= g_exp.duration_s) ExitProcess(0);
		if (Exp_Should_Stop(active_clients.load())) ExitProcess(0);
	}
}

void InitializeNetwork()
{
	g_exp.bots = Exp_Env_Int("EXP_BOTS", 0);
	g_exp.connect_ms = Exp_Env_Int("EXP_CONNECT_MS", g_exp.connect_ms);
	g_exp.duration_s = Exp_Env_Int("EXP_DURATION", 0);
	g_exp.seed = Exp_Env_Int("EXP_SEED", 1);
	g_exp.db_rate = Exp_Env_Int("EXP_DB_RATE", 0);
	g_exp.db_users = Exp_Env_Int("EXP_DB_USERS", g_exp.db_users);
	g_exp.db_hold_ms = Exp_Env_Int("EXP_DB_HOLD_MS", g_exp.db_hold_ms);
	g_exp.adaptive = Exp_Env_Int("EXP_ADAPTIVE", 0);
	g_exp.stop_gain = Exp_Env_Int("EXP_STOP_GAIN", 0);
	char csv_path[MAX_PATH];
	if (GetEnvironmentVariableA("EXP_BOT_CSV", csv_path, MAX_PATH) > 0) g_exp.csv = csv_path;

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
	if (g_exp.bots > 0) {
		thread{ Exp_Report_Thread }.detach();
		thread{ Connect_Thread }.detach();
		if (g_exp.db_rate > 0) thread{ DB_Churn_Thread }.detach();
	}
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

