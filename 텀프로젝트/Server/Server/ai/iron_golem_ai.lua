-- =============================================================================
-- iron_golem_ai.lua
-- 아이언 골렘 AI: 스폰 지점 반경 15칸 순찰
-- 플레이어가 8칸 이내 접근 시 수호자 모드 (AGGRO) → 강력 근접 공격
-- 플레이어가 멀어지면 스폰 지점으로 복귀
-- =============================================================================

local STATE_IDLE   = 0
local STATE_PATROL = 1
local STATE_AGGRO  = 2

local ACTION_IDLE         = 0
local ACTION_MOVE         = 1
local ACTION_MELEE_ATTACK = 2

local PATROL_RADIUS = 10   -- 스폰 지점 기준 순찰 반경
local AGGRO_RANGE   = 3    -- 이 거리 안에 오면 어그로

local function approach_dir(from_x, from_y, to_x, to_y)
    local dx = to_x - from_x
    local dy = to_y - from_y
    if math.abs(dx) >= math.abs(dy) then
        return (dx > 0 and 1 or -1), 0
    else
        return 0, (dy > 0 and 1 or -1)
    end
end

-- 스폰 지점으로부터의 맨해튼 거리
local function dist_from_spawn(ctx)
    return math.abs(ctx.self_x - ctx.initial_x) + math.abs(ctx.self_y - ctx.initial_y)
end

function on_timer(ctx)
    -- 플레이어가 AGGRO 범위 안에 있으면 수호자 모드
    if ctx.target_id ~= -1 and ctx.dist <= AGGRO_RANGE then
        if ctx.dist <= 1 then
            return {
                action    = ACTION_MELEE_ATTACK,
                target_id = ctx.target_id,
                new_state = STATE_AGGRO,
                chat      = "침입자!", delay_ms = 3000
            }
        end
        local dx, dy = approach_dir(ctx.self_x, ctx.self_y, ctx.target_x, ctx.target_y)
        return {
            action    = ACTION_MOVE,
            dx        = dx,
            dy        = dy,
            new_state = STATE_AGGRO, delay_ms = 500
        }
    end

    -- 순찰 영역 벗어났으면 복귀
    if dist_from_spawn(ctx) > PATROL_RADIUS then
        local dx, dy = approach_dir(ctx.self_x, ctx.self_y, ctx.initial_x, ctx.initial_y)
        return {
            action    = ACTION_MOVE,
            dx        = dx,
            dy        = dy,
            new_state = STATE_PATROL, delay_ms = 500
        }
    end

    -- 순찰: 랜덤 이동
    local dirs = { {1,0}, {-1,0}, {0,1}, {0,-1} }
    local d = dirs[math.random(1, 4)]
    return {
        action    = ACTION_MOVE,
        dx        = d[1],
        dy        = d[2],
        new_state = STATE_PATROL, delay_ms = 5000
    }
end

function on_hit(ctx, attacker_id, damage)
    -- 피격 즉시 AGGRO
    return { action = ACTION_IDLE, new_state = STATE_AGGRO, delay_ms = 500 }
end

function on_spawn(ctx)
    return { action = ACTION_IDLE, new_state = STATE_PATROL, delay_ms = 5000 }
end
