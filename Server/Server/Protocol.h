//#pragma once
//
//constexpr short PORT = 9001;
//constexpr int WORLD_WIDTH = 400;
//constexpr int WORLD_HEIGHT = 400;
//constexpr int MAX_PLAYERS = 1000;
//constexpr int MAX_NAME_LEN = 20;
//
//enum class packet_type : uint16_t
//{
//	C2S_LOGIN = 0,
//	C2S_MOVE = 1,
//
//
//	S2C_LOGIN_ACK = 12,
//	S2C_AVATAR_INFO = 13,
//	S2C_ADD_PLAYER = 14,
//	S2C_PLAYER_MOVE = 15,
//	S2C_REMOVE_PLAYER = 16,
//
//	S2C_ADD_NPC = 20,
//	S2C_NPC_MOVE = 21,
//	S2C_REMOVE_NPC = 22,
//};
//#pragma pack(push, 1)
//struct packet_header
//{
//	unsigned char size;
//	packet_type type;
//};
//
////----------------------------------------------------------------------------
//// 클라이언트 -> 서버 패킷
//struct c2s_login : packet_header
//{
//	char userName[MAX_NAME_LEN];
//};
//struct c2s_move : packet_header
//{
//	struct vec2
//	{
//		int16_t x;
//		int16_t y;
//	} dir;
//	uint32_t timestamp;
//};
//
////-----------------------------------------------------------------------------
//// 서버 -> 클라이언트 패킷
//struct s2c_login_ack : packet_header
//{
//	bool success;
//	char msg[50];
//};
//
//struct s2c_avatar_info : packet_header
//{
//	int id; // 플레이어 아이디는 적어도 int32_t 이상이어야 합니다.
//	int16_t x;
//	int16_t y;
//};
//
//struct s2c_add_player : packet_header
//{
//	int id;
//	char userName[MAX_NAME_LEN];
//	int16_t x;
//	int16_t y;
//};
//struct s2c_remove_player : packet_header
//{
//	int id;
//};
//struct s2c_player_move : packet_header
//{
//	int id;
//	int16_t x;
//	int16_t y;
//	uint32_t timestamp;
//};
//
//struct s2c_add_npc : packet_header
//{
//	int id;
//	char npcName[MAX_NAME_LEN];
//	int16_t x;
//	int16_t y;
//};
//struct s2c_remove_npc : packet_header
//{
//	int id;
//};
//struct s2c_npc_move : packet_header
//{
//	int id;
//	int16_t x;
//	int16_t y;
//	uint32_t timestamp;
//};
//#pragma pack(pop)

#pragma once

constexpr short PORT = 3500;
constexpr int WORLD_WIDTH = 2000;
constexpr int WORLD_HEIGHT = 2000;
constexpr int MAX_PLAYERS = 10000;
constexpr int NPC_ID_START = MAX_PLAYERS;
constexpr int MAX_NPCS = 20'0000;
constexpr int MAX_NAME_LEN = 20;

enum PACKET_TYPE { C2S_LOGIN, C2S_MOVE, S2C_LOGIN_RESULT, S2C_AVATAR_INFO, S2C_ADD_PLAYER, S2C_REMOVE_PLAYER, S2C_MOVE_PLAYER };
enum DIRECTION { UP, DOWN, LEFT, RIGHT };

#pragma pack(push, 1) // Ensure no padding between struct members
struct C2S_Login {
	unsigned char size;
	PACKET_TYPE   type;
	char user_id[MAX_NAME_LEN];
	char username[MAX_NAME_LEN];
};

struct C2S_Move {
	unsigned char size;
	PACKET_TYPE   type;
	DIRECTION    dir;
	int move_time; // in milliseconds
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
	short x;
	short y;
};

struct S2C_AddPlayer {
	unsigned char size;
	PACKET_TYPE   type;
	int playerId;
	char username[MAX_NAME_LEN];
	short x;
	short y;
};

struct S2C_RemovePlayer {
	unsigned char size;
	PACKET_TYPE   type;
	int playerId;
};

struct S2C_MovePlayer {
	unsigned char size;
	PACKET_TYPE   type;
	int playerId;
	short x;
	short y;
	int move_time; // in milliseconds
};
#pragma pack(pop) // Restore default packing