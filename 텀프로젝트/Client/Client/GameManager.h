#pragma once

#include "Singleton.h"
#include "Object.h"
#include <unordered_map>
#include <string>
#include <vector>

struct AttackEffect {
	int id;
	char tier;
	short x, y, dx, dy;
	std::chrono::time_point<std::chrono::steady_clock> start_time;
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

	void set_running(bool running) { _isRunning = running; }
	bool is_running() const { return _isRunning; }

	// 맵 데이터 로드
	void load_map();

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
	std::vector<std::wstring>& chat_logs() { return _chatLogs; }
	void add_chat_log(const std::wstring& msg) { 
		_chatLogs.push_back(msg); 
		if (_chatLogs.size() > 10) _chatLogs.erase(_chatLogs.begin());
	}
	
	bool is_chatting() const { return _isChatting; }
	void set_chatting(bool val) { _isChatting = val; }
	
	std::wstring& current_chat_input() { return _currentChatInput; }
	
	bool show_inventory() const { return _showInventory; }
	void toggle_inventory() { _showInventory = !_showInventory; }

	std::vector<AttackEffect>& attack_effects() { return _attackEffects; }

private:
	std::vector<AttackEffect> _attackEffects;
	std::vector<std::wstring> _chatLogs;
	bool _isChatting = false;
	std::wstring _currentChatInput = L"";
	bool _showInventory = false;
	std::unordered_map<int, Object> _players;
	std::unordered_map<int, Object> _npcs;
	int _myId;
	std::string _username;
	bool _isRunning;

	std::array<uint8_t, WORLD_WIDTH* WORLD_HEIGHT> _visualMap{};
	std::array<uint8_t, WORLD_WIDTH* WORLD_HEIGHT> _collisionMap{};
};
