-- =============================================================================
-- item_config.lua
-- Defines the items that can be dropped and stored in the inventory.
-- =============================================================================

ITEMS = {
    [1] = { name = "Rotten Flesh", image = "rotten_flesh.png" },
    [2] = { name = "Bone",         image = "bone.png" },
    [3] = { name = "Gunpowder",    image = "gunpowder.png" },
    [4] = { name = "Iron Ingot",   image = "iron_ingot.png" },
    [5] = { name = "Gold Ingot",   image = "gold_ingot.png" },
    [6] = { name = "Diamond",      image = "diamond.png" },
}

-- helper functions
function get_item_meta(item_id)
    return ITEMS[item_id]
end

function is_valid_item(item_id)
    return ITEMS[item_id] ~= nil
end
