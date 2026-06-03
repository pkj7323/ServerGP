#define NOMINMAX
#include <algorithm>
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
#include <queue>

#include "Protocol.h"
#include "npc_loader.h"
#include <tbb/concurrent_unordered_map.h>
#include <unordered_set>
#include <valarray>

class SESSION;
enum NpcType : uint8_t {
	NPC_NONE = 0, // 빈 값
	NPC_ZOMBIE = 1,
	NPC_SKELETON = 2,
	NPC_CREEPER = 3,
	NPC_ENDERMAN = 4,
	NPC_IRON_GOLEM = 5,
};

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
constexpr int MOVE_COOL_TIME_VARIATION = 1000;

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
	npc_respawn,
	count,
};

class EXP_OVER {
public:
	WSAOVERLAPPED	over;
	io_type			type = io_type::count;
	WSABUF			wsabuffer;
	char			buff[BUF_SIZE];
	SOCKET			accept_socket;

	// 배칭용 멤버 추가
	std::vector<WSABUF> wsa_bufs;
	std::vector<std::vector<char>> pending_data;

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
	// 배칭용 생성자
	EXP_OVER(std::vector<std::vector<char>>&& data) : type(io_type::send), pending_data(std::move(data))
	{
		ZeroMemory(&over, sizeof(over));
		wsa_bufs.reserve(pending_data.size());
		for (auto& d : pending_data) {
			wsa_bufs.push_back({ static_cast<ULONG>(d.size()), d.data() });
		}
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
constexpr int EVENT_RESPAWN = -2;
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
ArmorConfig g_armor_config;
PlayerConfig g_player_config;

std::vector<uint8_t> g_server_collision;

bool is_passable(int x, int y) {
	if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT) return false;
	if (g_server_collision.empty()) return true; // fail-safe if map isn't loaded
	return g_server_collision[y * WORLD_WIDTH + x] == 0;
}

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
	int _armor = 0;
	char _weapon_tier = 1;
	short _dir_x = 0;
	short _dir_y = -1;
	int _hp = 100;
	int _maxHp = 100;

	// Inventory
	int _gold = 0;
	std::unordered_map<int, int> inventory; // item_id -> count

	std::chrono::time_point<std::chrono::system_clock> last_move_timestamp_;
	std::chrono::time_point<std::chrono::system_clock> last_attack_time_;
	std::chrono::time_point<std::chrono::system_clock> potion_cooldown_end_time_;
	std::chrono::time_point<std::chrono::system_clock> skill_cooldown_end_time_;
	std::chrono::time_point<std::chrono::system_clock> buff_end_time_;

	Player() : BaseObject(), last_move_timestamp_(std::chrono::system_clock::now()),
			   last_attack_time_(std::chrono::system_clock::now()),
			   potion_cooldown_end_time_(std::chrono::system_clock::now()),
			   skill_cooldown_end_time_(std::chrono::system_clock::now()),
			   buff_end_time_(std::chrono::system_clock::now()) {}

	void add_item(int item_id, int count) {
		inventory[item_id] += count;
	}

	char get_armor_tier() const {
		if (_armor >= g_armor_config.netherite) return 4;
		if (_armor >= g_armor_config.diamond) return 3;
		if (_armor >= g_armor_config.iron) return 2;
		if (_armor >= g_armor_config.copper) return 1;
		return 0;
	}
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

	// 송신 배칭용 멤버 추가
	std::mutex send_mtx;
	std::vector<std::vector<char>> send_queue;
	std::atomic<bool> is_sending{ false };

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

		std::lock_guard<std::mutex> lock(send_mtx);
		send_queue.clear();
		is_sending = false;
	}
	void do_recv()
	{
		DWORD recv_flag = 0;
		recv_over_.over = {};
		recv_over_.wsabuffer.buf = recv_over_.buff + prev_recv_count_;
		recv_over_.wsabuffer.len = BUF_SIZE - prev_recv_count_;
		WSARecv(client_, &recv_over_.wsabuffer, 1, 0, &recv_flag, &recv_over_.over, nullptr);
	}
	void do_send(int size, char* packet)
	{
		{
			std::lock_guard<std::mutex> lock(send_mtx);
			send_queue.emplace_back(packet, packet + size);
		}

		bool expected = false;
		if (is_sending.compare_exchange_strong(expected, true)) {
			flush_send();
		}
	}

	void flush_send()
	{
		std::vector<std::vector<char>> sending_data;
		{
			std::lock_guard<std::mutex> lock(send_mtx);
			if (send_queue.empty()) {
				is_sending = false;
				return;
			}
			sending_data.swap(send_queue);
		}

		EXP_OVER* send_over = new EXP_OVER(std::move(sending_data));
		DWORD send_bytes;
		int ret = WSASend(client_, send_over->wsa_bufs.data(), static_cast<DWORD>(send_over->wsa_bufs.size()), &send_bytes, 0, &send_over->over, NULL);

		if (ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
			delete send_over;
			std::lock_guard<std::mutex> lock(send_mtx);
			is_sending = false;
			// 에러 발생 시 클라이언트 연결 종료 고려
			// client_disconnect(id_); 
		}
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
		info_packet.armor_tier = player_->get_armor_tier();
		info_packet.weapon_tier = player_->_weapon_tier;
		info_packet.dir_x = player_->_dir_x;
		info_packet.dir_y = player_->_dir_y;
		info_packet.hp = 100;
		info_packet.max_hp = 100;
		info_packet.exp = 0;
		info_packet.level = 1;
		do_send(sizeof(S2C_AvatarInfo), reinterpret_cast<char*>(&info_packet));
	}

	void send_inventory_sync()
	{
		S2C_InventorySync pkt;
		pkt.size = sizeof(S2C_InventorySync);
		pkt.type = PACKET_TYPE::S2C_INVENTORY_SYNC;
		pkt.gold = player_->_gold;
		
		int count = 0;
		for (auto& [item_id, amt] : player_->inventory) {
			if (amt > 0 && count < MAX_INVENTORY_SLOTS) {
				pkt.items[count].item_id = item_id;
				pkt.items[count].count = amt;
				count++;
			}
		}
		pkt.item_count = count;
		do_send(sizeof(S2C_InventorySync), reinterpret_cast<char*>(&pkt));
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
		S2C_RemoveObject packet;
		packet.size = sizeof(S2C_RemoveObject);
		packet.type = PACKET_TYPE::S2C_REMOVE_OBJECT;
		packet.object_id = player_id;
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
	std::atomic<bool> is_active{ false };

	// [SpawnMap] 스폰 데이터에서 로드되는 NPC 고유 정보
	NpcType            _npcType  = NPC_NONE;
	int                _visualId = 0;
	int                _hp       = 0;
	int                _maxHp    = 0;
	unsigned char      _level    = 1;
	unsigned long long _exp      = 0;
	int                _attack   = 0;
	int                _dropItem = 0;
	int                _dropGold = 0;
public:
	NPC() : BaseObject(), last_move_timestamp_(std::chrono::system_clock::now()), last_hit_time_(std::chrono::system_clock::now()) {}
	std::chrono::time_point<std::chrono::system_clock> last_move_timestamp_; // NPC의 마지막 이동 시간 기록
	std::chrono::time_point<std::chrono::system_clock> last_hit_time_;       // 마지막 피격 시간
	int16_t initial_x_ = 0;
	int16_t initial_y_ = 0;
	void heartbeat()
	{
		do_random_move();
	}
	bool do_timer_move()
	{
		auto now = std::chrono::system_clock::now();
		if (_hp < _maxHp && std::chrono::duration_cast<std::chrono::seconds>(now - last_hit_time_).count() >= 5) {
			_hp += _maxHp / 10;
			_hp = std::min(_hp, _maxHp);

			S2C_ChatMessage chat_pkt;
			chat_pkt.size = sizeof(chat_pkt);
			chat_pkt.type = S2C_CHAT_MESSAGE;
			chat_pkt.object_id = id_;
			std::string msg = "회복! 체력: " + std::to_string(_hp);
			strncpy_s(chat_pkt.message, msg.c_str(), sizeof(chat_pkt.message));
			
			for (auto pid : sector.get_objects_nearby_sector(x_, y_)) {
				if (!is_npc_id(pid)) {
					std::shared_ptr<SESSION> s = clients[pid].load();
					if (s && s->state_ == client_state::playing) {
						s->do_send(chat_pkt.size, reinterpret_cast<char*>(&chat_pkt));
					}
				}
			}
		}

		bool has_player_nearby = do_random_move();
		/*if (id_ == make_npc_id(20000))
		{
			auto delay = std::chrono::system_clock::now() - last_move_timestamp_;
			std::cout << "NPC " << id_ << " moved. Time since last move: " << std::chrono::duration_cast<std::chrono::milliseconds>(delay).count() << " ms\n";
		}*/

		last_move_timestamp_ = std::chrono::system_clock::now();
		return has_player_nearby;
	}
	bool do_random_move()
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

		// 10% 확률로 주변 플레이어에게 메시지 보내기
		if (!new_vl.empty() && (rand() % 10) == 0) {
			S2C_ChatMessage chat_pkt;
			chat_pkt.size = sizeof(chat_pkt);
			chat_pkt.type = S2C_CHAT_MESSAGE;
			chat_pkt.object_id = id_;
			
			const char* msgs[] = {
				"크르르르...",
				"누구냐!",
				"가까이 오지마라!",
				"침입자 발견!"
			};
			strcpy_s(chat_pkt.message, msgs[rand() % 4]);
			
			for (auto& player_id : new_vl) {
				std::shared_ptr<SESSION> s = clients[player_id].load();
				if (s && s->state_ == client_state::playing) {
					s->do_send(chat_pkt.size, reinterpret_cast<char*>(&chat_pkt));
				}
			}
		}

		return !new_vl.empty();
	}
	void wake_up()
	{
		bool expected = false;
		if (!is_active.compare_exchange_strong(expected, true))
		{
			return;
		}

		event_type ev;
		ev.obj_id = id_;
		ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::milliseconds(MOVE_COOL_TIME + (rand() % MOVE_COOL_TIME_VARIATION));
		ev.event_id = EVENT_MOVE;
		ev.target_id = -1;
		timer_queue.push(ev);
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
		auto now = std::chrono::system_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - player_->last_move_timestamp_).count();
		
		if (elapsed < g_player_config.move_cooldown_ms) {
			// 이동 무시하고 현재 서버 기준 올바른 좌표를 클라로 다시 보내서 위치 보정(고무줄 현상 처리)
			S2C_MoveObject res_pkt;
			res_pkt.size = sizeof(res_pkt);
			res_pkt.type = S2C_MOVE_OBJECT;
			res_pkt.object_id = id_;
			res_pkt.x = player_->x_;
			res_pkt.y = player_->y_;
			res_pkt.dir_x = player_->_dir_x;
			res_pkt.dir_y = player_->_dir_y;
			res_pkt.move_time = 0;
			do_send(res_pkt.size, reinterpret_cast<char*>(&res_pkt));
			break;
		}
		player_->last_move_timestamp_ = now;

		C2S_Move* packet = reinterpret_cast<C2S_Move*>(buff);
		int16_t dx = packet->x;
		int16_t dy = packet->y;

		int16_t old_x = player_->x_;
		int16_t old_y = player_->y_;
		int16_t new_x = player_->x_ + dx;
		int16_t new_y = player_->y_ + dy;
		
		if (dx != 0 || dy != 0) {
			player_->_dir_x = dx;
			player_->_dir_y = dy;
		}

		if (is_passable(new_x, new_y))
		{
			player_->x_ = new_x;
			player_->y_ = new_y;
		}
		else
		{
			// 충돌: 강제 위치 복원(고무줄 현상)
			S2C_MoveObject res_pkt;
			res_pkt.size = sizeof(res_pkt);
			res_pkt.type = S2C_MOVE_OBJECT;
			res_pkt.object_id = id_;
			res_pkt.x = player_->x_;
			res_pkt.y = player_->y_;
			res_pkt.dir_x = player_->_dir_x;
			res_pkt.dir_y = player_->_dir_y;
			res_pkt.move_time = 0;
			do_send(res_pkt.size, reinterpret_cast<char*>(&res_pkt));
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
		send_move_packet(id_, packet->move_time); // 나한테 보내기

		for (auto& id : new_visible_players)
		{
			if (!old_view.contains(id))
			{
				if (is_npc_id(id)) {
					auto npc = npcs[id].load();
					S2C_AddObject add_pkt;
					add_pkt.size = sizeof(add_pkt);
					add_pkt.type = PACKET_TYPE::S2C_ADD_OBJECT;
					add_pkt.object_id = id;
					add_pkt.x = npc->x_;
					add_pkt.y = npc->y_;
					strcpy_s(add_pkt.obj_name, npc->userName_);
					add_pkt.visual_id = npc->_visualId;
					add_pkt.hp        = npc->_hp;
					add_pkt.max_hp    = npc->_maxHp;
					add_pkt.exp       = npc->_exp;
					add_pkt.level     = npc->_level;
					add_pkt.armor_tier = 0; // NPCs don't have armor tier
					add_pkt.weapon_tier = 0;
					add_pkt.dir_x = 0;
					add_pkt.dir_y = -1;
					do_send(add_pkt.size, reinterpret_cast<char*>(&add_pkt));

					npc->wake_up();

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
				if (!is_npc_id(id)) {
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
				if (is_npc_id(id)) {
					S2C_RemoveObject remove_pkt;
					remove_pkt.size = sizeof(remove_pkt);
					remove_pkt.type = PACKET_TYPE::S2C_REMOVE_OBJECT;
					remove_pkt.object_id = id;
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
		break;
	}
	case PACKET_TYPE::C2S_USE_QUICKSLOT:
	{
		C2S_UseQuickSlot* pkt = reinterpret_cast<C2S_UseQuickSlot*>(buff);
		auto now = std::chrono::system_clock::now();

		if (pkt->slot_id == 1) { // Health Potion
			if (now >= player_->potion_cooldown_end_time_) {
				player_->potion_cooldown_end_time_ = now + std::chrono::milliseconds(g_player_config.potion_cooldown_ms);
				player_->_hp += g_player_config.potion_heal_amount;
				player_->_hp = std::min(player_->_hp, player_->_maxHp);

				// Send status update
				S2C_StatusChange stat;
				stat.size = sizeof(stat);
				stat.type = S2C_STATUS_CHANGE;
				stat.object_id = id_;
				stat.hp = player_->_hp;
				stat.max_hp = player_->_maxHp;
				stat.exp = 0; // TODO: Track EXP in Player
				stat.level = 1;
				stat.armor_tier = player_->get_armor_tier();
				stat.weapon_tier = player_->_weapon_tier;
				do_send(stat.size, reinterpret_cast<char*>(&stat));

				visible_players_mutex.lock();
				auto view_copy = visible_players;
				visible_players_mutex.unlock();
				for (auto& pid : view_copy) {
					std::shared_ptr<SESSION> s = clients[pid].load();
					if (s && s->state_ == client_state::playing) {
						s->do_send(stat.size, reinterpret_cast<char*>(&stat));
					}
				}
			}
		} else if (pkt->slot_id == 2) { // Buff Skill
			if (now >= player_->skill_cooldown_end_time_) {
				player_->skill_cooldown_end_time_ = now + std::chrono::milliseconds(g_player_config.skill_cooldown_ms);
				player_->buff_end_time_ = now + std::chrono::seconds(g_player_config.skill_buff_duration_sec);
				// We don't send status change here as the client handles the timer UI implicitly upon successful use.
			}
		}
		break;
	}
	case PACKET_TYPE::C2S_ATTACK:
	{
		auto now = std::chrono::system_clock::now();
		if (std::chrono::duration_cast<std::chrono::milliseconds>(now - player_->last_attack_time_).count() < g_player_config.attack_cooldown_ms) {
			break; // Cooldown not met
		}
		player_->last_attack_time_ = now;

		bool has_buff = now < player_->buff_end_time_;

		int damage = player_->_weapon_tier;
		if (has_buff) {
			damage += 1; // Increase damage during buff
		}

		std::vector<std::pair<int, int>> attack_cells;
		short dx = player_->_dir_x;
		short dy = player_->_dir_y;

		if (has_buff) {
			// AoE Half-circle attack
			int radius = 1 + player_->_weapon_tier; // Radius grows with weapon tier
			for (int y = player_->y_ - radius; y <= player_->y_ + radius; ++y) {
				for (int x = player_->x_ - radius; x <= player_->x_ + radius; ++x) {
					float dist = std::sqrt(std::pow(x - player_->x_, 2) + std::pow(y - player_->y_, 2));
					if (dist <= radius) {
						// Check if within half-circle (dot product > 0)
						int rel_x = x - player_->x_;
						int rel_y = y - player_->y_;
						if (rel_x * dx + rel_y * dy >= 0) {
							attack_cells.emplace_back(x, y);
						}
					}
				}
			}
		} else {
			// Normal rectangle attack
			int range = 1;
			int width = 0;
			switch(player_->_weapon_tier) {
			case 1: range = 1; width = 0; break;
			case 2: range = 1; width = 1; break;
			case 3: range = 2; width = 1; break;
			case 4: range = 3; width = 1; break;
			case 5: range = 3; width = 1; break;
			case 6: range = 3; width = 2; break;
			default: range = 1; width = 0; break;
			}
			
			if (dx != 0) {
				int sign = dx > 0 ? 1 : -1;
				for (int r = 1; r <= range; ++r) {
					for (int w = -width; w <= width; ++w) {
						attack_cells.emplace_back(player_->x_ + sign * r, player_->y_ + w);
					}
				}
			} else if (dy != 0) {
				int sign = dy > 0 ? 1 : -1;
				for (int r = 1; r <= range; ++r) {
					for (int w = -width; w <= width; ++w) {
						attack_cells.emplace_back(player_->x_ + w, player_->y_ + sign * r);
					}
				}
			} else {
				attack_cells.emplace_back(player_->x_, player_->y_ - 1);
			}
		}

		// Broadcast attack effect
		S2C_AttackEffect effect;
		effect.size = sizeof(effect);
		effect.type = S2C_ATTACK_EFFECT;
		effect.object_id = id_;
		effect.weapon_tier = player_->_weapon_tier;
		effect.attack_type = has_buff ? 1 : 0;
		effect.x = player_->x_;
		effect.y = player_->y_;
		effect.dir_x = player_->_dir_x;
		effect.dir_y = player_->_dir_y;
		
		do_send(effect.size, reinterpret_cast<char*>(&effect));
		visible_players_mutex.lock();
		auto view_copy = visible_players;
		visible_players_mutex.unlock();
		for (auto& pid : view_copy) {
			std::shared_ptr<SESSION> s = clients[pid].load();
			if (s && s->state_ == client_state::playing) {
				s->do_send(effect.size, reinterpret_cast<char*>(&effect));
			}
		}

		// Apply damage to NPCs
		auto nearby_ids = sector.get_objects_nearby_sector(player_->x_, player_->y_);
		for (auto& oid : nearby_ids) {
			if (is_npc_id(oid)) {
				auto npc = npcs[oid].load();
				if (!npc || !npc->is_active) continue;
				bool hit = false;
				for (auto& cell : attack_cells) {
					if (npc->x_ == cell.first && npc->y_ == cell.second) {
						hit = true;
						break;
					}
				}
				if (hit) {
					npc->last_hit_time_ = std::chrono::system_clock::now();
					int actual_damage = damage * 20; // 테스트를 위해 데미지 20배 뻥튀기!
					npc->_hp -= actual_damage;
					
					// 데미지 입었음을 채팅(말풍선)으로 출력
					S2C_ChatMessage chat_pkt;
					chat_pkt.size = sizeof(chat_pkt);
					chat_pkt.type = S2C_CHAT_MESSAGE;
					chat_pkt.object_id = oid;
					std::string msg = "앗! 체력: " + std::to_string(npc->_hp);
					strncpy_s(chat_pkt.message, msg.c_str(), sizeof(chat_pkt.message));
					
					do_send(chat_pkt.size, reinterpret_cast<char*>(&chat_pkt));
					for (auto& pid : view_copy) {
						std::shared_ptr<SESSION> s = clients[pid].load();
						if (s && s->state_ == client_state::playing) {
							s->do_send(chat_pkt.size, reinterpret_cast<char*>(&chat_pkt));
						}
					}

					if (npc->_hp <= 0) {
						// Death
						npc->is_active = false;
						
						// Reward
						if (npc->_dropItem > 0) {
							player_->add_item(npc->_dropItem, 1);
						}
						if (npc->_dropGold > 0) {
							player_->_gold += (rand() % npc->_dropGold + 1);
						}
						
						// Send inventory sync
						send_inventory_sync();

						// Broadcast remove
						S2C_RemoveObject rm_pkt;
						rm_pkt.size = sizeof(rm_pkt);
						rm_pkt.type = S2C_REMOVE_OBJECT;
						rm_pkt.object_id = oid;
						do_send(rm_pkt.size, reinterpret_cast<char*>(&rm_pkt));
						for (auto& pid : view_copy) {
							std::shared_ptr<SESSION> s = clients[pid].load();
							if (s && s->state_ == client_state::playing) {
								s->do_send(rm_pkt.size, reinterpret_cast<char*>(&rm_pkt));
							}
						}
						sector.remove_object(oid, npc->x_, npc->y_);

						// 리스폰 이벤트 등록 (5초 뒤)
						event_type ev;
						ev.obj_id = oid;
						ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::seconds(5);
						ev.event_id = EVENT_RESPAWN;
						ev.target_id = -1;
						timer_queue.push(ev);
					}
				}
			}
		}
		break;
	}
	case PACKET_TYPE::C2S_CHAT:
	{
		C2S_Chat* packet = reinterpret_cast<C2S_Chat*>(buff);

		S2C_ChatMessage chat_pkt;
		chat_pkt.size = sizeof(chat_pkt);
		chat_pkt.type = S2C_CHAT_MESSAGE;
		chat_pkt.object_id = id_;
		strncpy_s(chat_pkt.message, packet->message, MAX_CHAT_MSG_LEN - 1);

		// 1. 나 자신에게 에코(Echo)
		do_send(chat_pkt.size, reinterpret_cast<char*>(&chat_pkt));

		// 2. 내 시야에 있는 다른 플레이어들에게 브로드캐스트
		visible_players_mutex.lock();
		auto view_copy = visible_players;
		visible_players_mutex.unlock();

		for (auto& pid : view_copy) {
			std::shared_ptr<SESSION> s = clients[pid].load();
			if (s && s->state_ == client_state::playing) {
				s->do_send(chat_pkt.size, reinterpret_cast<char*>(&chat_pkt));
			}
		}
		break;
	}
	case PACKET_TYPE::C2S_TEST_WEAPON:
	{
		C2S_TestWeapon* pkt = reinterpret_cast<C2S_TestWeapon*>(buff);
		player_->_weapon_tier = pkt->weapon_tier;
		
		S2C_StatusChange stat;
		stat.size = sizeof(stat);
		stat.type = S2C_STATUS_CHANGE;
		stat.object_id = id_;
		stat.hp = 100;
		stat.max_hp = 100;
		stat.exp = 0;
		stat.level = 1;
		stat.armor_tier = player_->get_armor_tier();
		stat.weapon_tier = player_->_weapon_tier;
		do_send(stat.size, reinterpret_cast<char*>(&stat));

		visible_players_mutex.lock();
		auto view_copy = visible_players;
		visible_players_mutex.unlock();
		for (auto& pid : view_copy) {
			std::shared_ptr<SESSION> s = clients[pid].load();
			if (s && s->state_ == client_state::playing) {
				s->do_send(stat.size, reinterpret_cast<char*>(&stat));
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
		S2C_MoveObject move_packet;
		move_packet.size = sizeof(S2C_MoveObject);
		move_packet.type = PACKET_TYPE::S2C_MOVE_OBJECT;
		move_packet.object_id = move_player_id;
		move_packet.x = npc->x_;
		move_packet.y = npc->y_;
		move_packet.dir_x = 0;
		move_packet.dir_y = -1;
		move_packet.move_time = timestamp;
		do_send(move_packet.size, reinterpret_cast<char*>(&move_packet));
		return;
	}

	std::shared_ptr<SESSION> session = clients[move_player_id].load();
	if (!session || !session->player_) return;

	S2C_MoveObject move_packet;
	move_packet.size = sizeof(S2C_MoveObject);
	move_packet.type = PACKET_TYPE::S2C_MOVE_OBJECT;
	move_packet.object_id = move_player_id;
	move_packet.x = session->player_->x_;
	move_packet.y = session->player_->y_;
	move_packet.dir_x = session->player_->_dir_x;
	move_packet.dir_y = session->player_->_dir_y;
	move_packet.move_time = timestamp;
	do_send(move_packet.size, reinterpret_cast<char*>(&move_packet));
}
void SESSION::send_add_player(int player_id)
{
	std::shared_ptr<SESSION> session = clients[player_id].load();
	if (!session || !session->player_) return;

	S2C_AddObject add_packet;
	add_packet.size = sizeof(S2C_AddObject);
	add_packet.type = PACKET_TYPE::S2C_ADD_OBJECT;
	add_packet.object_id = player_id;
	strncpy_s(add_packet.obj_name, session->player_->userName_, MAX_NAME_LEN);
	add_packet.x = session->player_->x_;
	add_packet.y = session->player_->y_;
	add_packet.armor_tier = session->player_->get_armor_tier();
	add_packet.weapon_tier = session->player_->_weapon_tier;
	add_packet.dir_x = session->player_->_dir_x;
	add_packet.dir_y = session->player_->_dir_y;

	visible_players_mutex.lock();
	if (visible_players.contains(player_id))
	{
		visible_players_mutex.unlock();
		return;
	}
	visible_players.insert(player_id);
	visible_players_mutex.unlock();
	do_send(sizeof(S2C_AddObject), reinterpret_cast<char*>(&add_packet));
}
void SESSION::send_already_spawn_players()
{
	auto object_ids_nearby_sector = sector.get_objects_nearby_sector(player_->x_, player_->y_);
	for (auto& id : object_ids_nearby_sector)
	{
		if (id != this->id_)
		{
			if (is_npc_id(id)) {
				auto npc = npcs[id].load();
				if (is_visible(npc->x_, npc->y_)) {
					S2C_AddObject add_pkt;
					add_pkt.size = sizeof(add_pkt);
					add_pkt.type = PACKET_TYPE::S2C_ADD_OBJECT;
					add_pkt.object_id = id;
					add_pkt.x = npc->x_;
					add_pkt.y = npc->y_;
					strcpy_s(add_pkt.obj_name, npc->userName_);
					add_pkt.visual_id = npc->_visualId;
					add_pkt.hp        = npc->_hp;
					add_pkt.max_hp    = npc->_maxHp;
					add_pkt.exp       = npc->_exp;
					add_pkt.level     = npc->_level;
					add_pkt.armor_tier = 0;
					add_pkt.weapon_tier = 0;
					add_pkt.dir_x = 0;
					add_pkt.dir_y = -1;
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
		if (is_npc_id(id))
		{
			auto npc = npcs[id].load();
			npc->wake_up();
		}
	}
}
void broadcast_player_remove_packet(int player_id)
{
	auto removed_player = clients[player_id].load();
	if (!removed_player) return;

	for (auto& id : removed_player->visible_players)
	{
		if (id != player_id && !is_npc_id(id))
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
	// ── 데이터 경로 설정 (실행 파일 기준 ../../Data/) ──────────────────────────
	// Visual Studio 기본 실행 디렉터리: Server/Server/ 이므로 ../../Data/ 가 됨.
	const std::string data_dir = "../../Data/";
	const std::string lua_path  = data_dir + "npc_config.lua";
	const std::string player_lua = data_dir + "player_config.lua";
	const std::string spawn_path = data_dir + "map_spawn.bin";
	const std::string collision_path = data_dir + "map_collision.bin";

	// ── Step 1. Lua에서 메타데이터 로드 ─────────────────────────────
	std::cout << "[Server] Loading configs..." << std::endl;
	auto meta_table   = load_npc_config_lua(lua_path.c_str());
	g_armor_config    = load_armor_config_lua(lua_path.c_str());
	g_player_config   = load_player_config_lua(player_lua.c_str());

	// ── Step 1.5. 충돌 맵 로드 ─────────────────────────────
	std::cout << "[Server] Loading map_collision.bin..." << std::endl;
	std::ifstream coll_file(collision_path, std::ios::binary);
	if (coll_file) {
		g_server_collision.resize(WORLD_WIDTH * WORLD_HEIGHT);
		coll_file.read(reinterpret_cast<char*>(g_server_collision.data()), g_server_collision.size());
	} else {
		std::cerr << "[Server][WARNING] Failed to load map_collision.bin!" << std::endl;
	}

	// ── Step 2. map_spawn.bin에서 스폰 위치 로드 ─────────────────────────────
	std::cout << "[Server] Loading map_spawn.bin..." << std::endl;
	auto spawn_entries = load_spawn_map(spawn_path.c_str());

	if (spawn_entries.empty()) {
		std::cerr << "[Server][FATAL] 스폰 데이터 없음! 경로 확인: "
		          << spawn_path << std::endl;
		return;
	}

	// ── Step 3. 스폰 엔트리 → NPC 객체 생성 ─────────────────────────────────
	for (auto& entry : spawn_entries)
	{
		const NpcMeta& m  = meta_table[entry.type_id];
		int npc_id        = make_npc_id(npc_index++);
		auto npc          = std::make_shared<NPC>();

		npc->id_       = npc_id;
		npc->x_        = entry.x;
		npc->y_        = entry.y;
		npc->initial_x_= entry.x;
		npc->initial_y_= entry.y;
		npc->_npcType  = static_cast<NpcType>(entry.type_id);
		npc->_visualId = m.visual_id;
		npc->_hp       = m.hp;
		npc->_maxHp    = m.max_hp;
		npc->_visualId = m.visual_id;
		npc->_level    = static_cast<unsigned char>(m.level);
		npc->_exp      = m.exp;
		npc->_attack   = m.attack;
		npc->_dropItem = m.drop_item;
		npc->_dropGold = m.drop_gold;
		strncpy_s(npc->userName_, m.name.c_str(), sizeof(npc->userName_) - 1);

		npcs[npc_id] = npc;
		sector.add_object(npc_id, npc->x_, npc->y_);
	}

	std::cout << "[Server] NPC 초기화 완료: "
	          << spawn_entries.size() << "마리" << std::endl;
}
void timer_thread()
{
	using namespace std::chrono;

	// 실행 시간(wakeup_time)이 가장 빠른 이벤트가 먼저 오도록 정렬하는 비교 함수
	std::priority_queue<event_type, std::vector<event_type>> local_queue;

	while (true)
	{
		event_type ev;
		// 글로벌 큐에 새로 들어온 모든 이벤트를 로컬 큐로 옮김
		while (timer_queue.try_pop(ev))
		{
			local_queue.push(ev);
		}

		if (!local_queue.empty())
		{
			auto now = system_clock::now();
			auto top_ev = local_queue.top();

			// 가장 빠른 타이머의 실행 시간이 되었는지 확인
			if (now >= top_ev.wakeup_time)
			{
				local_queue.pop();
				switch (top_ev.event_id)
				{
				case EVENT_MOVE:
				{
					EXP_OVER* move_over = new EXP_OVER;
					move_over->type = io_type::npc_move;
					PostQueuedCompletionStatus(h_iocp, -1, top_ev.obj_id, &move_over->over);
				}
				break;
				case EVENT_RESPAWN:
				{
					EXP_OVER* respawn_over = new EXP_OVER;
					respawn_over->type = io_type::npc_respawn;
					PostQueuedCompletionStatus(h_iocp, -1, top_ev.obj_id, &respawn_over->over);
				}
				break;
				default:
					std::cout << "Unknown Event Type in Timer Thread!" << std::endl;
					break;
				}
			}
			else
			{
				// 아직 실행 시간이 되지 않았다면 잠깐 대기
				std::this_thread::sleep_for(milliseconds(1));
			}
		}
		else
		{
			// 처리할 이벤트가 없으면 대기
			std::this_thread::sleep_for(milliseconds(1));
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
				new_session->player_->x_ = 1000 + 100;
				new_session->player_->y_ = 1000;
				new_session->player_->_armor = g_armor_config.copper;

				sector.add_object(current_id, new_session->player_->x_, new_session->player_->y_);
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
			std::shared_ptr<SESSION> cl = clients[id].load();
			if (cl) cl->flush_send();
			break;
		}
		case io_type::npc_move:
		{
			delete o;
			auto npc = npcs[id].load();
			bool has_nearby_player = false;
			if (!npc)
				break;

			has_nearby_player = npc->do_timer_move();

			if (has_nearby_player)
			{
				event_type ev;
				ev.obj_id = id;
				ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::milliseconds(MOVE_COOL_TIME + rand() % MOVE_COOL_TIME_VARIATION);
				ev.event_id = EVENT_MOVE;
				ev.target_id = -1;
				timer_queue.push(ev);
			}
			else
			{
				npc->is_active = false;
				// 잠들기 직전 플레이어가 들어왔는지 다시 확인 (Lost Wakeup 방지)
				if (npc->do_timer_move()) {
					npc->wake_up();
				}
			}

			break;
		}
		case io_type::npc_respawn:
		{
			delete o;
			if (is_npc_id(id)) {
				auto npc = npcs[id].load();
				if (npc) {
					npc->x_ = npc->initial_x_;
					npc->y_ = npc->initial_y_;
					npc->_hp = npc->_maxHp;
					sector.add_object(npc->id_, npc->x_, npc->y_);
					npc->wake_up(); // 주변 플레이어 감지 및 이동 타이머 시작
					
					// 리스폰 확인용 로그 및 채팅 브로드캐스트
					//std::cout << "[Respawn] NPC ID: " << npc->id_ << " respawned at (" << npc->x_ << ", " << npc->y_ << ")\n";

					/*S2C_ChatMessage chat_pkt;
					chat_pkt.size = sizeof(chat_pkt);
					chat_pkt.type = S2C_CHAT_MESSAGE;
					chat_pkt.object_id = npc->id_;
					strncpy_s(chat_pkt.message, "부활했습니다!", sizeof(chat_pkt.message));
					
					for (auto pid : sector.get_objects_nearby_sector(npc->x_, npc->y_)) {
						if (!is_npc_id(pid)) {
							std::shared_ptr<SESSION> s = clients[pid].load();
							if (s && s->state_ == client_state::playing) {
								s->do_send(chat_pkt.size, reinterpret_cast<char*>(&chat_pkt));
							}
						}
					}*/
				}
			}
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

	std::thread ai_thread_handle(timer_thread);

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
