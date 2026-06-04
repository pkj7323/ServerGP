#pragma once
// =============================================================================
// npc_ai_manager.h
// NPC 타입별 Lua AI를 thread_local 방식으로 관리합니다.
//
// 설계:
//   - 워커 스레드마다 독립적인 lua_State 셋을 보유 (mutex 불필요)
//   - 타입별 AI 스크립트(zombie_ai.lua 등)를 스레드 첫 사용 시 한 번만 로드
//   - NPC 타이머 이벤트 발생 시 on_timer(ctx) 호출 → NpcAction 반환
//   - C2S_ATTACK 처리 시 on_hit(ctx, damage) 호출 → 어그로 전환 등 반응
// =============================================================================

#include <string>
#include <unordered_map>
#include <windows.h>
#include "lua-5.5.0/include/lua.hpp"
#pragma comment(lib, "lua-5.5.0/lua55.lib")
#include "Protocol.h"

// ── Lua AI에 넘기는 NPC 컨텍스트 ─────────────────────────────────────────────
struct NpcAiContext {
	int self_id       = 0;
	int self_x        = 0;
	int self_y        = 0;
	int self_hp       = 0;
	int self_max_hp   = 0;
	int initial_x     = 0;    // 스폰 지점 (순찰 기준)
	int initial_y     = 0;
	int current_state = 0;    // NpcState enum (char)
	int target_id     = -1;   // 가장 가까운 플레이어 ID (-1: 없음)
	int target_x      = 0;
	int target_y      = 0;
	int dist          = 9999; // 맨해튼 거리
};

// ── Lua AI가 반환하는 행동 명령 ───────────────────────────────────────────────
enum class NpcActionType : int {
	IDLE         = 0,
	MOVE         = 1,
	MELEE_ATTACK = 2,  // 근접 공격 (target_id에 데미지)
	RANGE_ATTACK = 3,  // 8방향 직선 5칸 공격
	CHAT         = 4,
	DIE          = 5,  // 즉사 (크리퍼 자폭)
};

struct NpcAction {
	NpcActionType type      = NpcActionType::IDLE;
	int           dx        = 0;
	int           dy        = 0;
	int           target_id = -1;
	int           new_state = 0;   // 변경할 NpcState
	int           delay_ms  = 1000; // 액션 후 다음 타이머 딜레이
	std::string   chat_msg;
};

// =============================================================================
// NpcAiManager
// =============================================================================
class NpcAiManager {
public:
	// ── thread_local lua_State 풀 접근 ───────────────────────────────────────
	// npc_type (1~5)에 해당하는 lua_State를 반환.
	// 스레드 첫 호출 시 스크립트를 로드한 뒤 캐시.
	static lua_State* get_state(int npc_type) {
		auto& pool = get_pool();
		auto it = pool.find(npc_type);
		if (it != pool.end()) return it->second;
		return nullptr; // 아직 로드 안 됨 (init 필요)
	}

	// ── 서버 시작 시 전역 스크립트 경로 등록 ─────────────────────────────────
	// main()에서 한 번 호출. 실제 로드는 각 스레드 첫 사용 시 lazy-init.
	static void register_script(int npc_type, const std::string& path) {
		script_paths()[npc_type] = path;
	}

	// ── 워커 스레드에서 호출: 해당 스레드의 Lua State 초기화 ─────────────────
	// 각 워커 스레드 시작 시 한 번 호출하면 thread_local pool에 Lua VM 생성.
	static void init_thread_states() {
		auto& pool = get_pool();
		for (auto& [type_id, path] : script_paths()) {
			if (pool.contains(type_id)) continue; // 이미 있음
			lua_State* L = luaL_newstate();
			luaL_openlibs(L);

			// 유틸 함수 등록 (math.random 사용 가능하게 열어둠)
			if (luaL_dofile(L, path.c_str()) != LUA_OK) {
				std::cerr << "[NpcAI][ERROR] Lua 로드 실패 type=" << type_id
				          << " : " << lua_tostring(L, -1) << std::endl;
				lua_close(L);
				continue;
			}
			pool[type_id] = L;
		}
	}

	// ── on_timer 호출 (EVENT_MOVE 타이머 만료 시) ────────────────────────────
	static NpcAction call_on_timer(int npc_type, const NpcAiContext& ctx) {
		return call_hook(npc_type, "on_timer", ctx, -1, 0);
	}

	// ── on_hit 호출 (NPC가 공격받았을 때) ────────────────────────────────────
	static NpcAction call_on_hit(int npc_type, const NpcAiContext& ctx, int attacker_id, int damage) {
		return call_hook(npc_type, "on_hit", ctx, attacker_id, damage);
	}

	// ── on_spawn 호출 (NPC 최초 스폰 / 리스폰 시) ───────────────────────────
	static NpcAction call_on_spawn(int npc_type, const NpcAiContext& ctx) {
		return call_hook(npc_type, "on_spawn", ctx, -1, 0);
	}

private:
	// thread_local lua_State 풀 (타입ID → lua_State*)
	static std::unordered_map<int, lua_State*>& get_pool() {
		thread_local std::unordered_map<int, lua_State*> pool;
		return pool;
	}

	// 전역 스크립트 경로 맵 (main에서 등록)
	static std::unordered_map<int, std::string>& script_paths() {
		static std::unordered_map<int, std::string> paths;
		return paths;
	}

	// ── 공통 훅 호출 헬퍼 ────────────────────────────────────────────────────
	static NpcAction call_hook(int npc_type, const char* func_name,
	                           const NpcAiContext& ctx, int extra_id, int extra_int)
	{
		NpcAction result;
		lua_State* L = get_state(npc_type);
		if (!L) return result;

		lua_getglobal(L, func_name);
		if (!lua_isfunction(L, -1)) {
			lua_pop(L, 1);
			return result;
		}

		// ctx 테이블 push
		lua_newtable(L);
		auto seti = [&](const char* key, int val) {
			lua_pushinteger(L, val);
			lua_setfield(L, -2, key);
		};
		seti("self_id",       ctx.self_id);
		seti("self_x",        ctx.self_x);
		seti("self_y",        ctx.self_y);
		seti("self_hp",       ctx.self_hp);
		seti("self_max_hp",   ctx.self_max_hp);
		seti("initial_x",     ctx.initial_x);
		seti("initial_y",     ctx.initial_y);
		seti("current_state", ctx.current_state);
		seti("target_id",     ctx.target_id);
		seti("target_x",      ctx.target_x);
		seti("target_y",      ctx.target_y);
		seti("dist",          ctx.dist);

		// 추가 인수 (on_hit 용)
		int nargs = 1;
		if (extra_id != -1 || extra_int != 0) {
			lua_pushinteger(L, extra_id);
			lua_pushinteger(L, extra_int);
			nargs = 3;
		}

		if (lua_pcall(L, nargs, 1, 0) != LUA_OK) {
			std::cerr << "[NpcAI][ERROR] " << func_name
			          << " 호출 실패: " << lua_tostring(L, -1) << std::endl;
			lua_pop(L, 1);
			return result;
		}

		// 반환값 파싱 (테이블)
		if (lua_istable(L, -1)) {
			auto geti = [&](const char* key) -> int {
				lua_getfield(L, -1, key);
				int v = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : 0;
				lua_pop(L, 1);
				return v;
			};
			auto gets = [&](const char* key) -> std::string {
				lua_getfield(L, -1, key);
				std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
				lua_pop(L, 1);
				
				// UTF-8(Lua) -> ANSI(CP949) 변환
				if (!v.empty()) {
					int wlen = MultiByteToWideChar(CP_UTF8, 0, v.c_str(), -1, NULL, 0);
					if (wlen > 0) {
						std::wstring wstr(wlen, 0);
						MultiByteToWideChar(CP_UTF8, 0, v.c_str(), -1, &wstr[0], wlen);
						
						int alen = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
						if (alen > 0) {
							std::string ansi_str(alen, 0);
							WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &ansi_str[0], alen, NULL, NULL);
							if (!ansi_str.empty() && ansi_str.back() == '\0') ansi_str.pop_back();
							v = ansi_str;
						}
					}
				}
				
				return v;
			};

			int action_int      = geti("action");
			result.type         = static_cast<NpcActionType>(action_int);
			result.dx           = geti("dx");
			result.dy           = geti("dy");
			result.target_id    = geti("target_id");
			result.new_state    = geti("new_state");
			result.chat_msg     = gets("chat");

			lua_getfield(L, -1, "delay_ms");
			if (lua_isinteger(L, -1)) result.delay_ms = (int)lua_tointeger(L, -1);
			else result.delay_ms = 1000; // 기본 1초
			lua_pop(L, 1);

			// target_id 기본값 처리 (-1 이면 0이 나오므로 보정)
			lua_getfield(L, -1, "target_id");
			if (lua_isnil(L, -1)) result.target_id = -1;
			lua_pop(L, 1);
		}

		lua_pop(L, 1); // 반환값 팝
		return result;
	}
};
