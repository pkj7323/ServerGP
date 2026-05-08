#include <iostream>
#include <WS2tcpip.h>
#include <array>
#pragma comment(lib, "WS2_32.lib")
#include <MSWSock.h>
#pragma comment(lib, "MSWSock.lib")
#include <chrono>
#include <set>
#include <shared_mutex>
#include <string>
#include <vector>
#include <ctime>
#include <concurrent_priority_queue.h>

#include "Protocol.h"
#include <tbb/concurrent_unordered_map.h>
#include <unordered_set>
#include <valarray>

// NPC 구현 첫번째 방법
//  NPC클래스를 별도 제작, NPC컨테이너를 따로 생성한다.
//  장점 : 깔끔하다, 군더더기가 없다.
//  단점 : 플레이어와 NPC가 따로논다. 똑같은 역할을 수행하는 함수를 여러개씩 중복 작성해야 한다.
//         예) bool can_see(int from, int to)
//                 => bool can_see_p2p()
//				    bool can_see_p2n()
//					bool can_see_n2n()

// NPC 구현 두번째 방법  <===== 실습에서 사용할 방법.
//   clients 컨테이너에 NPC도 추가한다.
//   장점 : 플레이어와 NPC를 동일하게 취급할 수 있어서, 프로그래밍 작성 부하가 줄어든다.
//   단점 : 사용하지 않는 멤버들로 인한 메모리 낭비.

// NPC 구현 세번째 방법  (실제로 많이 사용되는 방법)
//   클래스 상속기능을 사용한다.
//     SESSION은 NPC클래스를 상속받아서 네트워크 관련 기능을 추가한 형태로 정의한다.
//       clients컨테이너를 objects컨테이너로 변경하고, 컨테이너는 NPC의 pointer를 저장한다.
//      장점 : 메모리 낭비가 없다, 함수의 중복작성이 필요없다.
//          (포인터로 관리되므로 player id의 중복사용 방지를 구현하기 쉬워진다 => Data Race 방지를 위한 추가 구현이 필요)
//      단점 : 포인터가 사용되고, reinterpret_cast가 필요하다. (별로 단점이 아니다).

class SESSION;

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
constexpr int VIEW_RANGE = 5;
constexpr int SECTOR_SIZE = 10;
constexpr int MOVE_COOL_TIME = 1000; // NPC 이동 쿨타임 (밀리초)
constexpr int MOVE_COOL_TIME_VARIATION = 100;

std::atomic<int> player_index = 1;
std::atomic<int> npc_index = 20000;

HANDLE h_iocp;
SOCKET server_socket;

enum class io_type
{
	send,
	recv,
	accept,
	npc_move,
	count, 
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
	EXP_OVER(char* packet)
	{
		wsabuffer.len = packet[0];
		wsabuffer.buf = buff;
		ZeroMemory(&over, sizeof(over));
		type = io_type::send;
		memcpy(buff, packet, packet[0]);
	}

};

// ID Type Flags (Bit-masking)
constexpr int ID_TYPE_PLAYER = 0x00000000; // Players start with 0
constexpr int ID_TYPE_NPC = 0x40000000; // NPCs have 30th bit set (starts from 1,073,741,824)
constexpr int ID_INDEX_MASK = 0x3FFFFFFF; // Lower 30 bits for per-type indexes

static int make_player_id(int index)
{
	return ID_TYPE_PLAYER | (index & ID_INDEX_MASK);
}

static int make_npc_id(int index)
{
	return ID_TYPE_NPC | (index & ID_INDEX_MASK);
}

static bool is_npc_id(int id)
{
	return (id & ~ID_INDEX_MASK) == ID_TYPE_NPC;
}

static bool is_player_id(int id)
{
	return (id & ~ID_INDEX_MASK) == ID_TYPE_PLAYER;
}



constexpr int EVENT_MOVE = -1;
struct event_type {
	int obj_id;
	std::chrono::time_point<std::chrono::system_clock> wakeup_time;
	int event_id;
	int target_id;
	constexpr bool operator < (const event_type& _Left) const
	{
		return (wakeup_time > _Left.wakeup_time);
	}
};
concurrency::concurrent_priority_queue<event_type> timer_queue;
struct cell {
	std::unordered_set<int> objects;
	std::shared_mutex mtx; // 섹터 전용 Read-Write Lock
};
class Sector
{
public:
	Sector() = default;

	void move_object(int object_id, int old_x, int old_y, int new_x, int new_y)
	{
		int old_sector_x = old_x / SECTOR_SIZE;
		int old_sector_y = old_y / SECTOR_SIZE;
		int new_sector_x = new_x / SECTOR_SIZE;
		int new_sector_y = new_y / SECTOR_SIZE;
		if (old_sector_x == new_sector_x && old_sector_y == new_sector_y)
		{
			return; // 같은 섹터 내 이동이므로 업데이트 필요 없음
		}
		// 1. 이전 섹터에서 삭제
		if (old_sector_x >= 0 && old_sector_x < WORLD_WIDTH / SECTOR_SIZE &&
			old_sector_y >= 0 && old_sector_y < WORLD_HEIGHT / SECTOR_SIZE)
		{
			std::unique_lock<std::shared_mutex> lock(object_sector[old_sector_y][old_sector_x].mtx);
			object_sector[old_sector_y][old_sector_x].objects.erase(object_id);
		}
		// 2. 새 섹터에 추가
		if (new_sector_x >= 0 && new_sector_x < WORLD_WIDTH / SECTOR_SIZE &&
			new_sector_y >= 0 && new_sector_y < WORLD_HEIGHT / SECTOR_SIZE)
		{
			std::unique_lock<std::shared_mutex> lock(object_sector[new_sector_y][new_sector_x].mtx);
			object_sector[new_sector_y][new_sector_x].objects.insert(object_id);
		}
	}

	void add_object(int object_id, int x, int y)
	{
		int sector_x = x / SECTOR_SIZE;
		int sector_y = y / SECTOR_SIZE;
		if (sector_x >= 0 && sector_x < WORLD_WIDTH / SECTOR_SIZE &&
			sector_y >= 0 && sector_y < WORLD_HEIGHT / SECTOR_SIZE)
		{
			std::unique_lock<std::shared_mutex> lock(object_sector[sector_y][sector_x].mtx);
			object_sector[sector_y][sector_x].objects.insert(object_id);
		}
	}

	void remove_object(int object_id, int x, int y)
	{
		int sector_x = x / SECTOR_SIZE;
		int sector_y = y / SECTOR_SIZE;
		if (sector_x >= 0 && sector_x < WORLD_WIDTH / SECTOR_SIZE &&
			sector_y >= 0 && sector_y < WORLD_HEIGHT / SECTOR_SIZE)
		{
			std::unique_lock<std::shared_mutex> lock(object_sector[sector_y][sector_x].mtx);
			object_sector[sector_y][sector_x].objects.erase(object_id);
		}
	}


	std::vector<int> get_objects_nearby_sector(int x, int y)
	{
		int sector_x = x / SECTOR_SIZE;
		int sector_y = y / SECTOR_SIZE;
		std::vector<int> nearby_objects;

		nearby_objects.reserve(AVERAGE_EXPECTED_OBJECTS);

		for (int dy = -1; dy <= 1; ++dy)
		{
			for (int dx = -1; dx <= 1; ++dx)
			{
				int check_sector_x = sector_x + dx;
				int check_sector_y = sector_y + dy;

				if (check_sector_x >= 0 && check_sector_x < WORLD_WIDTH / SECTOR_SIZE &&
					check_sector_y >= 0 && check_sector_y < WORLD_HEIGHT / SECTOR_SIZE)
				{
					std::shared_lock<std::shared_mutex> lock(object_sector[check_sector_y][check_sector_x].mtx);

					nearby_objects.insert(
						nearby_objects.end(),
						object_sector[check_sector_y][check_sector_x].objects.begin(),
						object_sector[check_sector_y][check_sector_x].objects.end()
					);
				}
			}
		}

		return nearby_objects;
	}

private:
	std::array<std::array<cell, WORLD_WIDTH / SECTOR_SIZE>, WORLD_HEIGHT / SECTOR_SIZE> object_sector;
	const int AVERAGE_EXPECTED_OBJECTS = 50;
};
Sector sector;

class BaseObject {
public:
	int				id_;
	int16_t 		x_;
	int16_t 		y_;
	char			userName_[MAX_NAME_LEN];

	BaseObject() : id_(-1), x_{ 0 }, y_{ 0 }, userName_{} {}
	virtual ~BaseObject() {}
};

class Player : public BaseObject {
public:
	Player() : BaseObject() {}
};




void broadcast_new_player(int new_player_id);
void broadcast_player_remove_packet(int player_id);
void client_disconnect(int client_id);

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

	std::shared_ptr<Player> player_;

	std::unordered_set<int> visible_players; // 현재 보이는 플레이어 ID 목록
	std::mutex visible_players_mutex; // 보이는 플레이어 목록 보호용 뮤텍스

	SESSION()
	{
		prev_recv_count_ = 0;
		state_ = client_state::connected;
		id_ = 999;
		client_ = INVALID_SOCKET;
		recv_over_.type = io_type::recv;
		player_ = std::make_shared<Player>();
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
		if (player_) {
			player_->id_ = -1;
			player_->x_ = 0;
			player_->y_ = 0;
			ZeroMemory(player_->userName_, sizeof(player_->userName_));
		}
		ZeroMemory(&recv_over_.over, sizeof(WSAOVERLAPPED));
		recv_over_.type = io_type::recv;
		recv_over_.wsabuffer.buf = recv_over_.buff; // 본인의 버퍼 주소로 재설정
		recv_over_.wsabuffer.len = BUF_SIZE;
	}
	void do_recv()
	{
		DWORD recv_flag = 0;
		recv_over_.over = {};
		recv_over_.wsabuffer.buf = recv_over_.buff + prev_recv_count_;
		recv_over_.wsabuffer.len = BUF_SIZE - prev_recv_count_;
		WSARecv(client_, &recv_over_.wsabuffer, 1, 0, &recv_flag, &recv_over_.over, nullptr);
	}
	void do_send(int num_bytes, char* mess)
	{
		EXP_OVER* o = new EXP_OVER(io_type::send);
		o->wsabuffer.len = num_bytes;
		memcpy(o->wsabuffer.buf, mess, num_bytes);
		WSASend(client_, &o->wsabuffer, 1, 0, 0, &o->over, nullptr);
	}

	bool proccess_packet(unsigned char* buff);


	void send_avatar_info()
	{
		S2C_AvatarInfo info_packet;
		info_packet.size = sizeof(S2C_AvatarInfo);
		info_packet.type = PACKET_TYPE::S2C_AVATAR_INFO;
		info_packet.playerId = id_;
		info_packet.x = player_->x_;
		info_packet.y = player_->y_;
		do_send(sizeof(S2C_AvatarInfo), reinterpret_cast<char*>(&info_packet));
	}


	void send_login_success()
	{
		S2C_LoginResult ack_packet;
		ack_packet.size = sizeof(S2C_LoginResult);
		ack_packet.type = PACKET_TYPE::S2C_LOGIN_RESULT;
		ack_packet.success = true;
		strcpy_s(ack_packet.message, "Login successful.");
		do_send(ack_packet.size, reinterpret_cast<char*>(&ack_packet));
	}
	void send_remove_player(int player_id)
	{
		S2C_RemovePlayer packet;
		packet.size = sizeof(S2C_RemovePlayer);
		packet.type = PACKET_TYPE::S2C_REMOVE_PLAYER;
		packet.playerId = player_id;
		visible_players_mutex.lock();
		if (!visible_players.contains(player_id))
		{
			visible_players_mutex.unlock();
			return;
		}
		visible_players.erase(player_id);
		visible_players_mutex.unlock();
		do_send(packet.size, reinterpret_cast<char*>(&packet));
	}
	void send_move_packet(int mover, uint32_t timestamp);
	void send_add_player(int player_id);
	bool is_visible(int16_t x, int16_t y)
	{
		return abs(x - player_->x_) <= VIEW_RANGE && abs(y - player_->y_) <= VIEW_RANGE;
	}
	void send_already_spawn_players();
};

tbb::concurrent_unordered_map<int, std::atomic<std::shared_ptr<SESSION>>> clients;

class NPC : public BaseObject {
public:
	NPC() : BaseObject(), last_move_timestamp_(std::chrono::system_clock::now()) {}
	std::chrono::time_point<std::chrono::system_clock> last_move_timestamp_; // NPC의 마지막 이동 시간 기록
	void heartbeat()
	{
		do_random_move();
	}
	void do_timer_move()
	{
		do_random_move();
		if (id_ == make_npc_id(20000))
		{
			auto delay = std::chrono::system_clock::now() - last_move_timestamp_;
			std::cout << "NPC " << id_ << " moved. Time since last move: " << std::chrono::duration_cast<std::chrono::milliseconds>(delay).count() << " ms\n";
		}

		last_move_timestamp_ = std::chrono::system_clock::now();
	}
	void do_random_move()
	{
		std::unordered_set<int> old_vl;
		for (auto id : sector.get_objects_nearby_sector(x_, y_))
		{
			if (is_npc_id(id)) continue; // NPCs don't need to be tracked in visible_players for this example
			std::shared_ptr<SESSION> s = clients[id].load();
			if (!s || s->state_ != client_state::playing) continue;
			old_vl.insert(id);
		}

		int old_x = x_;
		int old_y = y_;
		switch (rand() % 4) {
		case 0: if (x_ < (WORLD_WIDTH - 1)) x_++; break;
		case 1: if (x_ > 0) x_--; break;
		case 2: if (y_ < (WORLD_HEIGHT - 1)) y_++; break;
		case 3:if (y_ > 0) y_--; break;
		}

		sector.move_object(id_, old_x, old_y, x_, y_);
		std::unordered_set<int> new_vl;
		for (auto id : sector.get_objects_nearby_sector(x_, y_))
		{
			if (is_npc_id(id)) continue; // NPCs don't need to be tracked in visible_players for this example
			std::shared_ptr<SESSION> s = clients[id].load();
			if (!s || s->state_ != client_state::playing) continue;
			new_vl.insert(id);
		}

		for (auto& id : new_vl)
		{
			if (!old_vl.contains(id))
			{
				std::shared_ptr<SESSION> s = clients[id].load();
				if (s) s->send_add_player(id_);
			}
			else
			{
				std::shared_ptr<SESSION> s = clients[id].load();
				if (s && s->state_ == client_state::playing)
				{
					s->send_move_packet(id_, MOVE_COOL_TIME);
				}
			}
		}
		for (auto& id : old_vl)
		{
			if (!new_vl.contains(id))
			{
				std::shared_ptr<SESSION> s = clients[id].load();
				if (s) s->send_remove_player(id_);
			}
		}

	}
};

tbb::concurrent_unordered_map<int, std::atomic<std::shared_ptr<NPC>>> npcs;


bool can_see(int from_id, int to_id)
{
	int16_t from_x, from_y, to_x, to_y;

	if (is_npc_id(from_id))
	{
		auto from_npc = npcs[from_id].load();
		from_x = from_npc->x_;
		from_y = from_npc->y_;
	}
	else
	{
		auto from_session = clients[from_id].load();
		if (!from_session || from_session->state_ != client_state::playing) return false;
		from_x = from_session->player_->x_;
		from_y = from_session->player_->y_;
	}

	if (is_npc_id(to_id))
	{
		auto to_npc = npcs[to_id].load();
		to_x = to_npc->x_;
		to_y = to_npc->y_;
	}
	else
	{
		auto to_session = clients[to_id].load();
		if (!to_session || to_session->state_ != client_state::playing) return false;
		to_x = to_session->player_->x_;
		to_y = to_session->player_->y_;
	}

	if (std::abs(from_x - to_x) > VIEW_RANGE)
	{
		return false;
	}
	return std::abs(from_y - to_y) <= VIEW_RANGE;
}

bool SESSION::proccess_packet(unsigned char* buff)
{
	PACKET_TYPE type = *reinterpret_cast<PACKET_TYPE*>(&buff[1]);
	switch (type)
	{
	case PACKET_TYPE::C2S_LOGIN:
	{
		C2S_Login* p = reinterpret_cast<C2S_Login*>(buff);
		strncpy_s(player_->userName_, p->username, MAX_NAME_LEN);
		state_ = client_state::playing;
		sector.add_object(id_, player_->x_, player_->y_); // 섹터에 플레이어 추가
		send_avatar_info();
		send_already_spawn_players();
		broadcast_new_player(id_);
		break;
	}
	case PACKET_TYPE::C2S_MOVE:
	{
		C2S_Move* packet = reinterpret_cast<C2S_Move*>(buff);
		int16_t dx = 0;
		int16_t dy = 0;
		switch (packet->dir)
		{
		case DIRECTION::UP:    dy = -1; break;
		case DIRECTION::DOWN:  dy = 1;  break;
		case DIRECTION::LEFT:  dx = -1; break;
		case DIRECTION::RIGHT: dx = 1;  break;
		}

		int16_t old_x = player_->x_;
		int16_t old_y = player_->y_;
		int16_t new_x = player_->x_ + dx;
		int16_t new_y = player_->y_ + dy;
		if (new_x >= 0 && new_x < WORLD_WIDTH)
		{
			player_->x_ = new_x;
		}
		if (new_y >= 0 && new_y < WORLD_HEIGHT)
		{
			player_->y_ = new_y;
		}
		sector.move_object(id_, old_x, old_y, player_->x_, player_->y_); // 섹터 정보 업데이트

		auto old_view = visible_players;

		std::unordered_set<int> new_visible_players;
		auto object_ids_nearby_sector = sector.get_objects_nearby_sector(player_->x_, player_->y_);
		for (auto& id : object_ids_nearby_sector)
		{
			if (id == id_) continue;

			// NPCs check
			if (is_npc_id(id)) {
				auto npc = npcs[id].load();
				if (is_visible(npc->x_, npc->y_)) {
					new_visible_players.insert(id);
				}
				continue;
			}

			// Players check
			auto it = clients.find(id);
			if (it == clients.end()) continue;
			std::shared_ptr<SESSION> s = it->second.load();
			if (!s || s->state_ != client_state::playing) continue;

			if (s->is_visible(player_->x_, player_->y_))
			{
				new_visible_players.insert(id);
			}
		}
		send_move_packet(id_, packet->move_time);

		for (auto& id : new_visible_players)
		{
			if (!old_view.contains(id))
			{
				if (npcs.contains(id)) {
					auto npc = npcs[id].load();
					S2C_AddPlayer add_pkt;
					add_pkt.size = sizeof(add_pkt);
					add_pkt.type = PACKET_TYPE::S2C_ADD_PLAYER;
					add_pkt.playerId = id;
					add_pkt.x = npc->x_;
					add_pkt.y = npc->y_;
					strcpy_s(add_pkt.username, npc->userName_);
					do_send(add_pkt.size, reinterpret_cast<char*>(&add_pkt));

					visible_players_mutex.lock();
					visible_players.insert(id);
					visible_players_mutex.unlock();
				}
				else {
					send_add_player(id);
					std::shared_ptr<SESSION> s = clients[id].load();
					if (s) s->send_add_player(id_);
				}
			}
			else
			{
				if (!npcs.contains(id)) {
					std::shared_ptr<SESSION> s = clients[id].load();
					if (s && s->state_ == client_state::playing)
					{
						s->send_move_packet(id_, packet->move_time);
					}
				}
			}
		}

		for (auto& id : old_view)
		{
			if (!new_visible_players.contains(id))
			{
				if (npcs.contains(id)) {
					S2C_RemovePlayer remove_pkt;
					remove_pkt.size = sizeof(remove_pkt);
					remove_pkt.type = PACKET_TYPE::S2C_REMOVE_PLAYER;
					remove_pkt.playerId = id;
					do_send(remove_pkt.size, reinterpret_cast<char*>(&remove_pkt));

					visible_players_mutex.lock();
					visible_players.erase(id);
					visible_players_mutex.unlock();
				}
				else {
					send_remove_player(id);
					std::shared_ptr<SESSION> s = clients[id].load();
					if (s) s->send_remove_player(id_);
				}
			}
		}
		break;
	}

	default:
		std::cout << "Unknown Packet Type from Client[" << id_ << "]" << std::endl;
		return false;
		break;
	}
	return true;

}
void SESSION::send_move_packet(int move_player_id, uint32_t timestamp)
{
	if (is_npc_id(move_player_id)) {
		auto npc = npcs[move_player_id].load();
		if (!npc) return;
		S2C_MovePlayer move_packet;
		move_packet.size = sizeof(S2C_MovePlayer);
		move_packet.type = PACKET_TYPE::S2C_MOVE_PLAYER;
		move_packet.playerId = move_player_id;
		move_packet.x = npc->x_;
		move_packet.y = npc->y_;
		move_packet.move_time = timestamp;
		do_send(move_packet.size, reinterpret_cast<char*>(&move_packet));
		return;
	}

	std::shared_ptr<SESSION> session = clients[move_player_id].load();
	if (!session || !session->player_) return;

	S2C_MovePlayer move_packet;
	move_packet.size = sizeof(S2C_MovePlayer);
	move_packet.type = PACKET_TYPE::S2C_MOVE_PLAYER;
	move_packet.playerId = move_player_id;
	move_packet.x = session->player_->x_;
	move_packet.y = session->player_->y_;
	move_packet.move_time = timestamp;
	do_send(move_packet.size, reinterpret_cast<char*>(&move_packet));
}
void SESSION::send_add_player(int player_id)
{
	std::shared_ptr<SESSION> session = clients[player_id].load();
	if (!session || !session->player_) return;

	S2C_AddPlayer add_packet;
	add_packet.size = sizeof(S2C_AddPlayer);
	add_packet.type = PACKET_TYPE::S2C_ADD_PLAYER;
	add_packet.playerId = player_id;
	strncpy_s(add_packet.username, session->player_->userName_, MAX_NAME_LEN);
	add_packet.x = session->player_->x_;
	add_packet.y = session->player_->y_;

	visible_players_mutex.lock();
	if (visible_players.contains(player_id))
	{
		visible_players_mutex.unlock();
		return;
	}
	visible_players.insert(player_id);
	visible_players_mutex.unlock();
	do_send(sizeof(S2C_AddPlayer), reinterpret_cast<char*>(&add_packet));
}
void SESSION::send_already_spawn_players()
{
	auto object_ids_nearby_sector = sector.get_objects_nearby_sector(player_->x_, player_->y_);
	for (auto& id : object_ids_nearby_sector)
	{
		if (id != this->id_)
		{
			if (npcs.count(id)) {
				auto npc = npcs[id].load();
				if (is_visible(npc->x_, npc->y_)) {
					S2C_AddPlayer add_pkt;
					add_pkt.size = sizeof(add_pkt);
					add_pkt.type = PACKET_TYPE::S2C_ADD_PLAYER;
					add_pkt.playerId = id;
					add_pkt.x = npc->x_;
					add_pkt.y = npc->y_;
					strcpy_s(add_pkt.username, npc->userName_);
					do_send(add_pkt.size, reinterpret_cast<char*>(&add_pkt));

					visible_players_mutex.lock();
					visible_players.insert(id);
					visible_players_mutex.unlock();
				}
				continue;
			}

			std::shared_ptr<SESSION> o = clients[id].load();
			if (nullptr == o || !o->player_) continue;
			if (false == is_visible(o->player_->x_, o->player_->y_)) continue;
			if (client_state::playing == o->state_)
				send_add_player(id);
		}
	}
}




void broadcast_new_player(int new_player_id)
{
	auto new_player_session = clients[new_player_id].load();
	if (!new_player_session || !new_player_session->player_) return;

	auto object_ids_nearby_sector = sector.get_objects_nearby_sector(new_player_session->player_->x_, new_player_session->player_->y_);
	for (auto& id : object_ids_nearby_sector) {
		if (id != new_player_id && is_player_id(id)) {
			std::shared_ptr<SESSION> o = clients[id].load();
			if (nullptr == o || !o->player_) continue;
			if (false == o->is_visible(new_player_session->player_->x_, new_player_session->player_->y_)) continue;
			if (client_state::playing == o->state_)
				o->send_add_player(new_player_id);
		}
	}
}
void broadcast_player_remove_packet(int player_id)
{
	auto removed_player = clients[player_id].load();
	if (!removed_player) return;

	for (auto& id : removed_player->visible_players)
	{
		if (id != player_id && !npcs.count(id))
		{
			std::shared_ptr<SESSION> o = clients[id].load();
			if (nullptr == o) continue;
			if (client_state::playing == o->state_)
				o->send_remove_player(player_id);
		}
	}
}
void send_login_fail(SOCKET client, const char* text)
{
	S2C_LoginResult ack_packet;
	ack_packet.size = sizeof(S2C_LoginResult);
	ack_packet.type = PACKET_TYPE::S2C_LOGIN_RESULT;
	ack_packet.success = false;
	strncpy_s(ack_packet.message, text, sizeof(ack_packet.message));
	WSABUF wsabuf;
	wsabuf.buf = reinterpret_cast<char*>(&ack_packet);
	wsabuf.len = sizeof(S2C_LoginResult);
	WSASend(client, &wsabuf, 1, 0, 0, nullptr, nullptr);
}
void client_disconnect(int client_id)
{
	std::cout << "client[" << client_id << "] Disconnected.\n";
	std::shared_ptr<SESSION> cl = clients[client_id].load();
	if (nullptr != cl && cl->player_) {
		cl->state_ = client_state::logout;
		broadcast_player_remove_packet(cl->id_);
		sector.remove_object(cl->id_, cl->player_->x_, cl->player_->y_);
		closesocket(cl->client_);
		cl->client_ = INVALID_SOCKET;
	}
	clients[client_id].store(nullptr);
}
void npc_initialize()
{

	for (int i = 0; i < MAX_NPCS; ++i)
	{
		int npc_id = make_npc_id(npc_index++);
		auto npc = std::make_shared<NPC>();
		npc->id_ = npc_id;
		npc->x_ = rand() % WORLD_WIDTH;
		npc->y_ = rand() % WORLD_HEIGHT;
		std::string npc_name = "NPC_" + std::to_string(npc_id);
		strcpy_s(npc->userName_, npc_name.c_str());
		npcs[npc_id] = npc;
		sector.add_object(npc_id, npc->x_, npc->y_); // 섹터에 NPC 추가
		timer_queue.push({ npc_id, 
			std::chrono::system_clock::now()
			+ std::chrono::milliseconds(MOVE_COOL_TIME)
			+ std::chrono::milliseconds(rand() % MOVE_COOL_TIME_VARIATION),
			EVENT_MOVE,
			-1 });
	}
}

void ai_thread()
{
	using namespace std::chrono;
	while (true)
	{
		auto current_time = system_clock::now();
		int elapsed_time = 1000;
		for (auto& [npc_id, atomic_npc] : npcs)
		{
			auto npc = atomic_npc.load();
			if (!npc) continue;
			auto duration = duration_cast<milliseconds>(current_time - npc->last_move_timestamp_).count();
			if (duration >= MOVE_COOL_TIME)
			{
				npc->heartbeat();
				npc->last_move_timestamp_ = current_time;
				if (duration > elapsed_time)
				{
					elapsed_time++;
				}
				else
					elapsed_time--;

			}
		}
		std::cout << "AI Thread: NPCs moved. Elapsed Time: " << elapsed_time << " ms\n";
		auto end_time = system_clock::now();
		auto elapsed = duration_cast<milliseconds>(end_time - current_time).count();

		for (auto& [npc_id, atomic_npc] : npcs)
		{
			auto npc = atomic_npc.load();
			if (!npc) continue;
			auto nearby_players = sector.get_objects_nearby_sector(npc->x_, npc->y_);
			for (auto& player_id : nearby_players)
			{
				if (npcs.count(player_id)) continue; // NPC는 패킷을 받지 않음
				std::shared_ptr<SESSION> session = clients[player_id].load();
				if (!session || session->state_ != client_state::playing) continue;
				if (session->is_visible(npc->x_, npc->y_))
				{
					S2C_MovePlayer move_packet;
					move_packet.size = sizeof(move_packet);
					move_packet.type = PACKET_TYPE::S2C_MOVE_PLAYER;
					move_packet.playerId = npc_id;
					move_packet.x = npc->x_;
					move_packet.y = npc->y_;
					session->do_send(sizeof(move_packet), reinterpret_cast<char*>(&move_packet));
				}
			}
		}
		if (elapsed < 5)
		{
			std::this_thread::sleep_for(milliseconds(10)); // AI 스레드의 CPU 사용량을 줄이기 위해 잠시 대기
		}
	}
}

void HeartBeat_thread()
{
	using namespace std::chrono;

	while (true) {
		auto start_time = system_clock::now();
		for (auto& [id, atomic_npc] : npcs) {
			auto npc = atomic_npc.load();
			if (npc) {
				npc->heartbeat();
			}
		}
		auto end_time = system_clock::now();
		auto elapsed = end_time - start_time;
		if (elapsed < milliseconds(MOVE_COOL_TIME)) {
			std::this_thread::sleep_for(milliseconds(MOVE_COOL_TIME) - elapsed);
		}

		std::cout << "Elapsed Time : "
			<< duration_cast<milliseconds>(elapsed).count()
			<< "ms.\n";
	}
}

void timer_thread()
{
	using namespace std::chrono;
	long long elasped_time = 1000;
	auto last_send_time = system_clock::now();
	while (true)
	{
		event_type ev;
		if (timer_queue.try_pop(ev))
		{
			auto now = system_clock::now();
			if (now >= ev.wakeup_time)
			{
				switch (ev.event_id)
				{
				case EVENT_MOVE:
					{
						auto npc = npcs[ev.obj_id].load();
						if (npc) npc->do_random_move();
						ev.wakeup_time = system_clock::now() + milliseconds(MOVE_COOL_TIME);
						timer_queue.push(ev);
					}
					break;
				default:
					std::cout << "Unknown Event Type in Timer Thread!" << std::endl;
					break;
				}
			}
			else
			{
				timer_queue.push(ev); // 아직 실행 시간이 안된 이벤트는 다시 큐에 넣음
				std::this_thread::sleep_for(milliseconds(1)); // CPU 사용량을 줄이기 위해 잠시 대기
			}
		}
		std::this_thread::sleep_for(milliseconds(1)); // 이벤트가 없을 때 CPU 사용량을 줄이기 위해 잠시 대기
	}

}

void timer_thread2()
{
	using namespace std::chrono;
	while (true)
	{
		event_type ev;
		if (timer_queue.try_pop(ev))
		{
			auto now = system_clock::now();
			if (now >= ev.wakeup_time)
			{
				switch (ev.event_id)
				{
				case EVENT_MOVE:
					{
						EXP_OVER* move_over = new EXP_OVER;
						move_over->type = io_type::npc_move;
						PostQueuedCompletionStatus(h_iocp, -1, ev.obj_id, &move_over->over);
					}
					break;
				default:
					std::cout << "Unknown Event Type in Timer Thread!" << std::endl;
					break;
				}
			}
			else
			{
				timer_queue.push(ev); // 아직 실행 시간이 안된 이벤트는 다시 큐에 넣음
				std::this_thread::sleep_for(milliseconds(1)); // CPU 사용량을 줄이기 위해 잠시 대기
			}
		}
		
	}
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
			int current_id = make_player_id(player_index++);
			CreateIoCompletionPort((HANDLE)o->accept_socket, h_iocp, current_id, 0);
			std::shared_ptr<SESSION> new_session = std::make_shared<SESSION>();
			new_session->id_ = current_id;
			new_session->client_ = o->accept_socket;
			new_session->state_ = client_state::connected;
			if (new_session->player_) {
				new_session->player_->id_ = current_id;
				new_session->player_->x_ = rand() % WORLD_WIDTH;
				new_session->player_->y_ = rand() % WORLD_HEIGHT;
			}
			clients.emplace(current_id, new_session);
			new_session->send_login_success();
			new_session->do_recv();


			o->accept_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			ZeroMemory(&o->over, sizeof(o->over));
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
			int data_size = num_bytes + cl->prev_recv_count_;
			bool packet_valid = true;
			while (data_size > 0)
			{
				unsigned char packet_size = p[0];
				if (packet_size > data_size)
				{
					break;
				}
				packet_valid = cl->proccess_packet(p);
				if (!packet_valid)
				{
					client_disconnect(id);
					break;
				}
				p += packet_size;
				data_size -= packet_size;
			}
			if (!packet_valid)
			{
				break;
			}
			if (data_size > 0)
			{
				memmove(cl->recv_over_.buff, p, data_size);
				cl->prev_recv_count_ = data_size;
			}
			else
			{
				cl->prev_recv_count_ = 0;
				ZeroMemory(o->buff, sizeof(o->buff));
			}

			cl->do_recv();
			break;
		}
		case io_type::send:
		{
			delete o;
			break;
		}
		case io_type::npc_move:
		{
			auto npc = npcs[id].load();
			if (npc)
			{
				npc->do_timer_move();
				event_type ev;
				ev.obj_id = id;
				ev.event_id = EVENT_MOVE;
				ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::milliseconds(MOVE_COOL_TIME) + std::chrono::milliseconds(rand() % MOVE_COOL_TIME_VARIATION);
				ev.target_id = -1;
				timer_queue.push(ev);
			}
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
	srand(static_cast<unsigned int>(time(NULL)));
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

	std::cout << "npc initializing..." << std::endl;
	npc_initialize();
	std::cout << "npc initialization complete. NPC Count: " << npcs.size() << std::endl;

	EXP_OVER accept_over(io_type::accept);
	accept_over.accept_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	AcceptEx(server_socket, accept_over.accept_socket, &accept_over.buff, 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, NULL, &accept_over.over);

	std::thread ai_thread_handle(timer_thread2);

	std::vector<std::thread> worker_threads;
	for (unsigned int i = 0; i < std::thread::hardware_concurrency(); ++i)
	{
		worker_threads.emplace_back(worker_thread);
	}
	for (auto& t : worker_threads)
	{
		t.join();
	}
	ai_thread_handle.join();

	closesocket(server_socket);
	WSACleanup();
}
