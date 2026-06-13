-- ender_dragon_ai.lua
-- 엔더 드래곤 (type_id = 11)

function on_timer(ctx)
    -- NpcActionType enum
    local IDLE = 0
    local MOVE = 1
    local MELEE = 2
    local RANGE = 3
    local CHAT = 4
    local DIE = 5
    local AOE = 6
    local BREATH = 7

    local hp_percent = ctx.self_hp / ctx.self_max_hp

    -- 2페이즈 전환 (HP <= 50% 이고 현재 1페이즈일 때)
    if hp_percent <= 0.50 and ctx.current_state == 2 then
        return {
            action = CHAT,
            new_state = 3, -- 3을 2페이즈 상태로 사용 (ATTACKING)
            chat = "크롸롸롸롸! (2페이즈 돌입)",
            delay_ms = 2000
        }
    end

    -- 타겟 확인
    if ctx.target_id < 0 or ctx.dist > 15 then
        return { action = MOVE, dx = math.random(-1, 1), dy = math.random(-1, 1), delay_ms = 500 }
    end

    -- 페이즈별 행동 결정
    if hp_percent > 0.50 then
        -- 1페이즈: 근접 평타, 꼬리치기(AOE), 이동
        if ctx.dist <= 1 then
            local r = math.random(1, 100)
            if r <= 60 then
                return { action = MELEE, target_id = ctx.target_id, delay_ms = 500 }
            else
                return { action = AOE, delay_ms = 1500 }
            end
        else
            -- 3칸 이내면 가끔 꼬리치기
            if ctx.dist <= 3 and math.random(1, 100) <= 30 then
                return { action = AOE, delay_ms = 1500 }
            else
                return { action = MOVE, dx = ctx.target_x - ctx.self_x, dy = ctx.target_y - ctx.self_y, delay_ms = 500 }
            end
        end
    else
        -- 2페이즈: 돌진(MOVE 활용), 파이어볼 브레스
        if ctx.dist <= 1 then
            return { action = MELEE, target_id = ctx.target_id, delay_ms = 800 }
        elseif ctx.dist <= 8 then
            local r = math.random(1, 100)
            if r <= 50 then
                -- 브레스
                return { action = BREATH, target_id = ctx.target_id, delay_ms = 2000 }
            else
                -- 돌진 (빠르게 타겟 방향으로 이동)
                return { action = MOVE, dx = ctx.target_x - ctx.self_x, dy = ctx.target_y - ctx.self_y, delay_ms = 500 }
            end
        else
            return { action = MOVE, dx = ctx.target_x - ctx.self_x, dy = ctx.target_y - ctx.self_y, delay_ms = 500 }
        end
    end
end

function on_hit(ctx, attacker_id, damage)
    return { action = 0, new_state = 2, target_id = attacker_id, delay_ms = 0 }
end

function on_spawn(ctx)
    return { action = 0, new_state = 2, delay_ms = 500 }
end
