#include <iostream>
#include <WS2tcpip.h>
#include <array>
#pragma comment(lib, "WS2_32.lib")
#include <MSWSock.h>
#pragma comment(lib, "MSWSock.lib")
#include "Protocol.h"

void error_display(const std::wstring& msg, int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	std::wcout << msg;
	std::wcout << L"=== 에러 " << lpMsgBuf << std::endl;
	__debugbreak();
	// 디버깅 용
	LocalFree(lpMsgBuf);
}
using namespace std;
constexpr int BUF_SIZE = 1024;
enum class io_type
{
	send,
	recv,
	accept,
	count
};


void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags);
void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags);

class EXP_OVER {
public:
	WSAOVERLAPPED	over;
	io_type			type = io_type::count;
	WSABUF			wsabuffer;
	char			buff[BUF_SIZE];
	EXP_OVER()
	{
		ZeroMemory(&over, sizeof(over));
		wsabuffer.buf = buff;
		wsabuffer.len = BUF_SIZE;
	}
	EXP_OVER(io_type type, int num_bytes, char* mess) : type{ type }
	{
		ZeroMemory(&over, sizeof(over));
		wsabuffer.buf = buff;
		wsabuffer.len = num_bytes;
	}
};


class SESSION {
public:
	SOCKET			client;
	int				id;
	bool			is_connected;
	EXP_OVER		recv_over;
	int16_t 		x;
	int16_t 		y;
	char			userName[MAX_NAME_LEN];
	int				prev_recv = 0;
	SESSION() : x{0}, y{0}, userName{}
	{
		prev_recv = 0;
		is_connected = false;
		id = 999;
		client = INVALID_SOCKET;
		recv_over.type = io_type::recv;
	}

	SESSION(int id, SOCKET so) : id(id), client(so), x{0}, y{0}, userName{}
	{
		prev_recv = 0;
		is_connected = true;
		// 객체 대입을 하지 말고 직접 초기화하세요.
		ZeroMemory(&recv_over.over, sizeof(WSAOVERLAPPED));
		recv_over.type = io_type::recv;
		recv_over.wsabuffer.buf = recv_over.buff; // 본인의 버퍼 주소로 재설정
		recv_over.wsabuffer.len = BUF_SIZE;
	}

	~SESSION()
	{
		if (is_connected)
		{
			closesocket(client);
		}
		
	}
	void init()
	{
		prev_recv = 0;
		is_connected = false;
		id = 999;
		client = INVALID_SOCKET;
		x = 0;
		y = 0;
		ZeroMemory(userName, sizeof(userName));
		ZeroMemory(&recv_over.over, sizeof(WSAOVERLAPPED));
		recv_over.type = io_type::recv;
		recv_over.wsabuffer.buf = recv_over.buff; // 본인의 버퍼 주소로 재설정
		recv_over.wsabuffer.len = BUF_SIZE;
	}
	void do_recv()
	{
		DWORD recv_flag = 0;
		recv_over.over = {};
		recv_over.type = io_type::recv;
		recv_over.wsabuffer.buf = recv_over.buff; // 안전하게 여기서 다시 잡아줌
		recv_over.wsabuffer.len = BUF_SIZE;	
		WSARecv(client, &recv_over.wsabuffer, 1, 0, &recv_flag, &recv_over.over, nullptr);
	}
	void do_send(int num_bytes, char* mess)
	{
		EXP_OVER* o = new EXP_OVER(io_type::send, num_bytes, mess);
		o->wsabuffer.buf = o->buff; // 안전하게 여기서 다시 잡아줌
		o->wsabuffer.len = num_bytes;
		memcpy(o->wsabuffer.buf, mess, num_bytes);
		ZeroMemory(&o->over, sizeof(WSAOVERLAPPED));
		WSASend(client, &o->wsabuffer, 1, 0, 0, &o->over, nullptr);
	}

	void proccess_packet(unsigned char* buff)
	{
		packet_type* type = reinterpret_cast<packet_type*>(&buff[1]);
		switch (*type)
		{
		case packet_type::C2S_LOGIN:
		{
			c2s_login* p = reinterpret_cast<c2s_login*>(buff);
			strncpy_s(userName, p->userName, MAX_NAME_LEN);
			cout << "Client[" << id << "] Login: " << userName << endl;
			send_avatar_info();
			break;
		}
		case packet_type::C2S_MOVE:
		{
			c2s_move* p = reinterpret_cast<c2s_move*>(buff);
			if (x > 0 && x < WORLD_WIDTH)
			{
				x += p->dir.x;
			}
			if (y > 0 && y < WORLD_HEIGHT)
			{
				y += p->dir.y;
			}
			cout << "Client[" << id << "] Move to (" << x << ", " << y << ")" << endl;
			send_move_packet();
			break;
		}
		default:
			cout << "Unknown Packet Type from Client[" << id << "]" << endl;
			break;
		}
		
	}

	void send_avatar_info()
	{
		s2c_avatar_info info_packet;
		info_packet.size = sizeof(s2c_avatar_info);
		info_packet.type = packet_type::S2C_AVATAR_INFO;
		info_packet.id = id;
		info_packet.x = x;
		info_packet.y = y;
		do_send(sizeof(s2c_avatar_info), reinterpret_cast<char*>(&info_packet));
	}

	void send_move_packet()
	{
		s2c_player_move move_packet;
		move_packet.size = sizeof(s2c_player_move);
		move_packet.type = packet_type::S2C_PLAYER_MOVE;
		move_packet.id = id;
		move_packet.x = x;
		move_packet.y = y;
		do_send(sizeof(s2c_player_move), reinterpret_cast<char*>(&move_packet));
	}


	void send_login_ack()
	{
		s2c_login_ack ack_packet;
		ack_packet.size = sizeof(s2c_login_ack);
		ack_packet.type = packet_type::S2C_LOGIN_ACK;
		ack_packet.success = true;
		strcpy_s(ack_packet.msg, "Welcome");
		do_send(sizeof(s2c_login_ack), reinterpret_cast<char*>(&ack_packet));
	}
	void send_add_player(int player_id);
};

std::array<SESSION, MAX_PLAYERS> clients;
void SESSION::send_add_player(int player_id)
{
	s2c_add_player add_packet;
	add_packet.size = sizeof(s2c_add_player);
	add_packet.type = packet_type::S2C_ADD_PLAYER;
	add_packet.id = player_id;
	strncpy_s(add_packet.userName, clients[player_id].userName, MAX_NAME_LEN);
	add_packet.x = clients[player_id].x;
	add_packet.y = clients[player_id].y;
	do_send(sizeof(s2c_add_player), reinterpret_cast<char*>(&add_packet));
}

void broadcast_new_player(int new_player_id)
{
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (clients[i].is_connected && clients[i].id != new_player_id)
		{
			clients[i].send_add_player(new_player_id);
		}
	}
}
void broadcast_move_packet(int player_id)
{
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (clients[i].is_connected && clients[i].id != player_id)
		{
			s2c_player_move move_packet;
			move_packet.size = sizeof(s2c_player_move);
			move_packet.type = packet_type::S2C_PLAYER_MOVE;
			move_packet.id = player_id;
			move_packet.x = clients[player_id].x;
			move_packet.y = clients[player_id].y;

			clients[i].do_send(move_packet.size, reinterpret_cast<char*>(&move_packet));
		}
	}
}
void broadcast_remove_packet(int player_id)
{
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (clients[i].is_connected && clients[i].id != player_id)
		{
			s2c_remove_player remove_packet;
			remove_packet.size = sizeof(s2c_remove_player);
			remove_packet.type = packet_type::S2C_REMOVE_PLAYER;
			remove_packet.id = player_id;
			clients[i].do_send(remove_packet.size, reinterpret_cast<char*>(&remove_packet));
		}
	}
	
}


void send_login_fail(SOCKET client, const char* text)
{
	s2c_login_ack ack_packet;
	ack_packet.size = sizeof(s2c_login_ack);
	ack_packet.type = packet_type::S2C_LOGIN_ACK;
	ack_packet.success = false;
	strncpy_s(ack_packet.msg, text, sizeof(ack_packet.msg));
	WSABUF wsabuf;
	wsabuf.buf = reinterpret_cast<char*>(&ack_packet);
	wsabuf.len = sizeof(s2c_login_ack);
	WSASend(client, &wsabuf, 1, 0, 0, nullptr, nullptr);
}

int main()
{
	wcout.imbue(locale("korean"));
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	SOCKET server = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;

	bind(server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));

	listen(server, SOMAXCONN);

	HANDLE h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	CreateIoCompletionPort((HANDLE)server, h_iocp, 0, 0);

	SOCKADDR_IN cl_addr;
	SOCKET client = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	EXP_OVER accept_over(io_type::accept, 0, nullptr);

	int index = 0;
	CreateIoCompletionPort((HANDLE)client, h_iocp, index, 0);
	AcceptEx(server, client, &accept_over.buff, 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, NULL, &accept_over.over);
	while (true)
	{
		DWORD bytes_transferred;
		ULONG_PTR key;
		LPOVERLAPPED over;
		GetQueuedCompletionStatus(h_iocp, &bytes_transferred, &key, &over, INFINITE);
		if (over == nullptr)
		{
			error_display(L"GetQueuedCompletionStatus Error", WSAGetLastError());
			continue;
		}
		EXP_OVER* o = reinterpret_cast<EXP_OVER*>(over);
		SESSION session = clients[(int)key];
		if (o->type == io_type::recv && bytes_transferred == 0)
		{
			cout << "Client[" << session.id << "] Disconnected" << endl;
			broadcast_remove_packet(session.id);
			clients[(int)key].init(); // 세션 초기화

		}
		if (o->type == io_type::send && bytes_transferred == 0)
		{
			broadcast_remove_packet(session.id);
			clients[(int)key].init(); // 세션 초기화
			delete o; // send 완료 후 오버랩드 객체 삭제
		}
		switch (o->type)
		{
		case io_type::accept:
		{
			std::cout << "New Client Connected!" << std::endl;
			int player_index = -1;
			for (int i = 0; i < MAX_PLAYERS; ++i)
			{
				if (!clients[i].is_connected)
				{
					player_index = i;
				}
			}
			if (-1 == player_index)
			{
				cout << "Max Players Reached. Connection Refused." << endl;
				send_login_fail(client, "Server is Full.");
				closesocket(client);
			}
			else
			{
				
				CreateIoCompletionPort((HANDLE)client, h_iocp, player_index, 0);
				clients[player_index].is_connected = true;
				clients[player_index].client = client;
				clients[player_index].x = 0;
				clients[player_index].y = 0;
				clients[player_index].id = player_index;
				clients[player_index].send_login_ack();
				

				clients[player_index].do_recv();
			}
			client = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

			accept_over = EXP_OVER(io_type::accept, 0, nullptr);
			AcceptEx(server, client, &accept_over.buff, 0,
				sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
				NULL, &accept_over.over);
			break;
		}
		case io_type::recv:
		{
			int client_id = static_cast<int>(key);
			// 0바이트를 받았다면 클라이언트가 접속을 종료한 것임
			if (bytes_transferred == 0) {
				cout << "Client[" << client_id << "] Disconnected" << endl;
				clients[(client_id)].init(); // 소켓은 소멸자에서 닫힘
				break;
			}
			SESSION& cl = clients[client_id];
			unsigned char* p = reinterpret_cast<unsigned char*>(o->buff);
			int data_size = bytes_transferred + cl.prev_recv;
			while (data_size > 0)
			{
				int packet_size = p[0];
				if (packet_size > data_size)
				{
					break;
				}
				cl.proccess_packet(reinterpret_cast<unsigned char*>(o->buff));
				p += packet_size;
				data_size -= packet_size;
			}
			if (data_size > 0)
			{
				memmove(o->buff, p, data_size);
				cl.prev_recv = data_size;
			}
			else
			{
				cl.prev_recv = 0;
				ZeroMemory(o->buff, sizeof(o->buff));
			}
			
			cl.do_recv();
			break;
		}
		case io_type::send:
		{
			cout << "Send Complete to Client[" << session.id << "], Bytes Sent: " << bytes_transferred << endl;
			delete o;
			break;
		}
			default:
				cout << "Unknown IO Type!" << endl;
				exit(-1);
				break;
		}
	}
	closesocket(server);
	WSACleanup();
}
