-- =============================================================================
-- creeper_ai.lua
-- 크리퍼 AI: 플레이어를 향해 접근 → 거리 2 이내 도달 시 경고 → 거리 1 시 자폭(DIE)
-- current_state: 0=IDLE, 1=PATROL(접근중), 4=FLEEING(경고 중)
-- =============================================================================

local STATE_IDLE      = 0
local STATE_PATROL    = 1  -- 접근 중
local STATE_AGGRO     = 2
local STATE_FLEEING   = 4  -- 경고/자폭 카운트다운 (재활용)

local ACTION_IDLE         = 0
local ACTION_MOVE         = 1
local ACTION_MELEE_ATTACK = 2  -- (크리퍼는 사용 안 함, DIE로 대체)
local ACTION_DIE          = 5  -- 자폭! AOE 공격 후 사망

local function approach_dir(from_x, from_y, to_x, to_y)
    local dx = to_x - from_x
    local dy = to_y - from_y
    if math.abs(dx) >= math.abs(dy) then
        return (dx > 0 and 1 or -1), 0
    else
        return 0, (dy > 0 and 1 or -1)
    end
end

function on_timer(ctx)
    if ctx.target_id == -1 or ctx.dist > 20 then
        return { action = ACTION_IDLE, new_state = STATE_IDLE, delay_ms = 1000 }
    end

    -- 거리 1: 자폭!
    if ctx.dist <= 1 then
        return {
            action    = ACTION_DIE,
            target_id = ctx.target_id,
            new_state = STATE_FLEEING,
            chat      = "쉬이이이...", delay_ms = 3000
        }
    end

    -- 거리 2: 경고 메시지 + 계속 접근
    if ctx.dist <= 2 then
        local dx, dy = approach_dir(ctx.self_x, ctx.self_y, ctx.target_x, ctx.target_y)
        return {
            action    = ACTION_MOVE,
            dx        = dx,
            dy        = dy,
            new_state = STATE_FLEEING,
            chat      = "쉿...", delay_ms = 1000
        }
    end

    -- 그 외: 접근
    local dx, dy = approach_dir(ctx.self_x, ctx.self_y, ctx.target_x, ctx.target_y)
    return {
        action    = ACTION_MOVE,
        dx        = dx,
        dy        = dy,
        new_state = STATE_PATROL, delay_ms = 1000
    }
end

function on_hit(ctx, attacker_id, damage)
    -- 피격 시 즉시 달려들기
    return { action = ACTION_IDLE, new_state = STATE_PATROL, delay_ms = 1000 }
end

function on_spawn(ctx)
    return { action = ACTION_IDLE, new_state = STATE_IDLE, delay_ms = 1000 }
end
