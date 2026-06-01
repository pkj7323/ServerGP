#pragma once
// =============================================================================
// npc_loader.h
// 서버 초기화 시 두 가지 데이터 소스를 로드합니다.
//   1. npc_config.lua  - NPC 타입별 메타데이터 (이름/HP/레벨/경험치/공격력)
//   2. map_spawn.bin   - 스폰 위치 + 타입 ID (2000x2000 uint8_t 배열)
// =============================================================================

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "lua-5.5.0/include/lua.hpp"
#pragma comment(lib, "lua-5.5.0/lua55.lib")
#include "Protocol.h"

// ── 방어력 임계값 메타데이터 (Lua npc_config.lua에서 로드) ────────────────────
struct ArmorConfig {
	int copper = 20;
	int iron = 100;
	int diamond = 200;
	int netherite = 300;
};

// ── NPC 타입 메타데이터 (Lua npc_config.lua에서 로드) ─────────────────────────
struct NpcMeta {
	std::string        name;
	int                hp        = 0;
	int                max_hp    = 0;
	int                level     = 1;
	unsigned long long exp       = 0;
	int                attack    = 0;
	int                visual_id = 0;
	int                drop_item = 0;
	int                drop_gold = 0;
};

// ── 스폰 엔트리 (map_spawn.bin 스캔 결과) ─────────────────────────────────────
struct SpawnEntry {
	int16_t x;
	int16_t y;
	uint8_t type_id;
};

// =============================================================================
// load_npc_config_lua()
//   npc_config.lua를 Lua VM으로 읽어 NPC_TYPES[1~5]를 NpcMeta 배열로 반환.
//   인덱스 0은 NPC_NONE (빈 값).
// =============================================================================
inline std::array<NpcMeta, 6> load_npc_config_lua(const char* lua_path)
{
	std::array<NpcMeta, 6> meta{};

	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

	if (luaL_dofile(L, lua_path) != LUA_OK) {
		std::cerr << "[NpcLoader][ERROR] Lua 로드 실패: "
				  << lua_tostring(L, -1) << std::endl;
		lua_close(L);
		return meta;
	}

	lua_getglobal(L, "NPC_TYPES");
	if (!lua_istable(L, -1)) {
		std::cerr << "[NpcLoader][ERROR] NPC_TYPES 테이블이 없습니다." << std::endl;
		lua_close(L);
		return meta;
	}

	for (int type_id = 1; type_id <= 5; ++type_id)
	{
		lua_pushinteger(L, type_id);
		lua_gettable(L, -2);   // NPC_TYPES[type_id]

		if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }

		NpcMeta& m = meta[type_id];

		auto read_str = [&](const char* key) -> std::string {
			lua_getfield(L, -1, key);
			std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
			lua_pop(L, 1);
			return v;
		};
		auto read_int = [&](const char* key) -> int {
			lua_getfield(L, -1, key);
			int v = lua_isinteger(L, -1) ? static_cast<int>(lua_tointeger(L, -1)) : 0;
			lua_pop(L, 1);
			return v;
		};

		m.name      = read_str("name");
		m.hp        = read_int("hp");
		m.max_hp    = m.hp;
		m.level     = read_int("level");
		m.exp       = static_cast<unsigned long long>(read_int("exp"));
		m.attack    = read_int("attack");
		m.visual_id = read_int("visual_id");
		m.drop_item = read_int("drop_item");
		m.drop_gold = read_int("drop_gold");

		std::cout << "[NpcLoader] Type[" << type_id << "] "
				  << m.name << "  HP=" << m.hp
				  << " Lv=" << m.level
				  << " EXP=" << m.exp << std::endl;

		lua_pop(L, 1); // pop type table
	}

	lua_pop(L, 1); // pop NPC_TYPES
	lua_close(L);

	return meta;
}

// =============================================================================
// load_armor_config_lua()
//   npc_config.lua를 Lua VM으로 읽어 ARMOR_THRESHOLDS를 ArmorConfig로 반환.
// =============================================================================
inline ArmorConfig load_armor_config_lua(const char* lua_path)
{
	ArmorConfig config;

	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

	if (luaL_dofile(L, lua_path) != LUA_OK) {
		std::cerr << "[NpcLoader][ERROR] Lua 로드 실패 (ArmorConfig): "
				  << lua_tostring(L, -1) << std::endl;
		lua_close(L);
		return config;
	}

	lua_getglobal(L, "ARMOR_THRESHOLDS");
	if (!lua_istable(L, -1)) {
		std::cerr << "[NpcLoader][ERROR] ARMOR_THRESHOLDS 테이블이 없습니다." << std::endl;
		lua_close(L);
		return config;
	}

	auto read_int = [&](const char* key, int default_val) -> int {
		lua_getfield(L, -1, key);
		int v = lua_isinteger(L, -1) ? static_cast<int>(lua_tointeger(L, -1)) : default_val;
		lua_pop(L, 1);
		return v;
	};

	config.copper    = read_int("COPPER", 20);
	config.iron      = read_int("IRON", 100);
	config.diamond   = read_int("DIAMOND", 200);
	config.netherite = read_int("NETHERITE", 300);

	std::cout << "[NpcLoader] ARMOR_THRESHOLDS 로드 완료: Copper=" << config.copper
			  << ", Iron=" << config.iron
			  << ", Diamond=" << config.diamond
			  << ", Netherite=" << config.netherite << std::endl;

	lua_pop(L, 1); // pop ARMOR_THRESHOLDS
	lua_close(L);

	return config;
}

// =============================================================================
// load_spawn_map()
//   map_spawn.bin(2000x2000 uint8_t)을 읽어 NPC_NONE(0)이 아닌 셀만
//   SpawnEntry 벡터로 반환합니다. 약 200,000개 예상.
// =============================================================================
inline std::vector<SpawnEntry> load_spawn_map(const char* bin_path)
{
	std::vector<SpawnEntry> entries;
	entries.reserve(200000);

	std::ifstream f(bin_path, std::ios::binary);
	if (!f) {
		std::cerr << "[NpcLoader][ERROR] map_spawn.bin 열기 실패: "
				  << bin_path << std::endl;
		return entries;
	}

	const int TOTAL_CELLS = WORLD_WIDTH * WORLD_HEIGHT;
	std::vector<uint8_t> buf(TOTAL_CELLS);
	f.read(reinterpret_cast<char*>(buf.data()), TOTAL_CELLS);

	for (int i = 0; i < TOTAL_CELLS; ++i)
	{
		if (buf[i] == 0) continue; // NPC_NONE

		SpawnEntry e;
		e.x       = static_cast<int16_t>(i % WORLD_WIDTH);
		e.y       = static_cast<int16_t>(i / WORLD_WIDTH);
		e.type_id = buf[i];
		entries.push_back(e);
	}

	std::cout << "[NpcLoader] map_spawn.bin 로드 완료: "
			  << entries.size() << "개 스폰 엔트리" << std::endl;
	return entries;
}