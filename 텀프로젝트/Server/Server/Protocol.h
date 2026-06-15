#pragma once

constexpr short PORT = 3900;
constexpr int WORLD_WIDTH = 2000;
constexpr int WORLD_HEIGHT = 2000;
constexpr int MAX_PLAYERS = 10000;
constexpr int NUM_NPCS = 200000;
constexpr int NPC_ID_START = 1000000;
constexpr int NPC_MOVE_INTERVAL = 1000; // in milliseconds
constexpr int MAX_NAME_LEN = 20;
constexpr int MAX_CHAT_MSG_LEN = 200;
constexpr int MAX_INVENTORY_SLOTS = 20;

enum PACKET_TYPE {
	C2S_LOGIN,			// Client to Server: Login request
						// 사용자 이름을 포함한 로그인 요청 패킷	
	C2S_MOVE,			// Client to Server: Move request
						// 이동 방향과 이동 시간을 포함한 이동 요청 패킷
	C2S_CHAT,			// Client to Server: Chat message
						// 채팅 메시지를 포함한 채팅 요청 패킷
	C2S_ATTACK,			// Client to Server: Attack request
						// 공격 요청 패킷 (4 방향 동시 공격)
	C2S_TELEPORT,		// Client to Server: Teleport request
						// 텔레포트 요청 패킷 (목적지 좌표 포함)
						// STRESS TEST용으로 추가한 패킷입니다. 시작 마을에 몰리는 것을 방지.
	C2S_LOGOUT,			// Client to Server: Logout request
	C2S_CHEAT,			// Client to Server: Cheat command
	C2S_USE_QUICKSLOT,	// Client to Server: Use quick slot (1 or 2)
	C2S_INTERACT_NPC,   // Client to Server: Interact with an NPC (e.g. F key)
	C2S_TRADE_COMMAND,  // Client to Server: Send trade option (e.g. 1, 2, 3)
	C2S_LEARN_SKILL,    // Client to Server: Learn a skill

	S2C_LOGIN_RESULT,	//	Server to Client: Login result
						// 로그인 결과 패킷 (성공 여부와 메시지 포함)
	S2C_AVATAR_INFO,	//	Server to Client: Avatar information
	S2C_ADD_OBJECT,		//	Server to Client: Add player or NPC		
	S2C_REMOVE_OBJECT,	//	Server to Client: Remove player or NPC
	S2C_MOVE_OBJECT,	//	Server to Client: Move player or NPC
	S2C_CHAT_MESSAGE,	//	Server to Client: Chat message
	S2C_STATUS_CHANGE,	//	Server to Client: Update player or NPC status (e.g., health, buffs)	
	S2C_ATTACK_EFFECT,	//	Server to Client: Show attack animation/range
	S2C_INVENTORY_SYNC,	//  Server to Client: Synchronize player inventory
	S2C_NPC_STATE_CHANGE,	// Server to Client: NPC state changed (IDLE/PATROL/AGGRO etc)
	S2C_OPEN_TRADE_UI,  // Server to Client: Open trading UI for specific merchant
	S2C_TRADE_RESULT,   // Server to Client: Result of trade (chat message)
	S2C_QUEST_INFO,     // Server to Client: Quest stage and progress update
	S2C_SKILL_SYNC,     // Server to Client: Synchronize player skills and cooldowns
	S2C_UPDATE_POSITION,// Server to Client: Force position update for teleporting
	S2C_ADD_NPC,		// Server to Client: Add NPC (lightweight, no equipment/name)
};

// NPC AI 상태 - 클라이언트와 서버가 공유하는 enum
enum class NpcState : char {
	IDLE     = 0,  // 비활성 (플레이어 없음)
	PATROL   = 1,  // 배회 중
	AGGRO    = 2,  // 플레이어 추격/공격 중
	ATTACKING = 3, // 공격 중 (예약)
	FLEEING = 4,   // 도주 중 (크리퍼 제외)
	DEAD     = 5,  // 사망
	MERCHANT = 6,  // 상인 (이동/공격 불가)
};

enum class SkillType : int {
	FIRE_ASPECT = 0, // Tier 1
	LIFE_STEAL = 1,  // Tier 1
	RESISTANCE = 2,  // Tier 2
	ENDER_PEARL = 3  // Tier 2
};

enum class ItemType : int {
	NONE = 0,
	ROTTEN_FLESH = 1, // 썩은 고기
	BONE = 2,         // 뼈다귀
	GUNPOWDER = 3,    // 화약
	IRON_INGOT = 4,   // 철 주괴
	GOLD_INGOT = 5,   // 금 주괴
	DIAMOND = 6,      // 다이아몬드
	
	HEALTH_POTION = 7,
	MANA_POTION = 8,
	ENDER_PEARL = 9,
	ENDER_EYE = 10,
	BLAZE_POWDER = 11,
};

#pragma pack(push, 1) // Ensure no padding between struct members
struct C2S_Login {
	unsigned char size;
	PACKET_TYPE   type;
	char username[MAX_NAME_LEN];
	char user_id[MAX_NAME_LEN];
};

struct C2S_Move {
	unsigned char size;
	PACKET_TYPE   type;
	short x;
	short y;
	int move_time; // in milliseconds
};

struct C2S_Chat {
	unsigned char size;
	PACKET_TYPE   type;
	char message[MAX_CHAT_MSG_LEN];
};

struct C2S_Attack {
	unsigned char size;
	PACKET_TYPE   type;
};

struct C2S_Teleport {
	unsigned char size;
	PACKET_TYPE   type;
	short x;
	short y;
};

struct C2S_Logout {
	unsigned char size;
	PACKET_TYPE   type;
};

enum class CheatType : char {
	TOGGLE_ARMOR = 1,
	LEVEL_UP = 2,
	TOGGLE_GOD_MODE = 3,
	TOGGLE_SKILLS = 4,
	KILL_PLAYER = 5,
	ADD_MONEY = 6,
	SET_MONEY_ZERO = 7,
	TP_IRON_GOLEM = 8,
	TP_ZOMBIE = 9,
	TP_CREEPER = 10,
	TP_ENDERMAN = 11,
	TP_DRAGON = 12
};

struct C2S_Cheat {
	unsigned char size;
	PACKET_TYPE   type;
	CheatType     cheat_type;
};

struct C2S_UseQuickSlot {
	unsigned char size;
	PACKET_TYPE   type;
	char slot_id; // 1=Health Potion, 2=Skill
};

struct C2S_InteractNpc {
	unsigned char size;
	PACKET_TYPE   type;
};

struct C2S_TradeCommand {
	unsigned char size;
	PACKET_TYPE   type;
	char trade_index; // 1, 2, 3...
	int npc_id;
};

struct S2C_LoginResult {
	unsigned char size;
	PACKET_TYPE   type;
	bool success;
	char message[50];
};

struct S2C_AvatarInfo {
	unsigned char size;
	PACKET_TYPE   type;
	int playerId;
	int visualId; // for future use (different visual appearances)
	short x;
	short y;
	int hp;
	int max_hp;
	unsigned long long exp;
	unsigned char level;
	char head_tier;
	char chest_tier;
	char legs_tier;
	char boots_tier;
	char weapon_tier; // 0: None, 1: Wood, 2: Gold, 3: Copper, 4: Iron, 5: Diamond, 6: Netherite
	short dir_x;
	short dir_y;
};

struct S2C_AddObject {
	unsigned char size;
	PACKET_TYPE   type;
	int object_id;
	int visual_id; // for future use (different visual appearances)
	char obj_name[MAX_NAME_LEN];
	short x;
	short y;
	int hp;
	int max_hp;
	unsigned long long exp;
	unsigned char level;
	char head_tier;
	char chest_tier;
	char legs_tier;
	char boots_tier;
	char weapon_tier;
	short dir_x;
	short dir_y;
	char npc_state; // NpcState enum value (0=IDLE, 2=AGGRO, etc)
};

// NPC 전용 경량 패킷 (장비/이름/EXP/레벨 제거, 약 20바이트)
struct S2C_AddNpc {
	unsigned char size;
	PACKET_TYPE   type;
	int object_id;
	int visual_id;
	short x;
	short y;
	int hp;
	int max_hp;
	char npc_state;
	char is_burning = 0;
};

struct S2C_RemoveObject {
	unsigned char size;
	PACKET_TYPE   type;
	int object_id;
};

struct S2C_MoveObject {
	unsigned char size;
	PACKET_TYPE   type;
	int object_id;
	short x; // 나는 방향으로 해석
	short y;
	short dir_x;
	short dir_y;
	int move_time; // in milliseconds
};

struct S2C_ChatMessage {
	unsigned char size;
	PACKET_TYPE   type;
	int object_id;
	char message[MAX_CHAT_MSG_LEN];
};

struct S2C_StatusChange {
	unsigned char size;
	PACKET_TYPE   type;
	int object_id;
	int hp;
	int max_hp;
	unsigned long long exp;
	unsigned char level;
	char head_tier;
	char chest_tier;
	char legs_tier;
	char boots_tier;
	char weapon_tier;
	char is_burning = 0;
};

struct S2C_AttackEffect {
	unsigned char size;
	PACKET_TYPE   type;
	int object_id; // Who attacked
	char weapon_tier;
	char attack_type; // 0 = normal, 1 = skill (AoE half-circle)
	short x; // Attacker's x
	short y; // Attacker's y
	short dir_x;
	short dir_y;
};

struct ItemSlot {
	int item_id;
	int count;
};

struct S2C_InventorySync {
	unsigned char size;
	PACKET_TYPE   type;
	int gold;
	int item_count;
	ItemSlot items[MAX_INVENTORY_SLOTS];
};

struct S2C_NpcStateChange {
	unsigned char size;
	PACKET_TYPE   type;
	int object_id;
	char npc_state; // NpcState enum value
	int target_id;  // aggro target (-1 if none)
};

struct S2C_OpenTradeUI {
	unsigned char size;
	PACKET_TYPE   type;
	int npc_id;
	int visual_id; // To know which merchant type (6=Priest, 7=Armorer, 8=Weaponsmith, 9=Librarian)
};

struct S2C_TradeResult {
	unsigned char size;
	PACKET_TYPE   type;
	bool success;
	char message[MAX_CHAT_MSG_LEN];
};

struct S2C_QuestInfo {
	unsigned char size;
	PACKET_TYPE   type;
	int quest_stage;
	int quest_progress;
	int max_progress;
};

struct C2S_LearnSkill {
	unsigned char size;
	PACKET_TYPE type;
	int skill_type; // Casted to int
};

struct S2C_SkillSync {
	unsigned char size;
	PACKET_TYPE type;
	int unspent_sp;
	int skills_mask; // Bitmask of learned skills
};

struct S2C_UpdatePosition {
	unsigned char size;
	PACKET_TYPE type;
	short x;
	short y;
};

#pragma pack(pop) // Restore default packing
