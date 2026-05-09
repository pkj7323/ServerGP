#include "pch.h"
#include "GameManager.h"
void GameManager::Init()
{
	std::cout << "Initializing GameManager...\n";
	std::cout << "Loading map data from files...\n";
	load_map();
	std::cout << "Map loaded finished.\n";
}

void GameManager::load_map()
{
	std::ifstream v_file("map_visual.bin", std::ios::binary);
	if (v_file) v_file.read(reinterpret_cast<char*>(_visualMap.data()), _visualMap.size());
	else { 		
		std::cerr << "Failed to load map_visual.bin\n";
		__debugbreak();
	}

	std::ifstream c_file("map_collision.bin", std::ios::binary);
	if (c_file) c_file.read(reinterpret_cast<char*>(_collisionMap.data()), _collisionMap.size());
	else {
		std::cerr << "Failed to load map_collision.bin\n";
		__debugbreak();
	}
}
