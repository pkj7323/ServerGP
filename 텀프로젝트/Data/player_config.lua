-- =============================================================================
-- player_config.lua
-- Defines the player's initial stats, movement configuration, etc.
-- =============================================================================

PLAYER_CONFIG = {
    -- Movement limit: milliseconds between tile moves
    -- Server will reject moves faster than this.
    -- Client will predict moves at this rate.
    move_cooldown_ms = 50,

    -- Future expansion: initial hp, level, etc.
    hp = 100,
    max_hp = 100,
    attack = 15,
}

-- helper function
function get_player_config()
    return PLAYER_CONFIG
end
