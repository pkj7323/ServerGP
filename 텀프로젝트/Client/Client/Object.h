#pragma once
#include <string>
#include <chrono>

class Object
{
public:
	int id;
	std::string name;
	int16_t x, y;

	// In-game Chat Bubble
	std::wstring chat_msg;
	std::chrono::time_point<std::chrono::steady_clock> chat_time;
};
