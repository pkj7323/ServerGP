-- =============================================================================
-- npc_config.lua
-- 서버 초기화 시 로드. map_spawn.bin의 NPC 타입 ID(1~5)에 대응하는
-- 메타데이터(이름, HP, 레벨, 경험치, 공격력, 시각 ID)를 정의합니다.
--
-- NpcType enum (C++ Protocol.h와 동기화 필수):
--   0 = NPC_NONE       (스폰 없음 - 사용 안 함)
--   1 = NPC_ZOMBIE
--   2 = NPC_SKELETON
--   3 = NPC_CREEPER
--   4 = NPC_ENDERMAN
--   5 = NPC_IRON_GOLEM
-- =============================================================================

NPC_TYPES = {
    -- [type_id] = { name, hp, level, exp, attack, visual_id }
    --   name      : S2C_AddObject.obj_name 에 복사됨
    --   hp        : 초기 HP (max_hp와 동일하게 설정)
    --   level     : NPC 레벨
    --   exp       : 처치 시 플레이어에게 부여하는 경험치
    --   attack    : 기본 공격력
    --   visual_id : S2C_AddObject.visual_id (클라이언트 렌더링용)

    [1] = {
        name       = "Zombie",
        hp         = 100,
        level      = 3,
        exp        = 20,
        attack     = 10,
        visual_id  = 1,
        drop_item  = 1, -- Rotten Flesh
        drop_gold  = 5,
    },
    [2] = {
        name       = "Skeleton",
        hp         = 80,
        level      = 5,
        exp        = 40,
        attack     = 15,
        visual_id  = 2,
        drop_item  = 2, -- Bone
        drop_gold  = 5,
    },
    [3] = {
        name       = "Creeper",
        hp         = 60,
        level      = 7,
        exp        = 60,
        attack     = 40,   -- 폭발 데미지
        visual_id  = 3,
        drop_item  = 3, -- Gunpowder
        drop_gold  = 5,
    },
    [4] = {
        name       = "Enderman",
        hp         = 300,
        level      = 15,
        exp        = 300,
        attack     = 30,
        visual_id  = 4,
        drop_item  = 6, -- Diamond
        drop_gold  = 20,
    },
    [5] = {
        name       = "Iron Golem",
        hp         = 500,
        level      = 10,
        exp        = 200,
        attack     = 50,
        visual_id  = 5,
        drop_item  = 4, -- Iron Ingot
        drop_gold  = 20,
    },
    [6] = {
        name       = "Priest",
        hp         = 9999,
        level      = 99,
        exp        = 0,
        attack     = 0,
        visual_id  = 6,
        drop_item  = 0,
        drop_gold  = 0,
    },
    [7] = {
        name       = "Armorer",
        hp         = 9999,
        level      = 99,
        exp        = 0,
        attack     = 0,
        visual_id  = 7,
        drop_item  = 0,
        drop_gold  = 0,
    },
    [8] = {
        name       = "Weaponsmith",
        hp         = 9999,
        level      = 99,
        exp        = 0,
        attack     = 0,
        visual_id  = 8,
        drop_item  = 0,
        drop_gold  = 0,
    },
    [9] = {
        name       = "Librarian",
        hp         = 9999,
        level      = 99,
        exp        = 0,
        attack     = 0,
        visual_id  = 9,
        drop_item  = 0,
        drop_gold  = 0,
    },
}

-- =============================================================================
-- 마을 상인 고정 스폰 좌표 (마을 중심 주변)
-- =============================================================================
MERCHANT_SPAWNS = {
    { type_id = 6, x = 1005, y = 1005 },
    { type_id = 7, x = 995, y = 1005 },
    { type_id = 8, x = 1005, y = 995 },
    { type_id = 9, x = 995, y = 995 },
}

-- =============================================================================
-- 스폰 존 정보 (Map Generator와 동일한 수치 - 참조용)
-- 실제 스폰 좌표는 map_spawn.bin 바이너리에서 읽어옴
-- =============================================================================
SPAWN_ZONES = {
    { type_id = 5, name = "Iron Golem",      min_radius = 50,  max_radius = 150,  count = 5000  },
    { type_id = 1, name = "Zombie",          min_radius = 150, max_radius = 400,  count = 40000 },
    { type_id = 2, name = "Skeleton",        min_radius = 150, max_radius = 400,  count = 25000 },
    { type_id = 3, name = "Creeper",         min_radius = 400, max_radius = 700,  count = 70000 },
    { type_id = 4, name = "Enderman",        min_radius = 700, max_radius = 9999, count = 60000 },
}

-- =============================================================================
-- 플레이어 초기 스폰 지점 (마을 중심)
-- =============================================================================
PLAYER_SPAWN = {
    x = 1000,  -- WORLD_WIDTH  / 2
    y = 1000,  -- WORLD_HEIGHT / 2
}

-- =============================================================================
-- 총 NPC 수 검증 (서버 초기화 시 assert용)
-- =============================================================================
NPC_TOTAL_COUNT = 200000

-- =============================================================================
-- 서버에서 사용하는 헬퍼 함수
-- =============================================================================

--- type_id로 NPC 메타데이터를 반환합니다.
--- @param type_id number  (1~5)
--- @return table | nil
function get_npc_meta(type_id)
    return NPC_TYPES[type_id]
end

--- type_id가 유효한지 확인합니다.
--- @param type_id number
--- @return boolean
function is_valid_npc_type(type_id)
    return NPC_TYPES[type_id] ~= nil
end
-- =============================================================================
-- Armor Thresholds
-- =============================================================================
ARMOR_THRESHOLDS = {
    COPPER = 20,
    IRON = 100,
    DIAMOND = 200,
    NETHERITE = 300
}
