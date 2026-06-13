-- =============================================================================
-- enderman_ai.lua
-- 엔더맨 AI: 기본적으로 IDLE. 플레이어가 5칸 이내로 오면 AGGRO 전환.
-- AGGRO 시: 플레이어 바로 옆으로 텔레포트(이동) + 강력 근접 공격
-- current_state 2(AGGRO)면 계속 추격
-- =============================================================================

local STATE_IDLE   = 0
local STATE_PATROL = 1
local STATE_AGGRO  = 2

local ACTION_IDLE         = 0
local ACTION_MOVE         = 1
local ACTION_MELEE_ATTACK = 2

local AGGRO_RANGE   = 5   -- 이 거리 안에 오면 어그로
local TELEPORT_DIST = 3   -- 텔레포트로 한번에 이동하는 최대 거리
                           -- (C++ 쪽에서 실제 이동량은 min(TELEPORT_DIST, dist-1)으로 처리)

-- 플레이어를 향해 빠르게 이동 (텔레포트 느낌: 한 틱에 멀리 이동)
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
    -- IDLE 상태에서 플레이어 범위 밖 → 계속 IDLE
    if ctx.target_id == -1 then
        return { action = ACTION_IDLE, new_state = STATE_IDLE, delay_ms = 500 }
    end

    -- 플레이어가 AGGRO_RANGE 이내: AGGRO 전환
    if ctx.dist <= AGGRO_RANGE or ctx.current_state == STATE_AGGRO then
        -- 인접 시 공격
        if ctx.dist <= 1 then
            return {
                action    = ACTION_MELEE_ATTACK,
                target_id = ctx.target_id,
                new_state = STATE_AGGRO,
                chat      = "...", delay_ms = 2000
            }
        end
        -- 텔레포트 이동 (dx/dy 신호는 C++에서 TELEPORT_DIST만큼 배수 적용)
        local dx, dy = approach_dir(ctx.self_x, ctx.self_y, ctx.target_x, ctx.target_y)
        return {
            action    = ACTION_MOVE,
            dx        = dx * TELEPORT_DIST,
            dy        = dy * TELEPORT_DIST,
            new_state = STATE_AGGRO, delay_ms = 500
        }
    end

    -- 멀면 그냥 IDLE
    return { action = ACTION_IDLE, new_state = STATE_IDLE, delay_ms = 500 }
end

function on_hit(ctx, attacker_id, damage)
    -- 피격 즉시 AGGRO
    return {
        action    = ACTION_IDLE,
        new_state = STATE_AGGRO, delay_ms = 500
    }
end

function on_spawn(ctx)
    return { action = ACTION_IDLE, new_state = STATE_IDLE, delay_ms = 500 }
end
