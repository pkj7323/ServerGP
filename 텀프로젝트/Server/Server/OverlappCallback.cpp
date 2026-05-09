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
int player_index = 1;
SOCKET server_socket;
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
	int				id;
	io_type			type = io_type::count;
	WSABUF			wsabuffer;
	char			buff[BUF_SIZE];
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
	SOCKET			client_;
	int				id_;
	EXP_OVER		recv_over_;
	int				prev_recv_count_ = 0;
	client_state	state_;

	int16_t 		x_;
	int16_t 		y_;
	char			userName_[MAX_NAME_LEN];
	SESSION() : x_{ 0 }, y_{ 0 }, userName_{}
	{
		prev_recv_count_ = 0;
		state_ = client_state::connected;
		id_ = 999;
		client_ = INVALID_SOCKET;
		recv_over_.type = io_type::recv;
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
		ZeroMemory(&recv_over_.over, sizeof(WSAOVERLAPPED));
		recv_over_.type = io_type::recv;
		recv_over_.wsabuffer.buf = recv_over_.buff; // 본인의 버퍼 주소로 재설정
		recv_over_.wsabuffer.len = BUF_SIZE;
	}
	void do_recv()
	{
		recv_over_.id = id_;
		recv_over_.wsabuffer.buf = recv_over_.buff + prev_recv_count_; // 남은 데이터 뒤부터 이어 받음
		recv_over_.wsabuffer.len = BUF_SIZE - prev_recv_count_;
		DWORD recv_flag = 0;
		ZeroMemory(&recv_over_.over, sizeof(WSAOVERLAPPED));
		WSARecv(client_, &recv_over_.wsabuffer, 1, nullptr, &recv_flag, &recv_over_.over, recv_callback);
	}
	void do_send(int num_bytes, char* mess)
	{
		EXP_OVER* o = new EXP_OVER(io_type::send);
		o->id = id_;
		o->wsabuffer.len = num_bytes;
		memcpy(o->wsabuffer.buf, mess, num_bytes);
		WSASend(client_, &o->wsabuffer, 1, 0, 0, &o->over, nullptr);
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
	void send_move_packet(int mover, uint32_t timestamp);
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
		//std::cout << "Client[" << id_ << "] Login: " << userName_ << std::endl;
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

		//std::cout << "Player[" << id_ << "] moved to (" << x_ << ", " << y_ << ")\n";
		for (auto& [id, session] : clients)
		{
			if (nullptr == session) continue;
			if (client_state::playing == session->state_)
				session->send_move_packet(id_, packet->timestamp);
		}
		break;
	}

	default:
		std::cout << "Unknown Packet Type from Client[" << id_ << "]" << std::endl;
		break;
	}

}


void SESSION::send_move_packet(int move_player_id, uint32_t timestamp)
{
	std::shared_ptr<SESSION> player = clients[move_player_id];
	s2c_player_move move_packet;
	move_packet.size = sizeof(s2c_player_move);
	move_packet.type = packet_type::S2C_PLAYER_MOVE;
	move_packet.id = player->id_;
	move_packet.x = player->x_;
	move_packet.y = player->y_;
	move_packet.timestamp = timestamp;
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
			std::shared_ptr<SESSION> o = session;
			if (nullptr == o) continue;
			if (client_state::playing == o->state_)
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
	std::shared_ptr<SESSION> cl = clients[client_id];
	if (nullptr != cl) {
		cl->state_ = client_state::logout;
		broadcast_player_remove_packet(cl->id_);
		closesocket(cl->client_);
		cl->client_ = INVALID_SOCKET;
	}
	clients[client_id] = nullptr;
}

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags)
{
	EXP_OVER* o = reinterpret_cast<EXP_OVER*>(over);
	if (o->type == io_type::recv && num_bytes == 0 && WSAGetLastError() != WSA_IO_PENDING)
	{
		client_disconnect(o->id);
		return;
	}
	std::shared_ptr<SESSION>& session = clients[o->id];
	if (nullptr == session)
	{
		client_disconnect(o->id);
		return;
	}
	unsigned char* p = reinterpret_cast<unsigned char*>(o->buff);
	int data_size = num_bytes + session->prev_recv_count_;
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
		memmove(session->recv_over_.buff, p, data_size);
		session->prev_recv_count_ = data_size;
	}
	else
	{
		session->prev_recv_count_ = 0;
		ZeroMemory(o->buff, sizeof(o->buff));
	}

	session->do_recv();
	
	
}

void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags)
{
	EXP_OVER* o = reinterpret_cast<EXP_OVER*>(over);
	delete o;
	if (o->type == io_type::send && num_bytes == 0 && WSAGetLastError() != WSA_IO_PENDING)
	{
		client_disconnect(o->id);
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

	INT addr_len = sizeof(server_addr);
	SOCKADDR_IN cl_addr;
	for (int i = 1; ; ++i) {
		SOCKET client = WSAAccept(server_socket, reinterpret_cast<sockaddr*>(&cl_addr), &addr_len, NULL, NULL);

		if (client != INVALID_SOCKET) {
			//std::cout << "New Client Connected!" << std::endl;
			int current_id = player_index++;

			std::shared_ptr<SESSION> new_session = std::make_shared<SESSION>();
			new_session->id_ = current_id;
			new_session->client_ = client;
			new_session->state_ = client_state::connected;
			new_session->x_ = rand() % WORLD_WIDTH;
			new_session->y_ = rand() % WORLD_HEIGHT;
			clients.emplace(current_id, new_session);
			new_session->send_login_success();
			new_session->do_recv();
		}

		SleepEx(0, TRUE);
	}


	closesocket(server_socket);
	WSACleanup();
}
