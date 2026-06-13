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
#include "DBManager.h"
#include "npc_loader.h"
#include "npc_ai_manager.h"
#include <tbb/concurrent_unordered_map.h>
#include <unordered_set>


class SESSION;
enum NpcType : uint8_t {
	NPC_NONE = 0, // 빈 값
	NPC_ZOMBIE = 1,
	NPC_SKELETON = 2,
	NPC_CREEPER = 3,
	NPC_ENDERMAN = 4,
	NPC_IRON_GOLEM = 5,
	NPC_ENDER_DRAGON = 11,
	NPC_FIRE_ZONE = 12,
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
constexpr int VIEW_RANGE = 6;
constexpr int SECTOR_SIZE = 10;
constexpr int MOVE_COOL_TIME = 1000; // NPC 이동 쿨타임 (밀리초)
constexpr int MOVE_COOL_TIME_VARIATION = 1000;

std::atomic<int> player_index = 1;
std::atomic<int> npc_index = 20000;

HANDLE h_iocp;
SOCKET server_socket;

constexpr int EVENT_MOVE = -1;
constexpr int EVENT_RESPAWN = -2;
constexpr int EVENT_PROJECTILE = -3;
constexpr int EVENT_PLAYER_RESPAWN = -4;
constexpr int EVENT_NPC_FIRE_DAMAGE = -5;
struct event_type {
	int obj_id = -1;
	std::chrono::time_point<std::chrono::system_clock> wakeup_time;
	int event_id = 0;
	int target_id = -1;
	int16_t dir_x = 0;
	int16_t dir_y = 0;
	int16_t step = 0;
	int attack_power = 0;
	int16_t start_x = 0;
	int16_t start_y = 0;
	int proj_type = 0; // 0: Arrow, 1: Fireball
	constexpr bool operator < (const event_type& _Left) const
	{
		return (wakeup_time > _Left.wakeup_time);
	}
};

enum class io_type
{
	send,
	recv,
	accept,
	npc_move,
	npc_respawn,
	npc_projectile,
	player_respawn,
	npc_fire_damage,
	db_player_auth,
	db_save_position,
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
	std::vector<char> pending_flat_data;
	event_type event_data;
	DBTask db_task;

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
	// 배칭용 생성자 (Flat Buffer)
	EXP_OVER(std::vector<char>&& data) : type(io_type::send), pending_flat_data(std::move(data))
	{
		ZeroMemory(&over, sizeof(over));
		wsabuffer.buf = pending_flat_data.data();
		wsabuffer.len = static_cast<ULONG>(pending_flat_data.size());
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



// event_type and EVENT constants moved to top
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

// Quest Definitions
struct QuestData {
	int target_npc_type;
	int required_count;
};

QuestData get_quest_data(int stage) {
	switch (stage) {
		case 0: return { 5, 3 }; // Iron Golem x3
		case 1: return { 1, 5 }; // Zombie x5
		case 2: return { 2, 5 }; // Skeleton x5
		case 3: return { 3, 3 }; // Creeper x3
		case 4: return { 4, 2 }; // Enderman x2
		case 5: return { 11, 1 }; // Ender Dragon x1
		default: return { 0, 0 }; // Completed or invalid
	}
}

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

enum class PlayerState {
	ALIVE,
	DEAD
};

class Player : public BaseObject {
public:
	std::atomic<PlayerState> _state{PlayerState::ALIVE};
	short _dir_x = 0;
	short _dir_y = -1;
	int _hp = 100;
	int _maxHp = 100;
	int _head_tier = 0;
	int _chest_tier = 0;
	int _legs_tier = 0;
	int _boots_tier = 0;
	int _weapon_tier = 0;
	int _level = 1;
	int _exp = 0;

	// Inventory
	int _gold = 0;
	std::unordered_map<int, int> inventory; // item_id -> count

	// Quest
	int _quest_stage = 0;
	int _quest_progress = 0;

	// Skills
	int _unspent_sp = 0;
	int _skills_mask = 0;

	std::chrono::time_point<std::chrono::system_clock> last_move_timestamp_;
	std::chrono::time_point<std::chrono::system_clock> last_attack_time_;
	std::chrono::time_point<std::chrono::system_clock> potion_cooldown_end_time_;
	std::chrono::time_point<std::chrono::system_clock> skill_cooldown_end_time_;
	std::chrono::time_point<std::chrono::system_clock> ender_pearl_cooldown_end_time_;
	std::chrono::time_point<std::chrono::system_clock> buff_end_time_;

	Player() : BaseObject(), last_move_timestamp_(std::chrono::system_clock::now()),
			   last_attack_time_(std::chrono::system_clock::now()),
			   potion_cooldown_end_time_(std::chrono::system_clock::now()),
			   skill_cooldown_end_time_(std::chrono::system_clock::now()),
			   ender_pearl_cooldown_end_time_(std::chrono::system_clock::now()),
			   buff_end_time_(std::chrono::system_clock::now()) {}

	void add_item(int item_id, int count) {
		inventory[item_id] += count;
	}

	int get_part_defense(int tier) const {
		switch (tier) {
			case 1: return g_armor_config.copper / 4;
			case 2: return g_armor_config.iron / 4;
			case 3: return g_armor_config.diamond / 4;
			case 4: return g_armor_config.netherite / 4;
			default: return 0;
		}
	}

	int get_defense() const {
		return get_part_defense(_head_tier) + get_part_defense(_chest_tier) +
		       get_part_defense(_legs_tier) + get_part_defense(_boots_tier);
	}

	int get_required_exp() const {
		return _level * _level * 100;
	}

	bool gain_exp(int amount) {
		bool leveled_up = false;
		int old_level = _level;
		_exp += amount;
		while (_exp >= get_required_exp()) {
			_exp -= get_required_exp();
			_level++;
			leveled_up = true;
		}
		if (leveled_up) {
			if (old_level < 3 && _level >= 3) _unspent_sp++;
			if (old_level < 5 && _level >= 5) _unspent_sp++;

			update_max_hp();
			_hp = _maxHp; // 레벨업 시 체력 비율 상관없이 100% 회복
		}
		return leveled_up;
	}

	int get_average_tier() const {
		return (_head_tier + _chest_tier + _legs_tier + _boots_tier) / 4;
	}

	void update_max_hp() {
		int avg_tier = get_average_tier();
		int new_max = 100 + (_level - 1) * 20; // 레벨당 20 증가
		switch (avg_tier) {
			case 1: new_max += 50; break;
			case 2: new_max += 100; break;
			case 3: new_max += 200; break;
			case 4: new_max += 400; break;
		}
		if (new_max != _maxHp) {
			// 체력 비율 유지
			float ratio = (float)_hp / _maxHp;
			_maxHp = new_max;
			_hp = (int)(_maxHp * ratio);
		}
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
	char			userId_[MAX_NAME_LEN] = {};

	std::shared_ptr<Player> player_;

	std::unordered_set<int> visible_players; // 현재 보이는 플레이어 ID 목록
	std::mutex visible_players_mutex; // 보이는 플레이어 목록 보호용 뮤텍스

	// 송신 비동기 제어용 추가
	std::mutex send_mtx;
	std::vector<char> send_queue;
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
			send_queue.insert(send_queue.end(), packet, packet + size);
		}

		bool expected = false;
		if (is_sending.compare_exchange_strong(expected, true)) {
			flush_send();
		}
	}

	void flush_send()
	{
		std::vector<char> sending_data;
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
		int ret = WSASend(client_, &send_over->wsabuffer, 1, &send_bytes, 0, &send_over->over, NULL);

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
		info_packet.head_tier = player_->_head_tier;
		info_packet.chest_tier = player_->_chest_tier;
		info_packet.legs_tier = player_->_legs_tier;
		info_packet.boots_tier = player_->_boots_tier;
		info_packet.weapon_tier = player_->_weapon_tier;
		info_packet.dir_x = player_->_dir_x;
		info_packet.dir_y = player_->_dir_y;
		info_packet.hp = player_->_hp;
		info_packet.max_hp = player_->_maxHp;
		info_packet.exp = player_->_exp;
		info_packet.level = player_->_level;
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
	bool is_visible(int x, int y, int npc_type = 0) {
		if (npc_type == 11) return true; // NPC_ENDER_DRAGON
		return abs(x - player_->x_) <= VIEW_RANGE && abs(y - player_->y_) <= VIEW_RANGE;
	}
	void send_already_spawn_players();
	void force_teleport(int tx, int ty);
	
	void save_to_db()
	{
		if (!player_) return;
		// 더미 클라이언트 또는 아직 로그인 미완료 세션은 DB 저장 생략
		if (userId_[0] == '\0') return;
		if (strncmp(userId_, "DUMMY_", 6) == 0) return;
		
		DBTask task;
		task.type = DBTaskType::SAVE_USER_DATA;
		task.ioType = static_cast<int>(io_type::db_save_position);
		task.session_id = id_;
		
		player_data pd = {};
		pd.x = player_->x_;
		pd.y = player_->y_;
		pd.hp = player_->_hp;
		pd.max_hp = player_->_maxHp;
		pd.level = player_->_level;
		pd.exp = player_->_exp;
		pd.gold = player_->_gold;
		pd.head_tier = player_->_head_tier;
		pd.chest_tier = player_->_chest_tier;
		pd.legs_tier = player_->_legs_tier;
		pd.boots_tier = player_->_boots_tier;
		pd.weapon_tier = static_cast<int>(player_->_weapon_tier);
		pd.quest_stage = player_->_quest_stage;
		pd.quest_progress = player_->_quest_progress;
		pd.unspent_sp = player_->_unspent_sp;
		pd.skills_mask = player_->_skills_mask;
		pd.inventory = player_->inventory;
		strncpy_s(pd.user_id, sizeof(pd.user_id), userId_, sizeof(pd.user_id) - 1);
		
		task.data = pd;
		DBManager::Instance().PushTask(task);
	}

	void send_login_failure()
	{
		S2C_LoginResult ack_packet;
		ack_packet.size = sizeof(S2C_LoginResult);
		ack_packet.type = PACKET_TYPE::S2C_LOGIN_RESULT;
		ack_packet.success = false;
		strcpy_s(ack_packet.message, "Login failed.");
		do_send(ack_packet.size, reinterpret_cast<char*>(&ack_packet));
	}

	void send_quest_info() {

		S2C_QuestInfo pkt;
		pkt.size = sizeof(pkt);
		pkt.type = S2C_QUEST_INFO;
		pkt.quest_stage = player_->_quest_stage;
		pkt.quest_progress = player_->_quest_progress;

		QuestData qd = get_quest_data(pkt.quest_stage);
		pkt.max_progress = qd.required_count;
		do_send(pkt.size, reinterpret_cast<char*>(&pkt));
	}

	void send_skill_sync() {
		S2C_SkillSync pkt;
		pkt.size = sizeof(pkt);
		pkt.type = S2C_SKILL_SYNC;
		pkt.unspent_sp = player_->_unspent_sp;
		pkt.skills_mask = player_->_skills_mask;
		do_send(pkt.size, reinterpret_cast<char*>(&pkt));
	}
};

tbb::concurrent_unordered_map<int, std::atomic<std::shared_ptr<SESSION>>> clients;

void player_take_damage(std::shared_ptr<SESSION> s, int actual_dmg)
{
	if (!s || !s->player_) return;
	// 더미 클라이언트는 데미지 무시
	if (strncmp(s->userId_, "DUMMY_", 6) == 0) return;
	
	if (s->player_->_state.load() == PlayerState::DEAD) return;

	if (s->player_->_skills_mask & (1 << static_cast<int>(SkillType::RESISTANCE))) {
		actual_dmg = actual_dmg * 70 / 100;
	}
	s->player_->_hp -= actual_dmg;
	if (s->player_->_hp <= 0)
	{
		PlayerState expected = PlayerState::ALIVE;
		if (s->player_->_state.compare_exchange_strong(expected, PlayerState::DEAD))
		{
			s->player_->_hp = 0;
			int drop_amount = static_cast<int>(s->player_->get_required_exp() * 0.10f);
			s->player_->_exp = std::max(0, s->player_->_exp - drop_amount);
			
			event_type ev;
			ev.obj_id = s->id_;
			ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::seconds(5);
			ev.event_id = EVENT_PLAYER_RESPAWN;
			ev.target_id = -1;
			timer_queue.push(ev);
		}
	}

	S2C_StatusChange stat;
	stat.size       = sizeof(stat);
	stat.type       = S2C_STATUS_CHANGE;
	stat.object_id  = s->id_;
	stat.hp         = s->player_->_hp;
	stat.max_hp     = s->player_->_maxHp;
	stat.exp        = s->player_->_exp;
	stat.level      = s->player_->_level;
	stat.head_tier = s->player_->_head_tier;
	stat.chest_tier = s->player_->_chest_tier;
	stat.legs_tier = s->player_->_legs_tier;
	stat.boots_tier = s->player_->_boots_tier;
	stat.weapon_tier = s->player_->_weapon_tier;
	s->do_send(stat.size, reinterpret_cast<char*>(&stat));
}

class NPC : public BaseObject {
public:
	std::atomic<bool> is_active{ false };
	std::atomic<int> fire_stacks_{ 0 };

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
	// AI 상태
	NpcState _aiState   = NpcState::IDLE;
	int      _aggroTarget = -1;  // 어그로 중인 플레이어 ID
public:
	NPC() : BaseObject(), last_move_timestamp_(std::chrono::system_clock::now()), last_hit_time_(std::chrono::system_clock::now()) {}
	std::chrono::time_point<std::chrono::system_clock> last_move_timestamp_; // NPC의 마지막 이동 시간 기록
	std::chrono::time_point<std::chrono::system_clock> last_hit_time_;       // 마지막 피격 시간
	int16_t initial_x_ = 0;
	int16_t initial_y_ = 0;
	void heartbeat()
	{
		do_ai();
	}
	int do_timer_move()
	{
		auto now = std::chrono::system_clock::now();
		if (_hp < _maxHp && std::chrono::duration_cast<std::chrono::seconds>(now - last_hit_time_).count() >= 5) {
			_hp += _maxHp / 10;
			_hp = std::min(_hp, _maxHp);

			S2C_StatusChange stat;
			stat.size       = sizeof(stat);
			stat.type       = S2C_STATUS_CHANGE;
			stat.object_id  = id_;
			stat.hp         = _hp;
			stat.max_hp     = _maxHp;
			stat.exp        = _exp;
			stat.level      = _level;
			stat.head_tier = 0;
						stat.chest_tier = 0;
						stat.legs_tier = 0;
						stat.boots_tier = 0;
			stat.weapon_tier = 0;

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
						s->do_send(stat.size, reinterpret_cast<char*>(&stat));
						s->do_send(chat_pkt.size, reinterpret_cast<char*>(&chat_pkt));
					}
				}
			}
		}

		int delay_ms = do_ai();
		/*if (id_ == make_npc_id(20000))
		{
			auto delay = std::chrono::system_clock::now() - last_move_timestamp_;
			std::cout << "NPC " << id_ << " moved. Time since last move: " << std::chrono::duration_cast<std::chrono::milliseconds>(delay).count() << " ms\n";
		}*/

		last_move_timestamp_ = std::chrono::system_clock::now();
		return delay_ms;
	}
	// ── 주변에서 가장 가까운 플레이어 찾기 ────────────────────────────────────────────
	std::pair<int, int> find_nearest_player() const
	{
		int best_id = -1;
		int best_dist = 99999;
		for (auto pid : sector.get_objects_nearby_sector(x_, y_)) {
			if (is_npc_id(pid)) continue;
			std::shared_ptr<SESSION> s = clients[pid].load();
			if (!s || s->state_ != client_state::playing) continue;
			
			//if (strncmp(s->userId_, "DUMMY_", 6) == 0) continue; // 더미 클라이언트는 어그로 대상에서 제외
			int d = std::abs(x_ - s->player_->x_) + std::abs(y_ - s->player_->y_);
			if (d < best_dist) { best_dist = d; best_id = pid; }
		}
		return { best_id, best_dist };
	}

	// ── NpcState 변경 브로드캐스트 ────────────────────────────────────────
	void broadcast_state_change(NpcState new_state) {
		if (_aiState == new_state) return;
		_aiState = new_state;
		S2C_NpcStateChange pkt;
		pkt.size      = sizeof(pkt);
		pkt.type      = S2C_NPC_STATE_CHANGE;
		pkt.object_id = id_;
		pkt.npc_state = static_cast<char>(new_state);
		pkt.target_id = _aggroTarget;
		for (auto pid : sector.get_objects_nearby_sector(x_, y_)) {
			if (is_npc_id(pid)) continue;
			std::shared_ptr<SESSION> s = clients[pid].load();
			if (s && s->state_ == client_state::playing)
				s->do_send(pkt.size, reinterpret_cast<char*>(&pkt));
		}
	}

	// ── 실제 이동 실행 ─────────
	// 반환값: 다음 타이머까지의 딜레이(ms). 0이면 끄기(Sleep).
	int run_ai_tick() {
		// 1. 인접 플레이어 스냅샷
		std::unordered_set<int> old_vl;
		for (auto pid : sector.get_objects_nearby_sector(x_, y_)) {
			if (is_npc_id(pid)) continue;
			std::shared_ptr<SESSION> s = clients[pid].load();
			if (!s || s->state_ != client_state::playing) continue;
			old_vl.insert(pid);
		}

		// 2. 어그로 타겟 갱신 (on_hit로 바뀌면 유지)
		auto [nearest_id, nearest_dist] = find_nearest_player();
		if (_aggroTarget == -1 || nearest_id == -1) _aggroTarget = nearest_id;

		// 3. Lua AI 컨텍스트 구성
		NpcAiContext ctx;
		ctx.self_id       = id_;
		ctx.self_x        = x_;
		ctx.self_y        = y_;
		ctx.self_hp       = _hp;
		ctx.self_max_hp   = _maxHp;
		ctx.initial_x     = initial_x_;
		ctx.initial_y     = initial_y_;
		ctx.current_state = static_cast<int>(_aiState);
		ctx.target_id     = _aggroTarget;
		if (_aggroTarget != -1) {
			std::shared_ptr<SESSION> s = clients[_aggroTarget].load();
			if (s && s->player_) {
				ctx.target_x = s->player_->x_;
				ctx.target_y = s->player_->y_;
				ctx.dist = std::abs(x_ - ctx.target_x) + std::abs(y_ - ctx.target_y);
			} else {
				_aggroTarget = nearest_id;
				ctx.target_id = nearest_id;
				ctx.dist = nearest_dist;
			}
		} else {
			ctx.dist = nearest_dist;
		}

		// 4. Lua on_timer 호용
		NpcAction action = NpcAiManager::call_on_timer(static_cast<int>(_npcType), ctx);

		// 5. 상태 변경 브로드캐스드
		broadcast_state_change(static_cast<NpcState>(action.new_state));

		// 6. 액션 실행
		int old_x = x_, old_y = y_;

		switch (action.type) {
		case NpcActionType::MOVE: {
			// 엔더맨 텔레포트: dx/dy가 1보다 클 수 있음
			int step_x = (action.dx != 0) ? (action.dx > 0 ? 1 : -1) : 0;
			int step_y = (action.dy != 0) ? (action.dy > 0 ? 1 : -1) : 0;
			int steps  = std::max(std::abs(action.dx), std::abs(action.dy));
			steps = std::min(steps, 5); // 텔레포트 최대 5칸
			for (int s = 0; s < steps; ++s) {
				int nx = x_ + step_x;
				int ny = y_ + step_y;
				if (_npcType == NPC_ENDER_DRAGON || is_passable(nx, ny)) { x_ = nx; y_ = ny; }
				else break;
			}
			break;
		}
		case NpcActionType::MELEE_ATTACK: {
			if (action.target_id >= 0) {
				std::shared_ptr<SESSION> s = clients[action.target_id].load();
				if (s && s->state_ == client_state::playing && s->player_) {
					int tx = s->player_->x_ - x_;
					int ty = s->player_->y_ - y_;
					int dir_x = (std::abs(tx) >= std::abs(ty)) ? (tx > 0 ? 1 : -1) : 0;
					int dir_y = (std::abs(ty) > std::abs(tx))  ? (ty > 0 ? 1 : -1) : 0;

					// 공격 이펙트 브로드캐스트
					S2C_AttackEffect eff;
					eff.size = sizeof(eff);
					eff.type = S2C_ATTACK_EFFECT;
					eff.object_id = id_;
					eff.weapon_tier = 0;
					eff.attack_type = 0; // 일반 1칸 공격
					eff.x = x_;
					eff.y = y_;
					eff.dir_x = dir_x;
					eff.dir_y = dir_y;
					for (auto pid : old_vl) {
						if (is_npc_id(pid)) continue;
						auto ps = clients[pid].load();
						if (ps && ps->state_ == client_state::playing) ps->do_send(eff.size, reinterpret_cast<char*>(&eff));
					}

					int def = s->player_->get_defense();
					int actual_dmg = std::max(1, _attack - def);
					player_take_damage(s, actual_dmg);
				}
			}
			break;
		}
		case NpcActionType::RANGE_ATTACK: {
			// 스켈레튼: 1칸씩 전진하는 투사체
			if (action.target_id < 0) break;
			std::shared_ptr<SESSION> tgt = clients[action.target_id].load();
			if (!tgt || !tgt->player_) break;
			// 방향 벡터 계산
			int tx = tgt->player_->x_ - x_;
			int ty = tgt->player_->y_ - y_;
			int abs_tx = std::abs(tx), abs_ty = std::abs(ty);
			int dir_x = (abs_tx >= abs_ty) ? (tx > 0 ? 1 : -1) : 0;
			int dir_y = (abs_ty > abs_tx)  ? (ty > 0 ? 1 : -1) : 0;
			
			event_type ev;
			ev.obj_id = id_;
			ev.event_id = EVENT_PROJECTILE;
			ev.start_x = x_;
			ev.start_y = y_;
			ev.dir_x = dir_x;
			ev.dir_y = dir_y;
			ev.step = 1;
			ev.attack_power = _attack;
			ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::milliseconds(200);
			timer_queue.push(ev);
			break;
		}
		case NpcActionType::DIE: {
			// 크리퍼 자폭: 반경 2 내 전체 플레이어에게 AOE 데미지
			S2C_AttackEffect eff;
			eff.size = sizeof(eff);
			eff.type = S2C_ATTACK_EFFECT;
			eff.object_id = id_;
			eff.weapon_tier = 0;
			eff.attack_type = 3; // 5x5 광역 폭발
			eff.x = x_;
			eff.y = y_;
			eff.dir_x = 0;
			eff.dir_y = 0;
			for (auto pid : old_vl) {
				if (is_npc_id(pid)) continue;
				auto ps = clients[pid].load();
				if (ps && ps->state_ == client_state::playing) ps->do_send(eff.size, reinterpret_cast<char*>(&eff));
			}

			for (auto pid : old_vl) {
				std::shared_ptr<SESSION> s = clients[pid].load();
				if (!s || !s->player_) continue;
				int d = std::abs(s->player_->x_ - x_) + std::abs(s->player_->y_ - y_);
				if (d <= 2) {
					int def = s->player_->get_defense();
					int actual_dmg = std::max(1, (_attack * 2) - def);
					player_take_damage(s, actual_dmg);
				}
			}
			// 크리퍼 스스로 사망
			_hp = 0;
			break;
		}
		case NpcActionType::AOE_ATTACK: {
			S2C_AttackEffect eff;
			eff.size = sizeof(eff);
			eff.type = S2C_ATTACK_EFFECT;
			eff.object_id = id_;
			eff.weapon_tier = 0;
			eff.attack_type = 5; // 5: Dragon Tail (Rotate)
			eff.x = x_;
			eff.y = y_;
			eff.dir_x = 0;
			eff.dir_y = 0;
			for (auto pid : old_vl) {
				if (is_npc_id(pid)) continue;
				auto ps = clients[pid].load();
				if (ps && ps->state_ == client_state::playing) ps->do_send(eff.size, reinterpret_cast<char*>(&eff));
			}
			for (auto pid : old_vl) {
				std::shared_ptr<SESSION> s = clients[pid].load();
				if (!s || !s->player_) continue;
				int dist = std::abs(s->player_->x_ - x_) + std::abs(s->player_->y_ - y_);
				if (dist <= 3) {
					player_take_damage(s, _attack);
				}
			}
			break;
		}
		case NpcActionType::BREATH_ATTACK: {
			if (action.target_id < 0) break;
			std::shared_ptr<SESSION> tgt = clients[action.target_id].load();
			if (!tgt || !tgt->player_) break;
			int tx = tgt->player_->x_ - x_;
			int ty = tgt->player_->y_ - y_;
			int abs_tx = std::abs(tx), abs_ty = std::abs(ty);
			int dir_x = (abs_tx >= abs_ty) ? (tx > 0 ? 1 : -1) : 0;
			int dir_y = (abs_ty > abs_tx)  ? (ty > 0 ? 1 : -1) : 0;
			
			event_type ev;
			ev.obj_id = id_;
			ev.event_id = EVENT_PROJECTILE;
			ev.start_x = x_;
			ev.start_y = y_;
			ev.dir_x = dir_x;
			ev.dir_y = dir_y;
			ev.step = 1;
			ev.attack_power = _attack;
			ev.proj_type = 1; // 1: Fireball
			ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::milliseconds(200);
			timer_queue.push(ev);
			
			S2C_AttackEffect eff;
			eff.size = sizeof(eff);
			eff.type = S2C_ATTACK_EFFECT;
			eff.object_id = id_;
			eff.weapon_tier = 0;
			eff.attack_type = 6; // 6: Dragon Breath (64th frame)
			eff.x = x_;
			eff.y = y_;
			eff.dir_x = dir_x;
			eff.dir_y = dir_y;
			for (auto pid : old_vl) {
				if (is_npc_id(pid)) continue;
				auto ps = clients[pid].load();
				if (ps && ps->state_ == client_state::playing) ps->do_send(eff.size, reinterpret_cast<char*>(&eff));
			}
			break;
		}
		case NpcActionType::ZONE_DAMAGE: {
			_hp--;
			if (_hp <= 0) {
				_aiState = NpcState::DEAD;
				break;
			}
			for (auto pid : old_vl) {
				std::shared_ptr<SESSION> s = clients[pid].load();
				if (!s || !s->player_) continue;
				int dist = std::max(std::abs(s->player_->x_ - x_), std::abs(s->player_->y_ - y_));
				if (dist <= 1) { // 3x3 반경 도트 딜
					player_take_damage(s, _attack);
				}
			}
			break;
		}
		default:
			break;
		}

		// 채팅 이펀트
		if (!action.chat_msg.empty()) {
			S2C_ChatMessage chat_pkt;
			chat_pkt.size = sizeof(chat_pkt);
			chat_pkt.type = S2C_CHAT_MESSAGE;
			chat_pkt.object_id = id_;
			strncpy_s(chat_pkt.message, action.chat_msg.c_str(), sizeof(chat_pkt.message));
			for (auto pid : old_vl) {
				std::shared_ptr<SESSION> s = clients[pid].load();
				if (s && s->state_ == client_state::playing)
					s->do_send(chat_pkt.size, reinterpret_cast<char*>(&chat_pkt));
			}
		}

		// 7. 섹터 갱신 + 브로드캐스트 오브젝트 패킷
		sector.move_object(id_, old_x, old_y, x_, y_);

		std::unordered_set<int> new_vl;
		for (auto pid : sector.get_objects_nearby_sector(x_, y_)) {
			if (is_npc_id(pid)) continue;
			std::shared_ptr<SESSION> s = clients[pid].load();
			if (!s || s->state_ != client_state::playing) continue;
			new_vl.insert(pid);
		}

		for (auto& pid : new_vl) {
			if (!old_vl.contains(pid)) {
				std::shared_ptr<SESSION> s = clients[pid].load();
				if (s) s->send_add_player(id_);
			} else {
				std::shared_ptr<SESSION> s = clients[pid].load();
				if (s && s->state_ == client_state::playing)
					s->send_move_packet(id_, MOVE_COOL_TIME);
			}
		}
		for (auto& pid : old_vl) {
			if (!new_vl.contains(pid)) {
				std::shared_ptr<SESSION> s = clients[pid].load();
				if (s) s->send_remove_player(id_);
			}
		}

		// 7. 결과 반환: 플레이어가 주변에 있으면 action.delay_ms로 타이머 재등록, 없으면 0으로 수면
		bool has_player_nearby = false;
		int check_range = (_npcType == 11) ? VIEW_RANGE + 4 : VIEW_RANGE;
		for (auto pid : new_vl) {
			std::shared_ptr<SESSION> s = clients[pid].load();
			if (s && s->state_ == client_state::playing) {
				if (std::abs(s->player_->x_ - x_) <= check_range && std::abs(s->player_->y_ - y_) <= check_range) {
					has_player_nearby = true;
					break;
				}
			}
		}
		return has_player_nearby ? action.delay_ms : 0;
	}

	// ── 기존 do_random_move() 대체 (명칭은 유지하돼 do_timer_move에서 호출) ──
	int do_ai() {
		return run_ai_tick();
	}
	void wake_up()
	{
		bool expected = false;
		if (!is_active.compare_exchange_strong(expected, true))
		{
			return;
		}

		if (_aiState == NpcState::MERCHANT) return; // Do not schedule AI ticks for merchants

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
		strncpy_s(userId_, sizeof(userId_), p->user_id, sizeof(userId_) - 1);

		// ── 더미 클라이언트 (스트레스 테스트): DB 검증 없이 즉시 스폰 ──
		if (strncmp(p->user_id, "DUMMY_", 6) == 0) {
			strncpy_s(player_->userName_, sizeof(player_->userName_), p->user_id, sizeof(player_->userName_) - 1);
			player_->_hp     = 100;
			player_->_maxHp  = 100;
			player_->_level  = 1;
			player_->_exp    = 0;
			player_->_gold   = 0;
			player_->_weapon_tier = 1;

			// 통과 가능한 랜덤 스폰 위치 탐색
			int rx, ry;
			do {
				rx = 100 + rand() % 1700; // 맵 가장자리 100칸 제외
				ry = 100 + rand() % 1700;
			} while (!is_passable(rx, ry));
			player_->x_ = static_cast<int16_t>(rx);
			player_->y_ = static_cast<int16_t>(ry);

			send_login_success();
			sector.add_object(id_, player_->x_, player_->y_);
			send_avatar_info();
			send_already_spawn_players();
			broadcast_new_player(id_);
			state_ = client_state::playing;
			//std::cout << "[DUMMY] " << p->user_id << " spawned at (" << rx << ", " << ry << ")\n";
			break;
		}

		// ── 일반 유저: DB 인증 ──
		state_ = client_state::connected;
		DBTask login_task;
		login_task.session_id = id_;
		login_task.type = DBTaskType::LOGIN_AUTH;
		login_task.ioType = static_cast<int>(io_type::db_player_auth);
		login_task.data = std::string(p->user_id);
		DBManager::Instance().PushTask(login_task);
		break;
	}
	case PACKET_TYPE::C2S_MOVE:
	{
		if (player_->_hp <= 0) return true;
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
			// Boss Room Teleport
			if (player_->_quest_stage == 5 && new_x >= 999 && new_x <= 1001 && new_y >= 1010 && new_y <= 1012) {
				force_teleport(1000, 1800);
				break;
			}
			
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
				if (is_visible(npc->x_, npc->y_, npc->_npcType)) {
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
					add_pkt.head_tier = 0;
					add_pkt.chest_tier = 0;
					add_pkt.legs_tier = 0;
					add_pkt.boots_tier = 0; // NPCs don't have armor tier
					add_pkt.weapon_tier = 0;
					add_pkt.dir_x = 0;
					add_pkt.dir_y = -1;
					add_pkt.npc_state = static_cast<char>(npc->_aiState);
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
		if (player_->_hp <= 0) return true;
		C2S_UseQuickSlot* pkt = reinterpret_cast<C2S_UseQuickSlot*>(buff);
		auto now = std::chrono::system_clock::now();

		if (pkt->slot_id == 1) { // Health Potion
			if (player_->inventory[static_cast<int>(ItemType::HEALTH_POTION)] > 0 && now >= player_->potion_cooldown_end_time_) {
				player_->inventory[static_cast<int>(ItemType::HEALTH_POTION)]--; // Consume 1 potion
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
				stat.exp = player_->_exp;
				stat.level = player_->_level;
				stat.head_tier = player_->_head_tier;
					stat.chest_tier = player_->_chest_tier;
					stat.legs_tier = player_->_legs_tier;
					stat.boots_tier = player_->_boots_tier;
				stat.weapon_tier = player_->_weapon_tier;
				do_send(stat.size, reinterpret_cast<char*>(&stat));

				// Send inventory sync
				send_inventory_sync();

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
		} else if (pkt->slot_id == 3) { // Ender Pearl
			if (player_->_skills_mask & (1 << static_cast<int>(SkillType::ENDER_PEARL))) {
				if (now >= player_->ender_pearl_cooldown_end_time_) {
					player_->ender_pearl_cooldown_end_time_ = now + std::chrono::seconds(10);
					
					int old_x = player_->x_;
					int old_y = player_->y_;
					
					// Teleport 3 cells in current direction
					short dx = player_->_dir_x;
					short dy = player_->_dir_y;
					
					// Default direction if none
					if (dx == 0 && dy == 0) { dy = -1; }
					
					int new_x = player_->x_ + dx * 3;
					int new_y = player_->y_ + dy * 3;
					
					// Bounds check (assuming map is 2000x2000)
					new_x = std::clamp(new_x, 0, 1999);
					new_y = std::clamp(new_y, 0, 1999);
					
					player_->x_ = new_x;
					player_->y_ = new_y;
					
					sector.move_object(id_, old_x, old_y, new_x, new_y);
					
					// Broadcast new position
					S2C_MoveObject move_packet;
					move_packet.size = sizeof(S2C_MoveObject);
					move_packet.type = PACKET_TYPE::S2C_MOVE_OBJECT;
					move_packet.object_id = id_;
					move_packet.x = player_->x_;
					move_packet.y = player_->y_;
					move_packet.dir_x = player_->_dir_x;
					move_packet.dir_y = player_->_dir_y;
					move_packet.move_time = 0; // Instant
					
					auto nearby = sector.get_objects_nearby_sector(player_->x_, player_->y_);
					for (auto pid : nearby) {
						if (!is_npc_id(pid)) {
							std::shared_ptr<SESSION> s = clients[pid].load();
							if (s && s->state_ == client_state::playing)
								s->do_send(move_packet.size, reinterpret_cast<char*>(&move_packet));
						}
					}
					
					// Send teleport to self
					S2C_UpdatePosition tp_pkt;
					tp_pkt.size = sizeof(tp_pkt);
					tp_pkt.type = S2C_UPDATE_POSITION;
					tp_pkt.x = player_->x_;
					tp_pkt.y = player_->y_;
					do_send(tp_pkt.size, reinterpret_cast<char*>(&tp_pkt));
					
					// We might need to handle sector view updates, but standard sector.move_object should handle some of it?
					// Usually the client recalculates view and sends C2S_MOVE or server handles view lists.
				}
			}
		}
		break;
	}
	case PACKET_TYPE::C2S_LEARN_SKILL:
	{
		C2S_LearnSkill* p = reinterpret_cast<C2S_LearnSkill*>(buff);
		int stype = p->skill_type;
		if (player_->_unspent_sp > 0) {
			int mask = 1 << stype;
			if ((player_->_skills_mask & mask) == 0) {
				player_->_skills_mask |= mask;
				player_->_unspent_sp--;
				send_skill_sync();
				std::cout << "Player[" << id_ << "] learned skill: " << stype << "\n";
			}
		}
		break;
	}
	case PACKET_TYPE::C2S_ATTACK:
	{
		if (player_->_hp <= 0) return true;
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
					float dist = std::sqrtf(std::powf(static_cast<float>(x - player_->x_), 2) 
							+ std::powf(static_cast<float>(y - player_->y_), 2));
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
				if (npc->_aiState == NpcState::MERCHANT) continue; // Prevent hitting merchants
				if (npc->_npcType == NPC_FIRE_ZONE) continue; // Prevent hitting fire zones
				bool hit = false;
				for (auto& cell : attack_cells) {
					if (npc->_npcType == NPC_ENDER_DRAGON) {
						if (std::abs(npc->x_ - cell.first) <= 4 && std::abs(npc->y_ - cell.second) <= 4) {
							hit = true;
							break;
						}
					} else {
						if (npc->x_ == cell.first && npc->y_ == cell.second) {
							hit = true;
							break;
						}
					}
				}
				if (hit) {
					npc->last_hit_time_ = std::chrono::system_clock::now();
					int actual_damage = damage * 20; // 테스트를 위해 데미지 20배 뻥튀기!
					npc->_hp -= actual_damage;

					// Skills
					if (player_->_skills_mask & (1 << static_cast<int>(SkillType::LIFE_STEAL))) {
						int heal = actual_damage * 15 / 100;
						heal = std::max(heal, 1);
						player_->_hp += heal;
						player_->_hp = std::min(player_->_hp, player_->_maxHp);

						S2C_StatusChange stat_p;
						stat_p.size = sizeof(stat_p);
						stat_p.type = S2C_STATUS_CHANGE;
						stat_p.object_id = id_;
						stat_p.hp = player_->_hp;
						stat_p.max_hp = player_->_maxHp;
						stat_p.exp = player_->_exp;
						stat_p.level = player_->_level;
						stat_p.head_tier = player_->_head_tier;
						stat_p.chest_tier = player_->_chest_tier;
						stat_p.legs_tier = player_->_legs_tier;
						stat_p.boots_tier = player_->_boots_tier;
						stat_p.weapon_tier = player_->_weapon_tier;
						do_send(stat_p.size, reinterpret_cast<char*>(&stat_p));
						for (auto& pid : view_copy) {
							std::shared_ptr<SESSION> s = clients[pid].load();
							if (s && s->state_ == client_state::playing) s->do_send(stat_p.size, reinterpret_cast<char*>(&stat_p));
						}
					}
					
					if (player_->_skills_mask & (1 << static_cast<int>(SkillType::FIRE_ASPECT))) {
						if (npc->fire_stacks_.load() < 9) { // 최대 3중첩 (3대 타격분 = 9틱)
							npc->fire_stacks_ += 3;
							for (int i = 1; i <= 3; ++i) {
								event_type ev;
								ev.obj_id = oid;
								ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::seconds(i);
								ev.event_id = EVENT_NPC_FIRE_DAMAGE;
								ev.target_id = id_;
								ev.attack_power = actual_damage * 20 / 100; // 20% tick damage
								ev.attack_power = std::max(ev.attack_power, 1);
								timer_queue.push(ev);
							}
						}
					}

					// Send status change to update NPC HP on clients
					S2C_StatusChange stat;
					stat.size       = sizeof(stat);
					stat.type       = S2C_STATUS_CHANGE;
					stat.object_id  = oid;
					stat.hp         = npc->_hp;
					stat.max_hp     = npc->_maxHp;
					stat.exp        = npc->_exp;
					stat.level      = npc->_level;
					stat.head_tier = 0;
					stat.chest_tier = 0;
					stat.legs_tier = 0;
					stat.boots_tier = 0;
					stat.weapon_tier = 0;
					
					do_send(stat.size, reinterpret_cast<char*>(&stat));
					for (auto& pid : view_copy) {
						std::shared_ptr<SESSION> s = clients[pid].load();
						if (s && s->state_ == client_state::playing) {
							s->do_send(stat.size, reinterpret_cast<char*>(&stat));
						}
					}

					// on_hit Lua 호출 (어그로 전환 등)
					NpcAiContext hit_ctx;
					hit_ctx.self_id   = oid;
					hit_ctx.self_x    = npc->x_;
					hit_ctx.self_y    = npc->y_;
					hit_ctx.self_hp   = npc->_hp;
					hit_ctx.self_max_hp = npc->_maxHp;
					hit_ctx.current_state = static_cast<int>(npc->_aiState);
					hit_ctx.target_id = id_;
					NpcAction hit_action = NpcAiManager::call_on_hit(
						static_cast<int>(npc->_npcType), hit_ctx, id_, actual_damage);
					// 어그로 타겟 갱신
					npc->_aggroTarget = id_;
					npc->broadcast_state_change(static_cast<NpcState>(hit_action.new_state));
					// 잠든 NPC 깨우기
					npc->wake_up();
					
					// 데미지 입었음을 채팅(말풍선)으로 출력
					/*{
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
					}*/

					if (npc->_hp <= 0) {
						// Death
						npc->is_active = false;
						
						// Quest Check
						QuestData qd = get_quest_data(player_->_quest_stage);
						if (qd.target_npc_type != 0 && qd.target_npc_type == npc->_npcType) {
							if (player_->_quest_progress < qd.required_count) {
								player_->_quest_progress++;
								send_quest_info();
								
								S2C_ChatMessage msg_pkt;
								msg_pkt.size = sizeof(msg_pkt);
								msg_pkt.type = S2C_CHAT_MESSAGE;
								msg_pkt.object_id = 0; // System
								
								std::string m_name = "Monster";
								if (npc->_npcType == 1) m_name = "Zombie";
								else if (npc->_npcType == 2) m_name = "Skeleton";
								else if (npc->_npcType == 3) m_name = "Creeper";
								else if (npc->_npcType == 4) m_name = "Enderman";
								else if (npc->_npcType == 5) m_name = "Iron Golem";
								else if (npc->_npcType == 11) m_name = "Ender Dragon";

								std::string msg = "[System] " + m_name + " Killed! (" + std::to_string(player_->_quest_progress) + "/" + std::to_string(qd.required_count) + ")";
								if (player_->_quest_progress >= qd.required_count) {
									msg += " - Return to Quest NPC!";
									if (npc->_npcType == 11) {
										force_teleport(1000, 1000);
									}
								}
								strncpy_s(msg_pkt.message, msg.c_str(), sizeof(msg_pkt.message));
								clients[id_].load()->do_send(msg_pkt.size, reinterpret_cast<char*>(&msg_pkt));
							}
						}

						// Reward
						int acquired_gold = 0;
						if (npc->_dropItem > 0) {
							player_->add_item(npc->_dropItem, 1);
						}
						if (npc->_dropGold > 0) {
							acquired_gold = (rand() % npc->_dropGold + 1);
							player_->_gold += acquired_gold;
						}
						
						bool leveled_up = player_->gain_exp(static_cast<int>(npc->_exp));
						
						// 항상 경험치가 갱신되었으므로 플레이어 본인에게 상태 변경 패킷 전송
						S2C_StatusChange stat;
						stat.size       = sizeof(stat);
						stat.type       = S2C_STATUS_CHANGE;
						stat.object_id  = id_;
						stat.hp         = player_->_hp;
						stat.max_hp     = player_->_maxHp;
						stat.exp        = player_->_exp;
						stat.level      = player_->_level;
						stat.head_tier = player_->_head_tier;
						stat.chest_tier = player_->_chest_tier;
						stat.legs_tier = player_->_legs_tier;
						stat.boots_tier = player_->_boots_tier;
						stat.weapon_tier = player_->_weapon_tier;
						do_send(stat.size, reinterpret_cast<char*>(&stat));

						if (leveled_up) {
							send_skill_sync();
							// 주변 플레이어들에게 레벨업 및 상태 변경 브로드캐스트
							for (auto& pid : view_copy) {
								std::shared_ptr<SESSION> s = clients[pid].load();
								if (s && s->state_ == client_state::playing) {
									s->do_send(stat.size, reinterpret_cast<char*>(&stat));
								}
							}
							
							// 레벨업 이펙트를 위한 텍스트 (클라이언트에서 S2C_StatusChange의 level을 감지하여 파티클을 띄울 수 있음)
							S2C_ChatMessage lvl_msg;
							lvl_msg.size = sizeof(lvl_msg);
							lvl_msg.type = S2C_CHAT_MESSAGE;
							lvl_msg.object_id = id_; // 자신 머리 위에 출력하도록 변경
							std::string msg = "레벨 업! 현재 레벨: " + std::to_string(player_->_level);
							strncpy_s(lvl_msg.message, msg.c_str(), sizeof(lvl_msg.message));
							do_send(lvl_msg.size, reinterpret_cast<char*>(&lvl_msg));
							
							// 주변 사람들에게도 레벨업 메시지 출력
							for (auto& pid : view_copy) {
								std::shared_ptr<SESSION> s = clients[pid].load();
								if (s && s->state_ == client_state::playing) {
									s->do_send(lvl_msg.size, reinterpret_cast<char*>(&lvl_msg));
								}
							}
						}
						
						// Send inventory sync
						send_inventory_sync();

						// Send System Chat Message for loot
						if (npc->_dropItem > 0 || acquired_gold > 0) {
							S2C_ChatMessage chat_pkt;
							chat_pkt.size = sizeof(chat_pkt);
							chat_pkt.type = S2C_CHAT_MESSAGE;
							chat_pkt.object_id = -1; // System message (Unknown)
							std::string msg = "아이템 획득: ";
							if (npc->_dropItem > 0) {
								std::string item_name = "알 수 없는 아이템";
								switch (npc->_dropItem) {
									case 1: item_name = "썩은 고기"; break;
									case 2: item_name = "뼈다귀"; break;
									case 3: item_name = "화약"; break;
									case 4: item_name = "철 주괴"; break;
									case 5: item_name = "금 주괴"; break;
									case 6: item_name = "다이아몬드"; break;
									case 7: item_name = "체력 포션"; break;
									case 8: item_name = "마나 포션"; break;
									case 9: item_name = "엔더 진주"; break;
									case 10: item_name = "엔더의 눈"; break;
									case 11: item_name = "블레이즈 가루"; break;
								}
								msg += item_name + " 1개 ";
							}
							if (acquired_gold > 0) msg += "Gold " + std::to_string(acquired_gold);
							strncpy_s(chat_pkt.message, msg.c_str(), sizeof(chat_pkt.message));
							do_send(chat_pkt.size, reinterpret_cast<char*>(&chat_pkt));
						}

						// 재사용 함수를 이용해 획득 즉시 DB 안전 저장!
						save_to_db();

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
		stat.hp = player_->_hp;
		stat.max_hp = player_->_maxHp;
		stat.exp = player_->_exp;
		stat.level = player_->_level;
		stat.head_tier = player_->_head_tier;
					stat.chest_tier = player_->_chest_tier;
					stat.legs_tier = player_->_legs_tier;
					stat.boots_tier = player_->_boots_tier;
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
	case PACKET_TYPE::C2S_INTERACT_NPC:
	{
		// Find closest NPC
		auto nearby_ids = sector.get_objects_nearby_sector(player_->x_, player_->y_);
		int closest_npc_id = -1;
		int min_dist = 2; // Interact range is 1 tile (Chebyshev distance)
		int npc_visual_id = -1;

		for (auto& oid : nearby_ids) {
			if (is_npc_id(oid)) {
				auto npc = npcs[oid].load();
				if (!npc) continue;
				if (npc->_aiState != NpcState::MERCHANT) continue;

				int dist = std::max(std::abs(npc->x_ - player_->x_), std::abs(npc->y_ - player_->y_));
				if (dist <= 1 && dist < min_dist) {
					min_dist = dist;
					closest_npc_id = oid;
					npc_visual_id = npc->_visualId;
				}
			}
		}

		if (closest_npc_id != -1) {
			if (npc_visual_id == 10) {
				// Quest NPC logic
				QuestData qd = get_quest_data(player_->_quest_stage);
				S2C_ChatMessage msg_pkt;
				msg_pkt.size = sizeof(msg_pkt);
				msg_pkt.type = S2C_CHAT_MESSAGE;
				msg_pkt.object_id = closest_npc_id;

				if (qd.target_npc_type == 0) {
					std::string msg = "[퀘스트 NPC] 모든 임무를 완수했군. 자네가 최고야!";
					strncpy_s(msg_pkt.message, msg.c_str(), sizeof(msg_pkt.message));
				} else if (player_->_quest_progress >= qd.required_count) {
					if (player_->_quest_stage == 5) {
						player_->_quest_stage = 1;
						player_->_quest_progress = 0;
						send_quest_info();
						
						bool leveled_up = player_->gain_exp(50000);
						player_->_gold += 50000;
						
						S2C_StatusChange stat_pkt;
						stat_pkt.size = sizeof(stat_pkt);
						stat_pkt.type = S2C_STATUS_CHANGE;
						stat_pkt.object_id = id_;
						stat_pkt.hp = player_->_hp;
						stat_pkt.max_hp = player_->_maxHp;
						stat_pkt.exp = player_->_exp;
						stat_pkt.level = player_->_level;
						stat_pkt.head_tier = player_->_head_tier;
						stat_pkt.chest_tier = player_->_chest_tier;
						stat_pkt.legs_tier = player_->_legs_tier;
						stat_pkt.boots_tier = player_->_boots_tier;
						stat_pkt.weapon_tier = player_->_weapon_tier;
						do_send(stat_pkt.size, reinterpret_cast<char*>(&stat_pkt));
						
						send_inventory_sync();
						if (leveled_up) {
							send_skill_sync();
							S2C_ChatMessage lvl_msg;
							lvl_msg.size = sizeof(lvl_msg);
							lvl_msg.type = S2C_CHAT_MESSAGE;
							lvl_msg.object_id = 0;
							std::string lm = "[System] Level Up! You reached level " + std::to_string(player_->_level) + "!";
							strncpy_s(lvl_msg.message, lm.c_str(), sizeof(lvl_msg.message));
							do_send(lvl_msg.size, reinterpret_cast<char*>(&lvl_msg));
							
							S2C_StatusChange stat_change;
							stat_change.size = sizeof(stat_change);
							stat_change.type = S2C_STATUS_CHANGE;
							stat_change.object_id = id_;
							stat_change.hp = player_->_hp;
							stat_change.max_hp = player_->_maxHp;
							stat_change.exp = player_->_exp;
							stat_change.level = player_->_level;
							stat_change.head_tier = player_->_head_tier;
							stat_change.chest_tier = player_->_chest_tier;
							stat_change.legs_tier = player_->_legs_tier;
							stat_change.boots_tier = player_->_boots_tier;
							stat_change.weapon_tier = player_->_weapon_tier;
							
							auto nearby = sector.get_objects_nearby_sector(player_->x_, player_->y_);
							for (auto pid : nearby) {
								if (is_npc_id(pid) || pid == id_) continue;
								auto s = clients[pid].load();
								if (s && s->state_ == client_state::playing) s->do_send(stat_change.size, reinterpret_cast<char*>(&stat_change));
							}
						}
						
						std::string msg = "[퀘스트 NPC] 세상을 구했군! 대량의 보상을 주지! 처음부터 다시 시작할까?";
						strncpy_s(msg_pkt.message, msg.c_str(), sizeof(msg_pkt.message));
					} else {
						player_->_quest_stage++;
						player_->_quest_progress = 0;
						send_quest_info();
						
						std::string msg = "[퀘스트 NPC] 훌륭해! 다음 목표를 주지!";
						strncpy_s(msg_pkt.message, msg.c_str(), sizeof(msg_pkt.message));
					}
				} else {
					std::string msg = "[퀘스트 NPC] 아직 목표를 다 채우지 못했군. 서둘러!";
					strncpy_s(msg_pkt.message, msg.c_str(), sizeof(msg_pkt.message));
				}
				do_send(msg_pkt.size, reinterpret_cast<char*>(&msg_pkt));
			} else {
				S2C_OpenTradeUI trade_pkt;
				trade_pkt.size = sizeof(trade_pkt);
				trade_pkt.type = PACKET_TYPE::S2C_OPEN_TRADE_UI;
				trade_pkt.npc_id = closest_npc_id;
				trade_pkt.visual_id = npc_visual_id;
				do_send(trade_pkt.size, reinterpret_cast<char*>(&trade_pkt));
			}
		}
		break;
	}
	case PACKET_TYPE::C2S_TRADE_COMMAND:
	{
		C2S_TradeCommand* pkt = reinterpret_cast<C2S_TradeCommand*>(buff);
		auto npc = npcs[pkt->npc_id].load();
		if (!npc || npc->_aiState != NpcState::MERCHANT) break;

		int dist = std::max(std::abs(npc->x_ - player_->x_), std::abs(npc->y_ - player_->y_));
		if (dist > 5) break; // Too far

		int v_id = npc->_visualId;
		bool success = false;
		std::string msg = "";

		if (v_id == 6) { // Priest
			if (pkt->trade_index == 1) { // Sell Rotten Flesh
				if (player_->inventory[static_cast<int>(ItemType::ROTTEN_FLESH)] >= 10) {
					player_->inventory[static_cast<int>(ItemType::ROTTEN_FLESH)] -= 10;
					player_->_gold += 50;
					success = true;
					msg = "성직자: 훌륭한 전리품이군요. 50 골드를 드리겠습니다.";
				} else { msg = "성직자: 썩은 고기가 부족합니다 (10개 필요)."; }
			} else if (pkt->trade_index == 2) { // Sell Bone
				if (player_->inventory[static_cast<int>(ItemType::BONE)] >= 5) {
					player_->inventory[static_cast<int>(ItemType::BONE)] -= 5;
					player_->_gold += 100;
					success = true;
					msg = "성직자: 훌륭한 전리품이군요. 100 골드를 드리겠습니다.";
				} else { msg = "성직자: 뼈다귀가 부족합니다 (5개 필요)."; }
			} else if (pkt->trade_index == 3) { // Sell Gunpowder
				if (player_->inventory[static_cast<int>(ItemType::GUNPOWDER)] >= 3) {
					player_->inventory[static_cast<int>(ItemType::GUNPOWDER)] -= 3;
					player_->_gold += 150;
					success = true;
					msg = "성직자: 위험한 화약이군요. 150 골드를 드리겠습니다.";
				} else { msg = "성직자: 화약이 부족합니다 (3개 필요)."; }
			} else if (pkt->trade_index == 4) { // Sell Iron Ingot
				if (player_->inventory[static_cast<int>(ItemType::IRON_INGOT)] >= 1) {
					player_->inventory[static_cast<int>(ItemType::IRON_INGOT)] -= 1;
					player_->_gold += 200;
					success = true;
					msg = "성직자: 귀한 철괴군요. 200 골드를 드리겠습니다.";
				} else { msg = "성직자: 철괴가 부족합니다 (1개 필요)."; }
			} else if (pkt->trade_index == 5) { // Sell Diamond
				if (player_->inventory[static_cast<int>(ItemType::DIAMOND)] >= 1) {
					player_->inventory[static_cast<int>(ItemType::DIAMOND)] -= 1;
					player_->_gold += 500;
					success = true;
					msg = "성직자: 눈부신 다이아몬드군요! 500 골드를 드리겠습니다.";
				} else { msg = "성직자: 다이아몬드가 부족합니다 (1개 필요)."; }
			} else if (pkt->trade_index == 6) { // Buy Potion
				if (player_->_gold >= 30) {
					player_->_gold -= 30;
					player_->inventory[static_cast<int>(ItemType::HEALTH_POTION)] += 1;
					success = true;
					msg = "성직자: 신의 축복이 담긴 포션입니다.";
				} else { msg = "성직자: 골드가 부족합니다 (30G 필요)."; }
			}
		} else if (v_id == 7) { // Armorer
			auto try_upgrade_armor = [&](int& current_tier, const std::string& part_name) {
				int next_tier = current_tier + 1;
				if (next_tier > 4) { msg = "대장장이: 이미 최고 등급의 " + part_name + "을(를) 입고 있군!"; }
				else {
					int cost = next_tier * 150;
					if (player_->_gold >= cost) {
						player_->_gold -= cost;
						current_tier = next_tier;
						success = true;
						msg = "대장장이: 새 " + part_name + "이(가) 아주 잘 어울리는군!";
					} else { msg = "대장장이: 골드가 부족하네 (" + std::to_string(cost) + "G 필요)."; }
				}
			};

			if (pkt->trade_index == 1) {
				try_upgrade_armor(player_->_head_tier, "투구");
			} else if (pkt->trade_index == 2) {
				try_upgrade_armor(player_->_chest_tier, "흉갑");
			} else if (pkt->trade_index == 3) {
				try_upgrade_armor(player_->_legs_tier, "바지");
			} else if (pkt->trade_index == 4) {
				try_upgrade_armor(player_->_boots_tier, "부츠");
			}
		} else if (v_id == 8) { // Weaponsmith
			if (pkt->trade_index == 1) {
				int next_tier = player_->_weapon_tier + 1;
				if (next_tier > 6) { msg = "무기장인: 그 무기는 더 이상 단련할 수 없다!"; }
				else {
					int cost = next_tier * 100;
					int level_req = next_tier * 5;
					if (player_->_level < level_req) {
						msg = "무기장인: 레벨이 너무 낮군! (" + std::to_string(level_req) + " 이상 필요)";
					} else if (player_->_gold >= cost) {
						player_->_gold -= cost;
						player_->_weapon_tier = next_tier;
						success = true;
						msg = "무기장인: 아주 날카롭게 벼려두었다!";
					} else { msg = "무기장인: 골드가 부족하다 (" + std::to_string(cost) + "G 필요)."; }
				}
			}
		} else if (v_id == 9) { // Librarian (Enchanter)
			if (pkt->trade_index == 1) {
				if (player_->_weapon_tier < 1) { msg = "사서: 빈손에는 마법을 부여할 수 없어요."; }
				else if (player_->_gold >= 500) {
					player_->_gold -= 500;
					int chance = rand() % 100;
					if (chance < 30) { // 30% chance
						if (player_->_weapon_tier < 6) {
							player_->_weapon_tier++;
							success = true;
							msg = "사서: 놀랍군요! 마법 부여에 완벽히 성공했습니다!";
						} else { msg = "사서: 앗, 이미 더 이상 부여할 수 없는 상태네요."; }
					} else {
						msg = "사서: 이런... 마법 부여에 실패하고 말았습니다.";
					}
					// Always send status update because gold changed
					success = true; 
				} else { msg = "사서: 연구 기금(골드)이 부족하네요 (500G 필요)."; }
			}
		}

		if (success) {
			send_inventory_sync();
			S2C_StatusChange stat;
			stat.size = sizeof(stat);
			stat.type = PACKET_TYPE::S2C_STATUS_CHANGE;
			stat.object_id = id_;
			stat.hp = player_->_hp;
			stat.max_hp = player_->_maxHp;
			stat.exp = player_->_exp;
			stat.level = player_->_level;
			stat.head_tier = player_->_head_tier;
					stat.chest_tier = player_->_chest_tier;
					stat.legs_tier = player_->_legs_tier;
					stat.boots_tier = player_->_boots_tier;
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
			save_to_db(); // DB 저장
		}

		S2C_TradeResult res_pkt;
		res_pkt.size = sizeof(res_pkt);
		res_pkt.type = PACKET_TYPE::S2C_TRADE_RESULT;
		res_pkt.success = success;
		strncpy_s(res_pkt.message, msg.c_str(), sizeof(res_pkt.message) - 1);
		do_send(res_pkt.size, reinterpret_cast<char*>(&res_pkt));

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

void SESSION::force_teleport(int tx, int ty) {
	int old_x = player_->x_;
	int old_y = player_->y_;
	player_->x_ = tx;
	player_->y_ = ty;
	sector.move_object(id_, old_x, old_y, tx, ty);

	S2C_UpdatePosition tp_pkt;
	tp_pkt.size = sizeof(tp_pkt);
	tp_pkt.type = S2C_UPDATE_POSITION;
	tp_pkt.x = tx;
	tp_pkt.y = ty;
	do_send(tp_pkt.size, reinterpret_cast<char*>(&tp_pkt));

	auto old_view = visible_players;
	std::unordered_set<int> new_visible_players;
	auto object_ids_nearby = sector.get_objects_nearby_sector(tx, ty);
	
	for (auto& id : object_ids_nearby) {
		if (id == id_) continue;
		if (is_npc_id(id)) {
			auto npc = npcs[id].load();
			if (is_visible(npc->x_, npc->y_, npc->_npcType)) new_visible_players.insert(id);
		} else {
			auto it = clients.find(id);
			if (it == clients.end()) continue;
			std::shared_ptr<SESSION> s = it->second.load();
			if (s && s->state_ == client_state::playing) {
				if (s->is_visible(tx, ty)) new_visible_players.insert(id);
			}
		}
	}

	for (auto& oid : new_visible_players) {
		if (!old_view.contains(oid)) {
			if (is_npc_id(oid)) {
				auto npc = npcs[oid].load();
				S2C_AddObject add_pkt;
				add_pkt.size = sizeof(add_pkt);
				add_pkt.type = PACKET_TYPE::S2C_ADD_OBJECT;
				add_pkt.object_id = oid;
				add_pkt.x = npc->x_;
				add_pkt.y = npc->y_;
				strncpy_s(add_pkt.obj_name, npc->userName_, sizeof(add_pkt.obj_name));
				add_pkt.visual_id = npc->_visualId;
				add_pkt.hp        = npc->_hp;
				add_pkt.max_hp    = npc->_maxHp;
				add_pkt.exp       = npc->_exp;
				add_pkt.level     = npc->_level;
				add_pkt.head_tier = 0; add_pkt.chest_tier = 0; add_pkt.legs_tier = 0;
				add_pkt.boots_tier = 0; add_pkt.weapon_tier = 0;
				add_pkt.npc_state = static_cast<char>(npc->_aiState);
				do_send(add_pkt.size, reinterpret_cast<char*>(&add_pkt));
				npc->wake_up();
			} else {
				std::shared_ptr<SESSION> s = clients[oid].load();
				if (s && s->state_ == client_state::playing) {
					s->send_add_player(id_);
					send_add_player(oid);
				}
			}
		}
	}

	for (auto& oid : old_view) {
		if (!new_visible_players.contains(oid)) {
			S2C_RemoveObject rm_pkt;
			rm_pkt.size = sizeof(rm_pkt);
			rm_pkt.type = S2C_REMOVE_OBJECT;
			rm_pkt.object_id = oid;
			do_send(rm_pkt.size, reinterpret_cast<char*>(&rm_pkt));
			
			if (!is_npc_id(oid)) {
				auto it = clients.find(oid);
				if (it != clients.end()) {
					std::shared_ptr<SESSION> s = it->second.load();
					if (s && s->state_ == client_state::playing) {
						s->visible_players_mutex.lock();
						s->visible_players.erase(id_);
						s->visible_players_mutex.unlock();
						
						S2C_RemoveObject rm_me;
						rm_me.size = sizeof(rm_me);
						rm_me.type = S2C_REMOVE_OBJECT;
						rm_me.object_id = id_;
						s->do_send(rm_me.size, reinterpret_cast<char*>(&rm_me));
					}
				}
			}
		}
	}

	visible_players_mutex.lock();
	visible_players = new_visible_players;
	visible_players_mutex.unlock();
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
	add_packet.head_tier = session->player_->_head_tier;
					add_packet.chest_tier = session->player_->_chest_tier;
					add_packet.legs_tier = session->player_->_legs_tier;
					add_packet.boots_tier = session->player_->_boots_tier;
	add_packet.weapon_tier = session->player_->_weapon_tier;
	add_packet.dir_x = session->player_->_dir_x;
	add_packet.dir_y = session->player_->_dir_y;
	add_packet.hp = session->player_->_hp;
	add_packet.max_hp = session->player_->_maxHp;
	add_packet.exp = session->player_->_exp;
	add_packet.level = session->player_->_level;
	add_packet.visual_id = 0;

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
				if (is_visible(npc->x_, npc->y_, npc->_npcType)) {
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
					add_pkt.head_tier = 0;
					add_pkt.chest_tier = 0;
					add_pkt.legs_tier = 0;
					add_pkt.boots_tier = 0;
					add_pkt.weapon_tier = 0;
					add_pkt.dir_x = 0;
					add_pkt.dir_y = -1;
					add_pkt.npc_state = static_cast<char>(npc->_aiState);
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
	// 동시성 제어: 이미 nullptr로 바뀌었다면 다른 스레드에서 해제 중이므로 무시
	std::shared_ptr<SESSION> cl = clients[client_id].exchange(nullptr);
	if (cl == nullptr) return; 

	std::cout << "client[" << client_id << "] Disconnected.\n";
	
	if (cl->player_) {
		cl->state_ = client_state::logout;
		
		// 간편하게 DB 저장 호출!
		cl->save_to_db();

		broadcast_player_remove_packet(cl->id_);
		sector.remove_object(cl->id_, cl->player_->x_, cl->player_->y_);
		closesocket(cl->client_);
		cl->client_ = INVALID_SOCKET;
	}
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

	// ── Step 1.5. AI 스크립트 경로 등록 (worker_thread에서 lazy-init) ─────
	const std::string ai_dir = "ai/";
	NpcAiManager::register_script(1, ai_dir + "zombie_ai.lua");
	NpcAiManager::register_script(2, ai_dir + "skeleton_ai.lua");
	NpcAiManager::register_script(3, ai_dir + "creeper_ai.lua");
	NpcAiManager::register_script(4, ai_dir + "enderman_ai.lua");
	NpcAiManager::register_script(5, ai_dir + "iron_golem_ai.lua");
	NpcAiManager::register_script(11, ai_dir + "ender_dragon_ai.lua");
	NpcAiManager::register_script(12, ai_dir + "fire_zone_ai.lua");
	std::cout << "[Server] NPC AI scripts registered." << std::endl;

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
	auto merchant_entries = load_merchant_spawns_lua(lua_path.c_str());
	spawn_entries.insert(spawn_entries.end(), merchant_entries.begin(), merchant_entries.end());

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
		npc->_level    = static_cast<unsigned char>(m.level);
		npc->_exp      = m.exp;
		npc->_attack   = m.attack;
		npc->_dropItem = m.drop_item;
		npc->_dropGold = m.drop_gold;
		if (entry.type_id >= 6 && entry.type_id <= 10) {
			npc->_aiState = NpcState::MERCHANT;
		} else {
			npc->_aiState = NpcState::IDLE;
		}
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
				case EVENT_PROJECTILE:
				{
					EXP_OVER* proj_over = new EXP_OVER;
					proj_over->type = io_type::npc_projectile;
					proj_over->event_data = top_ev;
					PostQueuedCompletionStatus(h_iocp, -1, top_ev.obj_id, &proj_over->over);
				}
				break;
				case EVENT_PLAYER_RESPAWN:
				{
					EXP_OVER* p_respawn_over = new EXP_OVER;
					p_respawn_over->type = io_type::player_respawn;
					PostQueuedCompletionStatus(h_iocp, -1, top_ev.obj_id, &p_respawn_over->over);
				}
				break;
				case EVENT_NPC_FIRE_DAMAGE:
				{
					EXP_OVER* fire_over = new EXP_OVER;
					fire_over->type = io_type::npc_fire_damage;
					fire_over->event_data = top_ev;
					PostQueuedCompletionStatus(h_iocp, -1, top_ev.obj_id, &fire_over->over);
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
	// 이 스레드의 thread_local Lua AI 상태 초기화
	NpcAiManager::init_thread_states();

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
			}
			clients.emplace(current_id, new_session);
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
			if (!npc)
				break;

			int delay_ms = npc->do_timer_move();

			// AI DIE 액션(크리퍼 자폭 등) 처리
			if (npc->_hp <= 0) {
				npc->is_active = false;
				// 주변 플레이어에게 제거 패킷 브로드캐스트
				auto nearby = sector.get_objects_nearby_sector(npc->x_, npc->y_);
				S2C_RemoveObject rm_pkt;
				rm_pkt.size = sizeof(rm_pkt);
				rm_pkt.type = S2C_REMOVE_OBJECT;
				rm_pkt.object_id = id;
				for (auto pid : nearby) {
					if (is_npc_id(pid)) continue;
					std::shared_ptr<SESSION> s = clients[pid].load();
					if (s && s->state_ == client_state::playing)
						s->do_send(rm_pkt.size, reinterpret_cast<char*>(&rm_pkt));
				}
				sector.remove_object(npc->id_, npc->x_, npc->y_);
				if (npc->_npcType != NPC_FIRE_ZONE) {
					// 5초 뒤 리스폰
					event_type ev;
					ev.obj_id = id;
					ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::seconds(5);
					ev.event_id = EVENT_RESPAWN;
					ev.target_id = -1;
					timer_queue.push(ev);
				}
				break;
			}

			if (delay_ms > 0)
			{
				event_type ev;
				ev.obj_id = id;
				ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::milliseconds(delay_ms);
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
					
					// 주변 플레이어에게 NPC 등장(리스폰) 패킷 전송
					S2C_AddObject add_pkt;
					add_pkt.size = sizeof(add_pkt);
					add_pkt.type = PACKET_TYPE::S2C_ADD_OBJECT;
					add_pkt.object_id = npc->id_;
					add_pkt.x = npc->x_;
					add_pkt.y = npc->y_;
					strcpy_s(add_pkt.obj_name, npc->userName_);
					add_pkt.visual_id = npc->_visualId;
					add_pkt.hp        = npc->_hp;
					add_pkt.max_hp    = npc->_maxHp;
					add_pkt.exp       = npc->_exp;
					add_pkt.level     = npc->_level;
					add_pkt.head_tier = 0;
					add_pkt.chest_tier = 0;
					add_pkt.legs_tier = 0;
					add_pkt.boots_tier = 0;
					add_pkt.weapon_tier = 0;
					add_pkt.dir_x = 0;
					add_pkt.dir_y = -1;
					add_pkt.npc_state = static_cast<char>(npc->_aiState);
					
					for (auto pid : sector.get_objects_nearby_sector(npc->x_, npc->y_)) {
						if (!is_npc_id(pid)) {
							std::shared_ptr<SESSION> s = clients[pid].load();
							if (s && s->state_ == client_state::playing) {
								if (s->is_visible(npc->x_, npc->y_, npc->_npcType)) {
									s->visible_players_mutex.lock();
									s->visible_players.insert(npc->id_);
									s->visible_players_mutex.unlock();
									s->do_send(add_pkt.size, reinterpret_cast<char*>(&add_pkt));
								}
							}
						}
					}

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
		case io_type::npc_fire_damage:
		{
			event_type ev = o->event_data;
			delete o;
			auto npc = npcs[ev.obj_id].load();
			if (npc && npc->is_active) {
				npc->fire_stacks_--;
				if (npc->fire_stacks_.load() < 0) npc->fire_stacks_ = 0;
				npc->_hp -= ev.attack_power;
				
				S2C_StatusChange stat;
				stat.size       = sizeof(stat);
				stat.type       = S2C_STATUS_CHANGE;
				stat.object_id  = ev.obj_id;
				stat.hp         = npc->_hp;
				stat.max_hp     = npc->_maxHp;
				stat.exp        = npc->_exp;
				stat.level      = npc->_level;
				stat.head_tier = 0;
				stat.chest_tier = 0;
				stat.legs_tier = 0;
				stat.boots_tier = 0;
				stat.weapon_tier = 0;

				S2C_ChatMessage chat;
				chat.size = sizeof(chat);
				chat.type = S2C_CHAT_MESSAGE;
				chat.object_id = ev.obj_id;
				std::string dot_msg = "Fire aspect -" + std::to_string(ev.attack_power);
				strncpy_s(chat.message, dot_msg.c_str(), sizeof(chat.message));

				auto nearby = sector.get_objects_nearby_sector(npc->x_, npc->y_);
				for (auto pid : nearby) {
					if (!is_npc_id(pid)) {
						std::shared_ptr<SESSION> s = clients[pid].load();
						if (s && s->state_ == client_state::playing) {
							s->do_send(stat.size, reinterpret_cast<char*>(&stat));
							s->do_send(chat.size, reinterpret_cast<char*>(&chat));
						}
					}
				}

				if (npc->_hp <= 0) {
					npc->is_active = false;
					
					// Reward to attacker if possible
					std::shared_ptr<SESSION> attacker = clients[ev.target_id].load();
					if (attacker && attacker->player_) {
						if (npc->_dropItem > 0) attacker->player_->add_item(npc->_dropItem, 1);
						if (npc->_dropGold > 0) attacker->player_->_gold += (rand() % npc->_dropGold + 1);
						bool leveled_up = attacker->player_->gain_exp(static_cast<int>(npc->_exp));
						if (leveled_up) {
							attacker->send_skill_sync();
						}
					}

					S2C_RemoveObject rm_pkt;
					rm_pkt.size = sizeof(rm_pkt);
					rm_pkt.type = S2C_REMOVE_OBJECT;
					rm_pkt.object_id = ev.obj_id;
					for (auto pid : nearby) {
						if (is_npc_id(pid)) continue;
						std::shared_ptr<SESSION> s = clients[pid].load();
						if (s && s->state_ == client_state::playing)
							s->do_send(rm_pkt.size, reinterpret_cast<char*>(&rm_pkt));
					}
					sector.remove_object(npc->id_, npc->x_, npc->y_);
					
					event_type rev;
					rev.obj_id = ev.obj_id;
					rev.wakeup_time = std::chrono::system_clock::now() + std::chrono::seconds(1);
					rev.event_id = EVENT_RESPAWN;
					rev.target_id = -1;
					timer_queue.push(rev);
				}
			}
			break;
		}
		case io_type::player_respawn:
		{
			delete o;
			std::shared_ptr<SESSION> s = clients[id].load();
			if (s && s->player_ && s->state_ == client_state::playing)
			{
				int old_x, old_y;
				{
					PlayerState expected = PlayerState::DEAD;
					s->player_->_state.compare_exchange_strong(expected, PlayerState::ALIVE);
					
					s->player_->_hp = s->player_->_maxHp;
					old_x = s->player_->x_;
					old_y = s->player_->y_;
					s->player_->x_ = 1000;
					s->player_->y_ = 1000;
				}
				
				sector.move_object(s->id_, old_x, old_y, 1000, 1000);
				
				S2C_StatusChange stat;
				stat.size       = sizeof(stat);
				stat.type       = S2C_STATUS_CHANGE;
				stat.object_id  = s->id_;
				stat.hp         = s->player_->_hp;
				stat.max_hp     = s->player_->_maxHp;
				stat.exp        = s->player_->_exp;
				stat.level      = s->player_->_level;
				stat.head_tier = s->player_->_head_tier;
					stat.chest_tier = s->player_->_chest_tier;
					stat.legs_tier = s->player_->_legs_tier;
					stat.boots_tier = s->player_->_boots_tier;
				stat.weapon_tier = s->player_->_weapon_tier;
				s->do_send(stat.size, reinterpret_cast<char*>(&stat));

				S2C_MoveObject move_packet;
				move_packet.size = sizeof(S2C_MoveObject);
				move_packet.type = PACKET_TYPE::S2C_MOVE_OBJECT;
				move_packet.object_id = s->id_;
				move_packet.x = s->player_->x_;
				move_packet.y = s->player_->y_;
				move_packet.dir_x = s->player_->_dir_x;
				move_packet.dir_y = s->player_->_dir_y;
				move_packet.move_time = 0;
				s->do_send(move_packet.size, reinterpret_cast<char*>(&move_packet));
				
				// Recompute visibility
				auto old_view = s->visible_players;
				std::unordered_set<int> new_visible_players;
				auto object_ids_nearby_sector = sector.get_objects_nearby_sector(s->player_->x_, s->player_->y_);
				for (auto& oid : object_ids_nearby_sector)
				{
					if (oid == s->id_) continue;

					if (is_npc_id(oid)) {
						auto npc = npcs[oid].load();
						if (s->is_visible(npc->x_, npc->y_, npc->_npcType)) {
							new_visible_players.insert(oid);
						}
						continue;
					}

					auto it = clients.find(oid);
					if (it == clients.end()) continue;
					std::shared_ptr<SESSION> other_s = it->second.load();
					if (!other_s || other_s->state_ != client_state::playing) continue;

					if (other_s->is_visible(s->player_->x_, s->player_->y_))
					{
						new_visible_players.insert(oid);
					}
				}

				for (auto& oid : new_visible_players)
				{
					if (!old_view.contains(oid))
					{
						if (is_npc_id(oid)) {
							auto npc = npcs[oid].load();
							S2C_AddObject add_pkt;
							add_pkt.size = sizeof(add_pkt);
							add_pkt.type = PACKET_TYPE::S2C_ADD_OBJECT;
							add_pkt.object_id = oid;
							add_pkt.x = npc->x_;
							add_pkt.y = npc->y_;
							strcpy_s(add_pkt.obj_name, npc->userName_);
							add_pkt.visual_id = npc->_visualId;
							add_pkt.hp        = npc->_hp;
							add_pkt.max_hp    = npc->_maxHp;
							add_pkt.exp       = npc->_exp;
							add_pkt.level     = npc->_level;
							add_pkt.head_tier = 0;
							add_pkt.chest_tier = 0;
							add_pkt.legs_tier = 0;
							add_pkt.boots_tier = 0;
							add_pkt.weapon_tier = 0;
							add_pkt.dir_x = 0;
							add_pkt.dir_y = -1;
							add_pkt.npc_state = static_cast<char>(npc->_aiState);
							s->do_send(add_pkt.size, reinterpret_cast<char*>(&add_pkt));

							npc->wake_up();

							s->visible_players_mutex.lock();
							s->visible_players.insert(oid);
							s->visible_players_mutex.unlock();
						}
						else {
							s->send_add_player(oid);
							std::shared_ptr<SESSION> other_s = clients[oid].load();
							if (other_s) other_s->send_add_player(s->id_);
						}
					}
					else
					{
						if (!is_npc_id(oid)) {
							std::shared_ptr<SESSION> other_s = clients[oid].load();
							if (other_s && other_s->state_ == client_state::playing)
							{
								other_s->send_move_packet(s->id_, 0);
							}
						}
					}
				}

				for (auto& oid : old_view)
				{
					if (!new_visible_players.contains(oid))
					{
						if (is_npc_id(oid)) {
							S2C_RemoveObject remove_pkt;
							remove_pkt.size = sizeof(remove_pkt);
							remove_pkt.type = PACKET_TYPE::S2C_REMOVE_OBJECT;
							remove_pkt.object_id = oid;
							s->do_send(remove_pkt.size, reinterpret_cast<char*>(&remove_pkt));

							s->visible_players_mutex.lock();
							s->visible_players.erase(oid);
							s->visible_players_mutex.unlock();
						}
						else {
							s->send_remove_player(oid);
							std::shared_ptr<SESSION> other_s = clients[oid].load();
							if (other_s) other_s->send_remove_player(s->id_);
						}
					}
				}
			}
			break;
		}
		case io_type::npc_projectile:
		{
			event_type ev = o->event_data;
			delete o;

			int cx = ev.start_x + ev.dir_x * ev.step;
			int cy = ev.start_y + ev.dir_y * ev.step;
			
			bool hit_wall = !is_passable(cx, cy);

			S2C_AttackEffect eff;
			eff.size = sizeof(eff);
			eff.type = S2C_ATTACK_EFFECT;
			eff.object_id = ev.obj_id;
			eff.weapon_tier = 0;
			eff.attack_type = (ev.proj_type == 1) ? 7 : 4; // 7 for fireball, 4 for arrow
			eff.x = cx;
			eff.y = cy;
			eff.dir_x = ev.dir_x;
			eff.dir_y = ev.dir_y;
			
			bool hit_player = false;
			auto nearby = sector.get_objects_nearby_sector(cx, cy);
			for (auto pid : nearby) {
				if (is_npc_id(pid)) continue;
				auto ps = clients[pid].load();
				if (ps && ps->state_ == client_state::playing) {
					ps->do_send(eff.size, reinterpret_cast<char*>(&eff));
					
					if (ps->player_->x_ == cx && ps->player_->y_ == cy) {
						hit_player = true;
						int def = ps->player_->get_defense();
						int actual_dmg = std::max(1, ev.attack_power - def);
						player_take_damage(ps, actual_dmg);
					}
				}
			}

			int max_steps = (ev.proj_type == 1) ? 8 : 5;
			bool exploded = false;

			if (hit_player || hit_wall || ev.step >= max_steps) {
				exploded = true;
			}

			if (exploded) {
				if (ev.proj_type == 1) { // Fireball 폭발 시 불 장판 생성
					int fire_zone_id = make_npc_id(npc_index++);
					auto npc = std::make_shared<NPC>();
					npc->id_ = fire_zone_id;
					npc->x_ = cx;
					npc->y_ = cy;
					npc->_aiState = NpcState::IDLE;
					npc->_hp = 3;
					npc->_maxHp = 3;
					npc->_visualId = 12; // 12: NPC_FIRE_ZONE
					npc->_npcType = NPC_FIRE_ZONE; // 12번 타입 (fire_zone_ai.lua 사용)
					npc->_attack = std::max(1, ev.attack_power / 2); // 도트 딜
					strncpy_s(npc->userName_, "FireZone", sizeof(npc->userName_) - 1);
					
					npcs[fire_zone_id] = npc;
					sector.add_object(fire_zone_id, cx, cy);

					event_type move_ev;
					move_ev.obj_id = fire_zone_id;
					move_ev.event_id = EVENT_MOVE;
					move_ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::milliseconds(1000);
					timer_queue.push(move_ev);
				}
			} else {
				ev.step++;
				ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::milliseconds((ev.proj_type == 1) ? 100 : 200);
				timer_queue.push(ev);
			}
			break;
		}
		case io_type::db_save_position:
		{
			delete o;
			break;
		}
		case io_type::db_player_auth:
		{
			auto new_session = clients[id].load();
			if (new_session)
			{
				DBTask task = o->db_task;
				if (std::holds_alternative<player_data>(task.data))
				{
					player_data result = std::get<player_data>(task.data);
					if (result.success)
					{
						new_session->player_->x_ = result.x;
						new_session->player_->y_ = result.y;
						new_session->player_->_head_tier = result.head_tier;
						new_session->player_->_chest_tier = result.chest_tier;
						new_session->player_->_legs_tier = result.legs_tier;
						new_session->player_->_boots_tier = result.boots_tier;
						new_session->player_->_weapon_tier = result.weapon_tier;
						new_session->player_->_hp = result.hp;
						new_session->player_->_maxHp = result.max_hp;
						new_session->player_->_level = result.level;
						new_session->player_->_exp = result.exp;
						new_session->player_->_gold = result.gold;
						new_session->player_->inventory = result.inventory;
						// Update max HP if armor affects it, but since DB stores max_hp, we use DB value unless we want to recalculate
						// new_session->player_->update_max_hp();
						
						strncpy_s(new_session->userId_, sizeof(new_session->userId_), result.user_id, sizeof(new_session->userId_) - 1);
						strncpy_s(new_session->player_->userName_, sizeof(new_session->player_->userName_), result.user_name, MAX_NAME_LEN);
						
						new_session->player_->_quest_stage = result.quest_stage;
						new_session->player_->_quest_progress = result.quest_progress;
						new_session->player_->_unspent_sp = result.unspent_sp;
						new_session->player_->_skills_mask = result.skills_mask;

						new_session->send_login_success();
						sector.add_object(id, new_session->player_->x_, new_session->player_->y_);
						new_session->send_avatar_info();
						new_session->send_inventory_sync();
						new_session->send_quest_info();
						new_session->send_skill_sync();
						new_session->send_already_spawn_players();
						broadcast_new_player(id);
						new_session->state_ = client_state::playing;
						std::cout << "Player[" << id << "] authenticated successfully. Position: (" << result.x << ", " << result.y << ")\n";
					}
					else
					{
						new_session->send_login_failure();
					}
				}
				else
				{
					new_session->send_login_failure();
				}
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
	setlocale(LC_ALL, "korean");
	srand(static_cast<unsigned int>(time(NULL)));
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	server_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;

	std::cout << "Attempting to bind to PORT: " << PORT << " (htons: " << htons(PORT) << ")\n";
	int64_t ret = bind(server_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	if (ret == SOCKET_ERROR) {
		error_display(L"bind failed", WSAGetLastError());
		system("pause");
		exit(-1);
	}

	if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
		error_display(L"listen failed", WSAGetLastError());
		system("pause");
		exit(-1);
	}

	h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	CreateIoCompletionPort((HANDLE)server_socket, h_iocp, 0, 0);

	DBManager::Instance().Init([](int session_id, int io_type_val, const DBTask& task) {
		EXP_OVER* over = new EXP_OVER;
		over->type = static_cast<io_type>(io_type_val);
		over->db_task = task;
		PostQueuedCompletionStatus(h_iocp, 1, session_id, &over->over);
	});
	DBManager::Instance().StartDBThread();

	std::cout << "NPC 초기화 중..." << std::endl;
	npc_initialize();
	std::cout << "NPC 초기화 완료. NPC Count: " << npcs.size() << std::endl;

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

	std::cout << "Waiting for DB Thread to finish saving data...\n";
	DBManager::Instance().StopDBThread();

	closesocket(server_socket);
	WSACleanup();
}
