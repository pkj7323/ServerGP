#pragma once
// ID Type Flags
constexpr int ID_TYPE_PLAYER = 0x00000000;
constexpr int ID_TYPE_NPC = 0x40000000;
constexpr int ID_INDEX_MASK = 0x3FFFFFFF;

static bool is_npc_id(int id)
{
	return (id & ~ID_INDEX_MASK) == ID_TYPE_NPC;
}

static bool is_player_id(int id)
{
	return (id & ~ID_INDEX_MASK) == ID_TYPE_PLAYER;
}

// Constants for rendering
constexpr int CELL_SIZE = 65;
constexpr int VIEW_WIDTH = 40;
constexpr int VIEW_HEIGHT = 22;
constexpr int WINDOW_WIDTH = VIEW_WIDTH * CELL_SIZE;
constexpr int WINDOW_HEIGHT = VIEW_HEIGHT * CELL_SIZE;