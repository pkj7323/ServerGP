-- =============================================================================
-- skeleton_ai.lua
-- 스켈레톤 AI: 플레이어와 3~8칸 거리를 유지하며 8방향 직선 원거리 공격
-- 너무 가까워지면 반대 방향으로 도주
-- =============================================================================

local STATE_IDLE      = 0
local STATE_PATROL    = 1
local STATE_AGGRO     = 2

local ACTION_IDLE         = 0
local ACTION_MOVE         = 1
local ACTION_MELEE_ATTACK = 2
local ACTION_RANGE_ATTACK = 3

local PREFER_DIST_MIN = 3  -- 이보다 가까우면 도망
local PREFER_DIST_MAX = 8  -- 이보다 멀면 접근
local ATTACK_RANGE    = 5  -- 공격 사거리

-- 도주 방향 (플레이어 반대)
local function flee_dir(from_x, from_y, to_x, to_y)
    local dx = from_x - to_x  -- 반대 방향
    local dy = from_y - to_y
    if math.abs(dx) >= math.abs(dy) then
        return (dx > 0 and 1 or -1), 0
    else
        return 0, (dy > 0 and 1 or -1)
    end
end

-- 접근 방향
local function approach_dir(from_x, from_y, to_x, to_y)
    local dx = to_x - from_x
    local dy = to_y - from_y
    if math.abs(dx) >= math.abs(dy) then
        return (dx > 0 and 1 or -1), 0
    else
        return 0, (dy > 0 and 1 or -1)
    end
end

-- 8방향 직선 공격 가능 여부 체크 (같은 행/열/대각선인지)
local function can_range_attack(sx, sy, tx, ty)
    local dx = tx - sx
    local dy = ty - sy
    if dx == 0 and dy ~= 0 then return true end   -- 수직
    if dy == 0 and dx ~= 0 then return true end   -- 수평
    if math.abs(dx) == math.abs(dy) then return true end  -- 대각선
    return false
end

function on_timer(ctx)
    if ctx.target_id == -1 or ctx.dist > 20 then
        return { action = ACTION_IDLE, new_state = STATE_IDLE, delay_ms = 1000 }
    end

    -- 너무 가까우면 도주
    if ctx.dist < PREFER_DIST_MIN then
        local dx, dy = flee_dir(ctx.self_x, ctx.self_y, ctx.target_x, ctx.target_y)
        return {
            action    = ACTION_MOVE,
            dx        = dx,
            dy        = dy,
            new_state = STATE_AGGRO, delay_ms = 1000
        }
    end

    -- 사거리 내 + 8방향 직선이면 원거리 공격
    if ctx.dist <= ATTACK_RANGE and can_range_attack(ctx.self_x, ctx.self_y, ctx.target_x, ctx.target_y) then
        return {
            action    = ACTION_RANGE_ATTACK,
            target_id = ctx.target_id,
            new_state = STATE_AGGRO,
            chat      = "딸깍!", delay_ms = 2000
        }
    end

    -- 멀면 접근
    if ctx.dist > PREFER_DIST_MAX then
        local dx, dy = approach_dir(ctx.self_x, ctx.self_y, ctx.target_x, ctx.target_y)
        return {
            action    = ACTION_MOVE,
            dx        = dx,
            dy        = dy,
            new_state = STATE_AGGRO, delay_ms = 1000
        }
    end

    -- 적정 거리 유지 (제자리 대기)
    return { action = ACTION_IDLE, new_state = STATE_AGGRO, delay_ms = 1000 }
end

function on_hit(ctx, attacker_id, damage)
    return { action = ACTION_IDLE, new_state = STATE_AGGRO, delay_ms = 1000 }
end

function on_spawn(ctx)
    return { action = ACTION_IDLE, new_state = STATE_IDLE, delay_ms = 1000 }
end
