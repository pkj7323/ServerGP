#include "pch.h"
#include "GameManager.h"

#include "NetworkManager.h"
#include "RenderManager.h"

void GameManager::Init()
{
	std::cout << "Initializing GameManager...\n";
	std::cout << "Loading map data from files...\n";
	load_map();
	std::cout << "Map loaded finished.\n";
}

void GameManager::Release()
{
	NetworkManager::Instance()->Release();
	RenderManager::Instance()->Release();
	_players.clear();
}

void GameManager::load_map()
{
	std::ifstream v_file("Data/map_visual.bin", std::ios::binary);
	if (v_file) v_file.read(reinterpret_cast<char*>(_visualMap.data()), _visualMap.size());
	else { 		
		std::cerr << "Failed to load map_visual.bin\n";
		__debugbreak();
	}

	std::ifstream c_file("Data/map_collision.bin", std::ios::binary);
	if (c_file) c_file.read(reinterpret_cast<char*>(_collisionMap.data()), _collisionMap.size());
	else {
		std::cerr << "Failed to load map_collision.bin\n";
		__debugbreak();
	}
}

void GameManager::add_attack_effect(int id, char tier, char type, short x, short y, short dx, short dy) {
	AttackEffect eff;
	eff.id = id; eff.tier = tier; eff.attack_type = type;
	eff.x = x; eff.y = y; eff.dx = dx; eff.dy = dy;
	eff.start_time = std::chrono::steady_clock::now();
	_attack_effects.push_back(eff);
}

void GameManager::add_damage_text(int obj_id, int damage) {
	DamageText dt;
	dt.object_id = obj_id;
	dt.damage = damage;
	dt.start_time = std::chrono::steady_clock::now();
	dt.offset_x = (rand() % 21 - 10) / 10.0f;
	dt.offset_y = (rand() % 21 - 10) / 10.0f;
	_damage_texts.push_back(dt);
}
