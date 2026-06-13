#pragma once

#include "Singleton.h"
#include "Object.h"
#include <unordered_map>
#include <string>
#include <vector>

struct AttackEffect {
	int id;
	char tier;
	char attack_type;
	short x, y, dx, dy;
	std::chrono::time_point<std::chrono::steady_clock> start_time;
};

struct DamageText {
	int object_id;
	int damage;
	std::chrono::time_point<std::chrono::steady_clock> start_time;
	float offset_x;
	float offset_y;
};

class GameManager : public Singleton<GameManager>
{
	friend class Singleton<GameManager>;
public:
	GameManager() : _isRunning(true), _myId(-1) {}

	void Init() override;
	void Release() override;


	std::unordered_map<int, Object>& players() { return _players; }
	std::unordered_map<int, Object>& npcs() { return _npcs; }

	void set_my_id(int id) { _myId = id; }
	int my_id() const { return _myId; }

	void set_username(const std::string& name) { _username = name; }
	const std::string& username() const { return _username; }

	void set_user_id(const std::string& id) { _user_id = id; }
	const std::string& user_id() const { return _user_id; }

	void set_running(bool running) { _isRunning = running; }
	bool is_running() const { return _isRunning; }

	// 맵 데이터 로드
	void load_map();
	void add_attack_effect(int id, char tier, char type, short x, short y, short dx, short dy);

	// 충돌 체크
	bool can_move(int x, int y) {
		if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT) return false;
		return _collisionMap[y * WORLD_WIDTH + x] == 0;
	}

	// 렌더링용 타일 가져오기
	uint8_t get_visual_tile(int x, int y) {
		if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT) return 0;
		return _visualMap[y * WORLD_WIDTH + x];
	}

	// -- UI & Chat States --
	std::vector<std::wstring>& chat_logs() { return _chat_logs; }
	void add_chat_log(const std::wstring& msg) { 
		_chat_logs.push_back(msg); 
		if (_chat_logs.size() > 10) _chat_logs.erase(_chat_logs.begin());
	}
	
	bool is_chatting() const { return _isChatting; }
	void set_chatting(bool val) { _isChatting = val; }
	
	std::wstring& current_chat_input() { return _currentChatInput; }
	
	bool show_inventory() const { return _showInventory; }
	void toggle_inventory() { _showInventory = !_showInventory; }
	bool show_skill_tree() const { return _showSkillTree; }
	void toggle_skill_tree() { _showSkillTree = !_showSkillTree; }

	int my_gold() const { return _myGold; }
	void set_my_gold(int g) { _myGold = g; }

	bool is_trading() const { return _isTrading; }
	void set_trading(bool val) { _isTrading = val; }
	int trade_npc_id() const { return _tradeNpcId; }
	void set_trade_npc_id(int val) { _tradeNpcId = val; }
	int trade_visual_id() const { return _tradeVisualId; }
	void set_trade_visual_id(int val) { _tradeVisualId = val; }

	// Quest Info
	int quest_stage() const { return _questStage; }
	void set_quest_stage(int val) { _questStage = val; }
	int quest_progress() const { return _questProgress; }
	void set_quest_progress(int val) { _questProgress = val; }
	int max_quest_progress() const { return _maxQuestProgress; }
	void set_max_quest_progress(int val) { _maxQuestProgress = val; }

	int unspent_sp() const { return _unspentSp; }
	void set_unspent_sp(int val) { _unspentSp = val; }
	int skills_mask() const { return _skillsMask; }
	void set_skills_mask(int val) { _skillsMask = val; }

	std::unordered_map<int, int>& my_inventory() { return _myInventory; }

	std::vector<AttackEffect>& attack_effects() { return _attack_effects; }

	void add_damage_text(int obj_id, int damage);
	std::vector<DamageText>& damage_texts() { return _damage_texts; }

	void start_potion_cooldown() { _last_potion_use = std::chrono::steady_clock::now(); }
	void start_skill_cooldown() { _last_skill_use = std::chrono::steady_clock::now(); _buff_end_time = _last_skill_use + std::chrono::seconds(5); }
	void start_ender_pearl_cooldown() { _last_ender_pearl_use = std::chrono::steady_clock::now(); }

	std::chrono::time_point<std::chrono::steady_clock> last_potion_use() const { return _last_potion_use; }
	std::chrono::time_point<std::chrono::steady_clock> last_skill_use() const { return _last_skill_use; }
	std::chrono::time_point<std::chrono::steady_clock> last_ender_pearl_use() const { return _last_ender_pearl_use; }
	std::chrono::time_point<std::chrono::steady_clock> buff_end_time() const { return _buff_end_time; }

private:
	std::atomic<bool> _isRunning;

	int _myId;
	std::string _username;
	std::string _user_id;
	std::unordered_map<int, Object> _players;
	std::unordered_map<int, Object> _npcs;
	std::vector<std::wstring> _chat_logs;
	std::vector<AttackEffect> _attack_effects;
	std::vector<DamageText> _damage_texts;

	bool _isChatting = false;
	std::wstring _currentChatInput = L"";
	bool _showInventory = false;
	bool _showSkillTree = false;
	int _myGold = 0;
	std::unordered_map<int, int> _myInventory;

	std::chrono::time_point<std::chrono::steady_clock> _last_potion_use;
	std::chrono::time_point<std::chrono::steady_clock> _last_skill_use;
	std::chrono::time_point<std::chrono::steady_clock> _buff_end_time;
	std::chrono::time_point<std::chrono::steady_clock> _last_ender_pearl_use;

	bool _isTrading = false;
	int _tradeNpcId = -1;
	int _tradeVisualId = -1;
	
	int _questStage = 0;
	int _questProgress = 0;
	int _maxQuestProgress = 0;
	int _unspentSp = 0;
	int _skillsMask = 0;

	std::array<uint8_t, WORLD_WIDTH* WORLD_HEIGHT> _visualMap{};
	std::array<uint8_t, WORLD_WIDTH* WORLD_HEIGHT> _collisionMap{};
};
