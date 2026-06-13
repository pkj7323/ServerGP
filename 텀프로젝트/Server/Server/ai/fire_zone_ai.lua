-- fire_zone_ai.lua
-- 화염 장판 (type_id = 12)

function on_timer(ctx)
    local ZONE_DAMAGE = 8
    -- 장판은 계속해서 ZONE_DAMAGE 액션을 수행하며, C++쪽에서 체력을 깎고 사라집니다.
    return { action = ZONE_DAMAGE, delay_ms = 1000 }
end

function on_hit(ctx, attacker_id, damage)
    return { action = 0, delay_ms = 0 } -- 장판은 피격에 무반응
end

function on_spawn(ctx)
    return { action = 0, delay_ms = 1000 }
end
