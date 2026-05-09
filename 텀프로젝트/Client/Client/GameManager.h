#pragma once

#include "Singleton.h"
#include "Object.h"
#include <unordered_map>
#include <string>

class GameManager : public Singleton<GameManager>
{
	friend class Singleton<GameManager>;
public:
	GameManager() : _isRunning(true), _myId(-1) {}

	std::unordered_map<int, Object>& players() { return _players; }
	std::unordered_map<int, Object>& npcs() { return _npcs; }

	void set_my_id(int id) { _myId = id; }
	int my_id() const { return _myId; }

	void set_username(const std::string& name) { _username = name; }
	const std::string& username() const { return _username; }

	void set_running(bool running) { _isRunning = running; }
	bool is_running() const { return _isRunning; }

private:
	std::unordered_map<int, Object> _players;
	std::unordered_map<int, Object> _npcs;
	int _myId;
	std::string _username;
	bool _isRunning;
};
