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

constexpr int EVENT_MOVE = -1;
constexpr int EVENT_RESPAWN = -2;
constexpr int EVENT_PROJECTILE = -3;
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
	std::vector<WSABUF> wsa_bufs;
	std::vector<std::vector<char>> pending_data;
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
	int _level = 1;
	int _exp = 0;

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

	int get_defense() const {
		char tier = get_armor_tier();
		switch (tier) {
			case 1: return 5;
			case 2: return 10;
			case 3: return 20;
			case 4: return 30;
			default: return 0;
		}
	}

	int get_required_exp() const {
		return _level * _level * 100;
	}

	bool gain_exp(int amount) {
		bool leveled_up = false;
		_exp += amount;
		while (_exp >= get_required_exp()) {
			_exp -= get_required_exp();
			_level++;
			leveled_up = true;
		}
		if (leveled_up) {
			update_max_hp();
			_hp = _maxHp; // 레벨업 시 체력 비율 상관없이 100% 회복
		}
		return leveled_up;
	}

	void update_max_hp() {
		char tier = get_armor_tier();
		int new_max = 100 + (_level - 1) * 20; // 레벨당 20 증가
		switch (tier) {
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
	bool is_visible(int x, int y) {
		return abs(x - player_->x_) <= VIEW_RANGE && abs(y - player_->y_) <= VIEW_RANGE;
	}
	void send_already_spawn_players();
	
	void save_to_db()
	{
		if (!player_) return;
		
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
		pd.armor_tier = static_cast<int>(player_->get_armor_tier());
		pd.weapon_tier = static_cast<int>(player_->_weapon_tier);
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
			stat.armor_tier = 0;
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
				if (is_passable(nx, ny)) { x_ = nx; y_ = ny; }
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
					s->player_->_hp -= actual_dmg;
					
					S2C_StatusChange stat;
					stat.size       = sizeof(stat);
					stat.type       = S2C_STATUS_CHANGE;
					stat.object_id  = action.target_id;
					stat.hp         = s->player_->_hp;
					stat.max_hp     = s->player_->_maxHp;
					stat.exp        = 0;
					stat.level      = 1;
					stat.armor_tier = s->player_->get_armor_tier();
					stat.weapon_tier = s->player_->_weapon_tier;
					s->do_send(stat.size, reinterpret_cast<char*>(&stat));
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
					s->player_->_hp -= actual_dmg; // 자폭 데미지
					
					S2C_StatusChange stat;
					stat.size       = sizeof(stat);
					stat.type       = S2C_STATUS_CHANGE;
					stat.object_id  = pid;
					stat.hp         = s->player_->_hp;
					stat.max_hp     = s->player_->_maxHp;
					stat.exp        = s->player_->_exp;
					stat.level      = s->player_->_level;
					stat.armor_tier = s->player_->get_armor_tier();
					stat.weapon_tier = s->player_->_weapon_tier;
					s->do_send(stat.size, reinterpret_cast<char*>(&stat));
				}
			}
			// 크리퍼 스스로 사망
			_hp = 0;
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
		bool has_player_nearby = !new_vl.empty();
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
		strncpy_s(userId_, sizeof(userId_), p->user_id, sizeof(userId_) - 1); // Store user_id from packet
		
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
				stat.armor_tier = player_->get_armor_tier();
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

					// Send status change to update NPC HP on clients
					S2C_StatusChange stat;
					stat.size       = sizeof(stat);
					stat.type       = S2C_STATUS_CHANGE;
					stat.object_id  = oid;
					stat.hp         = npc->_hp;
					stat.max_hp     = npc->_maxHp;
					stat.exp        = npc->_exp;
					stat.level      = npc->_level;
					stat.armor_tier = 0;
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
						
						// Reward
						int acquired_gold = 0;
						if (npc->_dropItem > 0) {
							player_->add_item(npc->_dropItem, 1);
						}
						if (npc->_dropGold > 0) {
							acquired_gold = (rand() % npc->_dropGold + 1);
							player_->_gold += acquired_gold;
						}
						
						bool leveled_up = player_->gain_exp(npc->_exp);
						
						// 항상 경험치가 갱신되었으므로 플레이어 본인에게 상태 변경 패킷 전송
						S2C_StatusChange stat;
						stat.size       = sizeof(stat);
						stat.type       = S2C_STATUS_CHANGE;
						stat.object_id  = id_;
						stat.hp         = player_->_hp;
						stat.max_hp     = player_->_maxHp;
						stat.exp        = player_->_exp;
						stat.level      = player_->_level;
						stat.armor_tier = player_->get_armor_tier();
						stat.weapon_tier = player_->_weapon_tier;
						do_send(stat.size, reinterpret_cast<char*>(&stat));

						if (leveled_up) {
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
							if (npc->_dropItem > 0) msg += "Item_ID[" + std::to_string(npc->_dropItem) + "] 1개 ";
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
		
		// 간편하게 DB 저장 호출!
		cl->save_to_db();

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

	// ── Step 1.5. AI 스크립트 경로 등록 (worker_thread에서 lazy-init) ─────
	const std::string ai_dir = "ai/";
	NpcAiManager::register_script(1, ai_dir + "zombie_ai.lua");
	NpcAiManager::register_script(2, ai_dir + "skeleton_ai.lua");
	NpcAiManager::register_script(3, ai_dir + "creeper_ai.lua");
	NpcAiManager::register_script(4, ai_dir + "enderman_ai.lua");
	NpcAiManager::register_script(5, ai_dir + "iron_golem_ai.lua");
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
				case EVENT_PROJECTILE:
				{
					EXP_OVER* proj_over = new EXP_OVER;
					proj_over->type = io_type::npc_projectile;
					proj_over->event_data = top_ev;
					PostQueuedCompletionStatus(h_iocp, -1, top_ev.obj_id, &proj_over->over);
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
				new_session->player_->x_ = 1000 + 100;
				new_session->player_->y_ = 1000;
				new_session->player_->_armor = g_armor_config.netherite;
				new_session->player_->update_max_hp(); // 체력 최대치 업데이트

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
				// 5초 뒤 리스폰
				event_type ev;
				ev.obj_id = id;
				ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::seconds(5);
				ev.event_id = EVENT_RESPAWN;
				ev.target_id = -1;
				timer_queue.push(ev);
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
					add_pkt.armor_tier = 0;
					add_pkt.weapon_tier = 0;
					add_pkt.dir_x = 0;
					add_pkt.dir_y = -1;
					
					for (auto pid : sector.get_objects_nearby_sector(npc->x_, npc->y_)) {
						if (!is_npc_id(pid)) {
							std::shared_ptr<SESSION> s = clients[pid].load();
							if (s && s->state_ == client_state::playing) {
								if (s->is_visible(npc->x_, npc->y_)) {
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
		case io_type::npc_projectile:
		{
			event_type ev = o->event_data;
			delete o;

			int cx = ev.start_x + ev.dir_x * ev.step;
			int cy = ev.start_y + ev.dir_y * ev.step;
			
			S2C_AttackEffect eff;
			eff.size = sizeof(eff);
			eff.type = S2C_ATTACK_EFFECT;
			eff.object_id = ev.obj_id;
			eff.weapon_tier = 0;
			eff.attack_type = 4; // 1-tile projectile effect
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
						ps->player_->_hp -= actual_dmg;
						
						S2C_StatusChange stat;
						stat.size       = sizeof(stat);
						stat.type       = S2C_STATUS_CHANGE;
						stat.object_id  = pid;
						stat.hp         = ps->player_->_hp;
						stat.max_hp     = ps->player_->_maxHp;
						stat.exp        = ps->player_->_exp;
						stat.level      = ps->player_->_level;
						stat.armor_tier = ps->player_->get_armor_tier();
						stat.weapon_tier = ps->player_->_weapon_tier;
						ps->do_send(stat.size, reinterpret_cast<char*>(&stat));
					}
				}
			}

			if (!hit_player && ev.step < 5) {
				ev.step++;
				ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::milliseconds(200);
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
						new_session->player_->_armor = result.armor_tier;
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
						
						sector.add_object(id, new_session->player_->x_, new_session->player_->y_);
						new_session->send_avatar_info();
						new_session->send_inventory_sync();
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

	int64_t ret = bind(server_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));

	listen(server_socket, SOMAXCONN);

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

	closesocket(server_socket);
	WSACleanup();
}
