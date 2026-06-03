import re

with open('Client/Client/RenderManager.cpp', 'r', encoding='utf-8') as f:
    code = f.read()

# 1. Insert interpolation logic at the beginning of Render()
interp_logic = r"""
	static auto last_time = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	float dt = std::chrono::duration<float>(now - last_time).count();
	last_time = now;
	if (dt > 0.1f) dt = 0.1f; // cap dt

	float speed = 10.0f; // interpolation speed
	for (auto& [id, player] : gm->players()) {
		player.render_x += (player.x - player.render_x) * speed * dt;
		player.render_y += (player.y - player.render_y) * speed * dt;
	}
	for (auto& [id, npc] : gm->npcs()) {
		npc.render_x += (npc.x - npc.render_x) * speed * dt;
		npc.render_y += (npc.y - npc.render_y) * speed * dt;
	}

	float left_x = 0.0f, bottom_y = 0.0f;
	int my_id = gm->my_id();
	auto& players = gm->players();
	auto& npcs = gm->npcs();

	if (players.contains(my_id)) {
		left_x = players[my_id].render_x - VIEW_WIDTH / 2.0f;
		bottom_y = players[my_id].render_y - VIEW_HEIGHT / 2.0f;
		left_x = std::clamp(left_x, 0.0f, (float)(WORLD_WIDTH - VIEW_WIDTH));
		bottom_y = std::clamp(bottom_y, 0.0f, (float)(WORLD_HEIGHT - VIEW_HEIGHT));
	}
"""

# Replace from `int left_x = 0, bottom_y = 0;` down to `}`
old_camera_logic = r"""	int left_x = 0, bottom_y = 0;
	int my_id = gm->my_id();
	auto& players = gm->players();
	auto& npcs = gm->npcs();

	if (players.contains(my_id)) {
		left_x = players[my_id].x - VIEW_WIDTH / 2;
		bottom_y = players[my_id].y - VIEW_HEIGHT / 2;
		left_x = std::clamp(left_x, 0, WORLD_WIDTH - VIEW_WIDTH);
		bottom_y = std::clamp(bottom_y, 0, WORLD_HEIGHT - VIEW_HEIGHT);
	}"""

code = code.replace(old_camera_logic, interp_logic.strip())

# 2. Replace npc render loop coordinates
code = code.replace("int rel_x = npc.x - left_x;", "float rel_x = npc.render_x - left_x;")
code = code.replace("int rel_y = npc.y - bottom_y;", "float rel_y = npc.render_y - bottom_y;")
# screen_y needs to be float as well to keep smooth vertical movement
code = code.replace("int screen_y = (VIEW_HEIGHT - 1 - rel_y);", "float screen_y = (VIEW_HEIGHT - 1.0f - rel_y);")
code = code.replace("int px = rel_x * cellWidth + padX;", "int px = (int)(rel_x * cellWidth) + padX;")
code = code.replace("int py = screen_y * cellHeight + padY;", "int py = (int)(screen_y * cellHeight) + padY;")

# 3. Replace player render loop coordinates
code = code.replace("int rel_x = player.x - left_x;", "float rel_x = player.render_x - left_x;")
code = code.replace("int rel_y = player.y - bottom_y;", "float rel_y = player.render_y - bottom_y;")

# 4. Replace chat bubble coordinates
code = code.replace("int rel_x = obj.x - left_x;", "float rel_x = obj.render_x - left_x;")
code = code.replace("int rel_y = obj.y - bottom_y;", "float rel_y = obj.render_y - bottom_y;")
code = code.replace("int px = rel_x * cellWidth + (cellWidth / 2);", "int px = (int)(rel_x * cellWidth) + (cellWidth / 2);")
code = code.replace("int py = screen_y * cellHeight - 30;", "int py = (int)(screen_y * cellHeight) - 30;")

# 5. Helmets
code = code.replace("int py = screen_y * cellHeight;", "int py = (int)(screen_y * cellHeight);")
# 6. Swords
code = code.replace("int py = screen_y * cellHeight + (cellHeight / 2);", "int py = (int)(screen_y * cellHeight) + (cellHeight / 2);")

with open('Client/Client/RenderManager.cpp', 'w', encoding='utf-8') as f:
    f.write(code)
