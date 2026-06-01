#pragma once
#include <string>
#include <chrono>

class Object
{
public:
	int id;
	std::string name;
	int16_t x, y;
	char armor_tier = 0;
	char weapon_tier = 1;
	short dir_x = 0;
	short dir_y = -1;

	// In-game Chat Bubble
	std::wstring chat_msg;
	std::chrono::time_point<std::chrono::steady_clock> chat_time;
};
