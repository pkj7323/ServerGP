#include <iostream>
#include <WS2tcpip.h>
#include <array>
#pragma comment(lib, "WS2_32.lib")
#include <MSWSock.h>
#pragma comment(lib, "MSWSock.lib")
#include <vector>

#include "Protocol.h"
#include <tbb/concurrent_unordered_map.h>

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
constexpr int BUF_SIZE = 1024;
std::atomic<int> player_index = 0;
HANDLE h_iocp;
SOCKET server_socket;
enum class io_type
{
	send,
	recv,
	accept,
	count
};



class EXP_OVER {
public:
	WSAOVERLAPPED	over;
	io_type			type = io_type::count;
	WSABUF			wsabuffer;
	char			buff[BUF_SIZE];
	SOCKET			accept_socket;
	EXP_OVER()
	{
		ZeroMemory(&over, sizeof(over));
		wsabuffer.buf = buff;
		wsabuffer.len = BUF_SIZE;
	}
	EXP_OVER(io_type type) : type{ type }
	{
		ZeroMemory(&over, sizeof(over));
		wsabuffer.buf = buff;
		wsabuffer.len = BUF_SIZE;
	}
	
};

void broadcast_new_player(int new_player_id);
void broadcast_player_remove_packet(int player_id);
enum class client_state
{
	connected,
	playing,
	logout
};
class SESSION {
public:
	SOCKET			client;
	int				id;
	EXP_OVER		recv_over;
	int				prev_recv_count = 0;
	client_state	state;

	int16_t 		x;
	int16_t 		y;
	char			userName[MAX_NAME_LEN];
	SESSION() : x{0}, y{0}, userName{}
	{
		prev_recv_count = 0;
		state = client_state::connected;
		id = 999;
		client = INVALID_SOCKET;
		recv_over.type = io_type::recv;
	}
	~SESSION()
	{
		if (state == client_state::playing || state == client_state::connected)
		{
			closesocket(client);
		}
		
	}
	void init()
	{
		prev_recv_count = 0;
		state = client_state::connected;
		id = 999;
		closesocket(client);
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
		// 남은 데이터(prev_recv_count) 뒤부터 이어 받도록 수정
		recv_over.wsabuffer.buf = recv_over.buff + prev_recv_count;
		recv_over.wsabuffer.len = BUF_SIZE - prev_recv_count;
		WSARecv(client, &recv_over.wsabuffer, 1, 0, &recv_flag, &recv_over.over, nullptr);
	}
	void do_send(int num_bytes, char* mess)
	{
		EXP_OVER* o = new EXP_OVER(io_type::send);
		o->wsabuffer.len = num_bytes;
		memcpy(o->wsabuffer.buf, mess, num_bytes);
		WSASend(client, &o->wsabuffer, 1, 0, 0, &o->over, nullptr);
	}

	void proccess_packet(unsigned char* buff);
	

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


	void send_login_success()
	{
		s2c_login_ack ack_packet;
		ack_packet.size = sizeof(s2c_login_ack);
		ack_packet.type = packet_type::S2C_LOGIN_ACK;
		ack_packet.success = true;
		strcpy_s(ack_packet.msg, "Login successful.");
		do_send(ack_packet.size, reinterpret_cast<char*>(&ack_packet));
	}
	void send_remove_player(int player_id)
	{
		s2c_remove_player packet;
		packet.size = sizeof(s2c_remove_player);
		packet.type = packet_type::S2C_REMOVE_PLAYER;
		packet.id = player_id;
		do_send(packet.size, reinterpret_cast<char*>(&packet));
	}
	void send_move_packet(int mover);
	void send_add_player(int player_id);
	void send_already_spawn_players();
};

tbb::concurrent_unordered_map
	<int, std::atomic<std::shared_ptr<SESSION>>> clients;


void SESSION::proccess_packet(unsigned char* buff)
{
	packet_type type = *reinterpret_cast<packet_type*>(&buff[1]);
	switch (type)
	{
	case packet_type::C2S_LOGIN:
	{
		c2s_login* p = reinterpret_cast<c2s_login*>(buff);
		strncpy_s(userName, p->userName, MAX_NAME_LEN);
		std::cout << "Client[" << id << "] Login: " << userName << std::endl;
		state = client_state::playing;
		send_avatar_info();
		send_already_spawn_players();
		broadcast_new_player(id);
		break;
	}
	case packet_type::C2S_MOVE:
	{
		c2s_move* packet = reinterpret_cast<c2s_move*>(buff);
		int16_t dx = packet->dir.x;
		int16_t dy = packet->dir.y;

		int16_t new_x = x + dx;
		int16_t new_y = y + dy;
		if (new_x >= 0 && new_x < WORLD_WIDTH)
		{
			x = new_x;
		}
		if (new_y >= 0 && new_y < WORLD_HEIGHT)
		{
			y = new_y;
		}

		std::cout << "Player[" << id << "] moved to (" << x << ", " << y << ")\n";
		for (auto& [id, session] : clients)
		{
			std::shared_ptr<SESSION> s = session.load();
			if (nullptr == s) continue;
			if (client_state::playing == s->state)
				s->send_move_packet(id);
		}
		break;
	}

	default:
		std::cout << "Unknown Packet Type from Client[" << id << "]" << std::endl;
		break;
	}

}


void SESSION::send_move_packet(int move_player_id) 
{
	std::shared_ptr<SESSION> player = clients[move_player_id].load();
	s2c_player_move move_packet;
	move_packet.size = sizeof(s2c_player_move);
	move_packet.type = packet_type::S2C_PLAYER_MOVE;
	move_packet.id = player->id;
	move_packet.x = player->x;
	move_packet.y = player->y;
	do_send(move_packet.size, reinterpret_cast<char*>(&move_packet));
}
void SESSION::send_add_player(int player_id)
{
	std::shared_ptr<SESSION> player = clients[player_id].load();
	s2c_add_player add_packet;
	add_packet.size = sizeof(s2c_add_player);
	add_packet.type = packet_type::S2C_ADD_PLAYER;
	add_packet.id = player_id;
	strncpy_s(add_packet.userName, player->userName, MAX_NAME_LEN);
	add_packet.x = player->x;
	add_packet.y = player->y;
	do_send(sizeof(s2c_add_player), reinterpret_cast<char*>(&add_packet));
}

void SESSION::send_already_spawn_players()
{
	for (auto& [id, session] : clients)
	{
		if (id != this->id)
		{
			std::shared_ptr<SESSION> o = session.load();
			if (nullptr == o) continue;
			if (client_state::playing == o->state)
				send_add_player(id);
		}
	}
}

void broadcast_new_player(int new_player_id)
{
	for (auto& [id, session] : clients)
	{
		if (id != new_player_id)
		{
			std::shared_ptr<SESSION> o = session.load();
			if (nullptr == o) continue;
			if (client_state::playing == o->state)
				o->send_add_player(new_player_id);
		}
	}
}
void broadcast_player_remove_packet(int player_id)
{
	for (auto& [id, session] : clients)
	{
		if (id != player_id)
		{
			std::shared_ptr<SESSION> o = session.load();
			if (nullptr == o) continue;
			if (client_state::playing == o->state)
				o->send_remove_player(player_id);
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

void client_disconnect(int client_id)
{
	std::cout << "client[" << client_id << "] Disconnected.\n";
	std::shared_ptr<SESSION> cl = clients[client_id].load();
	if (nullptr != cl) {
		cl->state = client_state::logout;
		broadcast_player_remove_packet(cl->id);
		closesocket(cl->client);
		cl->client = INVALID_SOCKET;
	}
	clients[client_id].store(nullptr);
}

void worker_thread()
{
	while (true)
	{
		DWORD bytes_transferred;
		ULONG_PTR key;
		LPOVERLAPPED over;
		GetQueuedCompletionStatus(h_iocp, &bytes_transferred, &key, &over, INFINITE);
		EXP_OVER* o = reinterpret_cast<EXP_OVER*>(over);
		int id = static_cast<int>(key);
		int num_bytes = static_cast<int>(bytes_transferred);
		if (over == nullptr)
		{
			error_display(L"GQCS Errror: ", WSAGetLastError());
			if (key == -1) {
				exit(-1);
			}
			client_disconnect(id);
			continue;
		}
		std::shared_ptr<SESSION> session = clients[id].load();
		if (o->type == io_type::recv && bytes_transferred == 0 && WSAGetLastError() != WSA_IO_PENDING)
		{
			client_disconnect(id);
			continue;
		}
		if (o->type == io_type::send && bytes_transferred == 0 && WSAGetLastError() != WSA_IO_PENDING)
		{
			client_disconnect(id);
			delete o; // send 완료 후 오버랩드 객체 삭제
			continue;
		}
		switch (o->type)
		{
		case io_type::accept:
		{
			std::cout << "New Client Connected!" << std::endl;
			CreateIoCompletionPort((HANDLE)o->accept_socket, h_iocp, player_index, 0);
			++player_index;
			std::shared_ptr<SESSION> new_session = std::make_shared<SESSION>();
			new_session->id = player_index;
			new_session->client = o->accept_socket;
			new_session->state = client_state::connected;
			new_session->x = 0;
			new_session->y = 0;
			new_session->send_login_success();
			new_session->do_recv();
			clients.emplace(player_index, new_session);
				
			
			o->accept_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			AcceptEx(server_socket, o->accept_socket, &o->buff, 0,
				sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
				NULL, &o->over);
			break;
		}
		case io_type::recv:
		{
			std::shared_ptr<SESSION> cl = clients[id].load();
			if (nullptr == cl)
			{
				break;
			}
			unsigned char* p = reinterpret_cast<unsigned char*>(o->buff);
			int data_size = num_bytes + cl->prev_recv_count;
			while (data_size > 0)
			{
				unsigned char packet_size = p[0];
				if (packet_size > data_size)
				{
					break;
				}
				cl->proccess_packet(p);
				p += packet_size;
				data_size -= packet_size;
			}
			if (data_size > 0)
			{
				memmove(cl->recv_over.buff, p, data_size);
				cl->prev_recv_count = data_size;
			}
			else
			{
				cl->prev_recv_count = 0;
				ZeroMemory(o->buff, sizeof(o->buff));
			}

			cl->do_recv();
			break;
		}
		case io_type::send:
		{
			//packet_type sent_type = (*reinterpret_cast<packet_type*>(&o->wsabuffer.buf[1]));
			//cout << "Send Complete to Client[" << session.id << "], Bytes Sent: " << bytes_transferred
			//	<< " Packet Type = " << static_cast<uint16_t>(sent_type) << endl;
			delete o;
			break;
		}
		default:
			std::cout << "Unknown IO Type!" << std::endl;
			exit(-1);
			break;
		}
	}
}

int main()
{
	std::wcout.imbue(std::locale("korean"));
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	server_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;

	int64_t ret = bind(server_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));

	listen(server_socket, SOMAXCONN);

	h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	CreateIoCompletionPort((HANDLE)server_socket, h_iocp, 0, 0);


	EXP_OVER accept_over(io_type::accept);
	accept_over.accept_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	AcceptEx(server_socket, accept_over.accept_socket, &accept_over.buff, 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, NULL, &accept_over.over);


	std::vector<std::thread> worker_threads;
	for (unsigned int i = 0; i < std::thread::hardware_concurrency(); ++i)
	{
		worker_threads.emplace_back(worker_thread);
	}
	for (auto& t : worker_threads)
	{
		t.join();
	}
	
	closesocket(server_socket);
	WSACleanup();
}
