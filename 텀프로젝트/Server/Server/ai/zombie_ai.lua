-- =============================================================================
-- zombie_ai.lua
-- 좀비 AI: 플레이어가 보이면 직선 추격, 인접 시 근접 공격
-- 플레이어가 없으면 IDLE (타이머 꺼짐)
-- =============================================================================

-- NpcState 상수 (Protocol.h의 NpcState enum과 동기화)
local STATE_IDLE      = 0
local STATE_PATROL    = 1
local STATE_AGGRO     = 2

-- NpcActionType 상수 (NpcAiManager의 NpcActionType enum과 동기화)
local ACTION_IDLE         = 0
local ACTION_MOVE         = 1
local ACTION_MELEE_ATTACK = 2

-- 방향 결정: 목표 쪽으로 한 칸 이동
local function dir_to(from_x, from_y, to_x, to_y)
    local dx = to_x - from_x
    local dy = to_y - from_y
    -- 더 먼 축을 먼저 이동 (단순 추격)
    if math.abs(dx) >= math.abs(dy) then
        return (dx > 0 and 1 or -1), 0
    else
        return 0, (dy > 0 and 1 or -1)
    end
end

-- ─────────────────────────────────────────────────────────────────────────────
-- on_timer(ctx): EVENT_MOVE 타이머 만료 시 호출
-- ─────────────────────────────────────────────────────────────────────────────
function on_timer(ctx)
    -- 플레이어가 없으면 IDLE
    if ctx.target_id == -1 or ctx.dist > 20 then
        return { action = ACTION_IDLE, new_state = STATE_IDLE, delay_ms = 1000 }
    end

    -- 인접(거리 1)이면 근접 공격
    if ctx.dist <= 1 then
        return {
            action    = ACTION_MELEE_ATTACK,
            target_id = ctx.target_id,
            new_state = STATE_AGGRO,
            chat      = "크르르!", delay_ms = 2000
        }
    end

    -- 그 외: 플레이어를 향해 이동
    local dx, dy = dir_to(ctx.self_x, ctx.self_y, ctx.target_x, ctx.target_y)
    return {
        action    = ACTION_MOVE,
        dx        = dx,
        dy        = dy,
        new_state = STATE_AGGRO, delay_ms = 1000
    }
end

-- ─────────────────────────────────────────────────────────────────────────────
-- on_hit(ctx, attacker_id, damage): 피격 시 호출
-- ─────────────────────────────────────────────────────────────────────────────
function on_hit(ctx, attacker_id, damage)
    -- 피격 즉시 AGGRO로 전환 (이미 AGGRO면 유지)
    return { action = ACTION_IDLE, new_state = STATE_AGGRO, delay_ms = 1000 }
end

-- ─────────────────────────────────────────────────────────────────────────────
-- on_spawn(ctx): 스폰 시 초기화
-- ─────────────────────────────────────────────────────────────────────────────
function on_spawn(ctx)
    return { action = ACTION_IDLE, new_state = STATE_IDLE, delay_ms = 1000 }
end
