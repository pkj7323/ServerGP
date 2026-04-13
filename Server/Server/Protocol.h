#pragma once
#include <cstdint>

constexpr short PORT = 9001;
constexpr int WORLD_WIDTH = 400;
constexpr int WORLD_HEIGHT = 400;
constexpr int MAX_PLAYERS = 1000;
constexpr int MAX_NAME_LEN = 20;

enum class packet_type : uint16_t
{
	C2S_LOGIN = 0,
	C2S_MOVE = 1,


	S2C_LOGIN_ACK = 12,
	S2C_AVATAR_INFO = 13,
	S2C_ADD_PLAYER = 14,
	S2C_PLAYER_MOVE = 15,
	S2C_REMOVE_PLAYER = 16,
};
#pragma pack(push, 1)
struct packet_header
{
	unsigned char size;
	packet_type type;
};

//----------------------------------------------------------------------------
// 클라이언트 -> 서버 패킷
struct c2s_login : packet_header
{
	char userName[MAX_NAME_LEN];
};
struct c2s_move : packet_header
{
	struct vec2
	{
		int16_t x;
		int16_t y;
	} dir;
	uint32_t timestamp;
};

//-----------------------------------------------------------------------------
// 서버 -> 클라이언트 패킷
struct s2c_login_ack : packet_header
{
	bool success;
	char msg[50];
};

struct s2c_avatar_info : packet_header
{
	int id; // 플레이어 아이디는 적어도 int32_t 이상이어야 합니다.
	int16_t x;
	int16_t y;
};

struct s2c_add_player : packet_header
{
	int id;
	char userName[MAX_NAME_LEN];
	int16_t x;
	int16_t y;
};
struct s2c_remove_player : packet_header
{
	int id;
};
struct s2c_player_move : packet_header
{
	int id;
	int16_t x;
	int16_t y;
	uint32_t timestamp;
};
#pragma pack(pop)