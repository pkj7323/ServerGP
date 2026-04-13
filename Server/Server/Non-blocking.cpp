#include <iostream>
#include <WS2tcpip.h>
#include <array>
#pragma comment(lib, "WS2_32.lib")
#include <MSWSock.h>
#pragma comment(lib, "MSWSock.lib")
#include <complex>
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
int player_index = 1;
SOCKET server_socket;
enum class io_type
{
	send,
	recv,
	accept,
	count
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
	SOCKET			client_;
	int				id_;
	int				prev_recv_count_ = 0;
	client_state	state_;
	char			recv_buffer_[BUF_SIZE];

	int16_t 		x_;
	int16_t 		y_;
	char			userName_[MAX_NAME_LEN];
	SESSION() : x_{ 0 }, y_{ 0 }, userName_{}
	{
		prev_recv_count_ = 0;
		state_ = client_state::connected;
		id_ = 999;
		client_ = INVALID_SOCKET;
	}
	~SESSION()
	{
		if (state_ == client_state::playing || state_ == client_state::connected)
		{
			closesocket(client_);
		}

	}
	void init()
	{
		prev_recv_count_ = 0;
		state_ = client_state::connected;
		id_ = 999;
		closesocket(client_);
		client_ = INVALID_SOCKET;
		x_ = 0;
		y_ = 0;
		ZeroMemory(userName_, sizeof(userName_));
	}
	int do_recv()
	{
		return recv(client_, recv_buffer_ + prev_recv_count_,1024 - prev_recv_count_, 0);
	}
	int do_send(int num_bytes, char* mess)
	{
		return send(client_, mess, num_bytes, 0);
	}

	void proccess_packet(unsigned char* buff);


	void send_avatar_info()
	{
		s2c_avatar_info info_packet;
		info_packet.size = sizeof(s2c_avatar_info);
		info_packet.type = packet_type::S2C_AVATAR_INFO;
		info_packet.id = id_;
		info_packet.x = x_;
		info_packet.y = y_;
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

std::unordered_map<int, std::shared_ptr<SESSION>> clients;



void SESSION::proccess_packet(unsigned char* buff)
{
	packet_type type = *reinterpret_cast<packet_type*>(&buff[1]);
	switch (type)
	{
	case packet_type::C2S_LOGIN:
	{
		c2s_login* p = reinterpret_cast<c2s_login*>(buff);
		strncpy_s(userName_, p->userName, MAX_NAME_LEN);
		std::cout << "Client[" << id_ << "] Login: " << userName_ << std::endl;
		state_ = client_state::playing;
		send_avatar_info();
		send_already_spawn_players();
		broadcast_new_player(id_);
		break;
	}
	case packet_type::C2S_MOVE:
	{
		c2s_move* packet = reinterpret_cast<c2s_move*>(buff);
		int16_t dx = packet->dir.x;
		int16_t dy = packet->dir.y;

		int16_t new_x = (int16_t)x_ + dx;
		int16_t new_y = (int16_t)y_ + dy;
		if (new_x >= 0 && new_x < WORLD_WIDTH)
		{
			x_ = new_x;
		}
		if (new_y >= 0 && new_y < WORLD_HEIGHT)
		{
			y_ = new_y;
		}

		std::cout << "Player[" << id_ << "] moved to (" << x_ << ", " << y_ << ")\n";
		for (auto& [id, session] : clients)
		{
			if (nullptr == session) continue;
			if (client_state::playing == session->state_)
				session->send_move_packet(id_);
		}
		break;
	}

	default:
		std::cout << "Unknown Packet Type from Client[" << id_ << "]" << std::endl;
		break;
	}

}


void SESSION::send_move_packet(int move_player_id)
{
	std::shared_ptr<SESSION> player = clients[move_player_id];
	s2c_player_move move_packet;
	move_packet.size = sizeof(s2c_player_move);
	move_packet.type = packet_type::S2C_PLAYER_MOVE;
	move_packet.id = player->id_;
	move_packet.x = player->x_;
	move_packet.y = player->y_;
	do_send(move_packet.size, reinterpret_cast<char*>(&move_packet));
}
void SESSION::send_add_player(int player_id)
{
	std::shared_ptr<SESSION> player = clients[player_id];
	s2c_add_player add_packet;
	add_packet.size = sizeof(s2c_add_player);
	add_packet.type = packet_type::S2C_ADD_PLAYER;
	add_packet.id = player_id;
	strncpy_s(add_packet.userName, player->userName_, MAX_NAME_LEN);
	add_packet.x = player->x_;
	add_packet.y = player->y_;
	do_send(sizeof(s2c_add_player), reinterpret_cast<char*>(&add_packet));
}

void SESSION::send_already_spawn_players()
{
	for (auto& [id, session] : clients)
	{
		if (id != this->id_)
		{
			std::shared_ptr<SESSION> o = session;
			if (nullptr == o) continue;
			if (client_state::playing == o->state_)
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
			std::shared_ptr<SESSION> o = session;
			if (nullptr == o) continue;
			if (client_state::playing == o->state_)
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
			if (nullptr == session) continue;
			if (client_state::playing == session->state_)
				session->send_remove_player(player_id);
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
	std::shared_ptr<SESSION> cl = clients[client_id];
	if (nullptr != cl) {
		cl->state_ = client_state::logout;
		broadcast_player_remove_packet(cl->id_);
		closesocket(cl->client_);
		cl->client_ = INVALID_SOCKET;
	}
	clients[client_id] = nullptr;
}


int main()
{
	std::wcout.imbue(std::locale("korean"));
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	server_socket = socket(AF_INET, SOCK_STREAM, 0);

	unsigned long non_blocking = 1;
	ioctlsocket(server_socket, FIONBIO, &non_blocking);

	SOCKADDR_IN server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	int64_t ret = bind(server_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(server_socket, SOMAXCONN);

	while (true) {
		SOCKADDR_IN c_addr;
		int len = sizeof(c_addr);
		SOCKET client_sock = accept(server_socket, (sockaddr*)&c_addr, &len);

		if (client_sock != INVALID_SOCKET) {
			ioctlsocket(client_sock, FIONBIO, &non_blocking);
			int id = player_index++;
			auto session = std::make_shared<SESSION>();
			session->client_ = client_sock;
			session->id_ = id;
			clients[id] = session;
			std::cout << "New Client Connected! ID: " << id << "\n";
		}
		else {
			int err = WSAGetLastError();
			if (err != WSAEWOULDBLOCK)
			{
				error_display(L"Accept Error: ", err);
			}
		}

		for (auto it = clients.begin(); it != clients.end(); ) {
			auto& session = it->second;
			int r_ret = session->do_recv();

			if (r_ret > 0) {
				std::cout << "Received " << r_ret << " bytes from ID: " << session->id_ << "\n";

				if (nullptr == session)
				{
					client_disconnect(session->id_);
					continue;
				}
				unsigned char* p = reinterpret_cast<unsigned char*>(session->recv_buffer_);
				int data_size = r_ret + session->prev_recv_count_;
				while (data_size > 0)
				{
					unsigned char packet_size = p[0];
					if (packet_size > data_size)
					{
						break;
					}
					session->proccess_packet(p);
					p += packet_size;
					data_size -= packet_size;
				}
				if (data_size > 0)
				{
					memmove(session->recv_buffer_, p, data_size);
					session->prev_recv_count_ = data_size;
				}
				else
				{
					session->prev_recv_count_ = 0;
				}

				++it;
			}
			else if (r_ret == 0) {
				// 클라이언트가 정상 종료함
				std::cout << "Client Disconnected. ID: " << session->id_ << "\n";
				client_disconnect(session->id_);
				it = clients.erase(it);
			}
			else {
				int err = WSAGetLastError();
				if (err == WSAEWOULDBLOCK) {
					// 수신할 데이터가 없음: 그냥 다음 클라이언트로 넘어감
					++it;
				}
				else {
					// 진짜 에러 발생 (연결 강제 종료 등)
					std::cout << "Client Error. ID: " << session->id_ << "\n";
					client_disconnect(session->id_);
					it = clients.erase(it);
				}
			}
		}

		Sleep(1);
	}

	closesocket(server_socket);
	WSACleanup();
}
