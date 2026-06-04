#pragma once
#include <string>
#include <chrono>

class Object
{
public:
	int id;
	std::string name;
	int16_t x, y;
	float render_x = 0.0f;
	float render_y = 0.0f;
	char armor_tier = 0;
	char weapon_tier = 1;
	short dir_x = 0;
	short dir_y = -1;
	int visual_id = 0;
	int hp = 100;
	int max_hp = 100;

	// In-game Chat Bubble
	std::wstring chat_msg;
	std::chrono::time_point<std::chrono::steady_clock> chat_time;

	// AI State (NpcState enum from Protocol.h, 0=IDLE, 2=AGGRO)
	char npc_state = 0;
};
