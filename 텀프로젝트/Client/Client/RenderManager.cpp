#include "pch.h"
#include "RenderManager.h"
#include "GameManager.h"

#pragma comment(lib, "msimg32.lib")

static void DrawChatBubbles(Gdiplus::Graphics& graphics, Gdiplus::Font& font, float left_x, float bottom_y, int cellWidth, int cellHeight, std::unordered_map<int, Object>& obj_map) {
	for (auto& [id, obj] : obj_map) {
		if (obj.chat_msg.empty()) continue;
		auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::seconds>(now - obj.chat_time).count() > 4) {
			obj.chat_msg.clear(); // Expire after 4 seconds
			continue;
		}

		float rel_x = obj.render_x - left_x;
		float rel_y = obj.render_y - bottom_y;
		if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
			float screen_y = (VIEW_HEIGHT - 1.0f - rel_y);
			int px = (int)(rel_x * cellWidth) + (cellWidth / 2);
			int py = (int)(screen_y * cellHeight) - 30; // Above head

			Gdiplus::RectF bounds;
			graphics.MeasureString(obj.chat_msg.c_str(), -1, &font, Gdiplus::PointF(0, 0), &bounds);
			
			Gdiplus::SolidBrush bubbleBrush(Gdiplus::Color(220, 255, 255, 255));
			Gdiplus::Color textColor = (obj.chat_msg.find(L"Fire aspect -") == 0) ? 
				Gdiplus::Color(255, 255, 0, 0) : Gdiplus::Color(255, 0, 0, 0);
			Gdiplus::SolidBrush textBrush(textColor);
			
			graphics.FillRectangle(&bubbleBrush, px - bounds.Width / 2 - 5, py - bounds.Height - 5, bounds.Width + 10, bounds.Height + 10);
			graphics.DrawString(obj.chat_msg.c_str(), -1, &font, Gdiplus::PointF(px - bounds.Width / 2, py - bounds.Height), &textBrush);
		}
	}
}

RenderManager::~RenderManager()
{
	if (_hBoardBmp) {
		DeleteObject(_hBoardBmp);
		_hBoardBmp = NULL;
	}
	for (int i = 1; i <= 4; ++i) {
		if (_helmets[i]) { DeleteObject(_helmets[i]); _helmets[i] = nullptr; }
		if (_chestplates[i]) { DeleteObject(_chestplates[i]); _chestplates[i] = nullptr; }
		if (_leggings[i]) { DeleteObject(_leggings[i]); _leggings[i] = nullptr; }
		if (_boots[i]) { DeleteObject(_boots[i]); _boots[i] = nullptr; }
	}
	for (int i = 1; i <= 6; ++i) {
		if (_swords[i]) {
			delete _swords[i];
			_swords[i] = nullptr;
		}
	}
	if (_player_head) { DeleteObject(_player_head); _player_head = nullptr; }
	for (int i = 1; i <= 10; ++i) {
		if (_mob_heads[i]) { DeleteObject(_mob_heads[i]); _mob_heads[i] = nullptr; }
	}
	if (_gold_icon) { DeleteObject(_gold_icon); _gold_icon = nullptr; }
	for (int i = 0; i < 20; ++i) {
		if (_item_images[i]) { DeleteObject(_item_images[i]); _item_images[i] = nullptr; }
	}
	if (_grass_img) { DeleteObject(_grass_img); _grass_img = nullptr; }
	if (_oak_sapling_img) { DeleteObject(_oak_sapling_img); _oak_sapling_img = nullptr; }
	if (_spruce_sapling_img) { DeleteObject(_spruce_sapling_img); _spruce_sapling_img = nullptr; }
	if (_cactus_img) { DeleteObject(_cactus_img); _cactus_img = nullptr; }
	if (_health_potion_img) { DeleteObject(_health_potion_img); _health_potion_img = nullptr; }
	if (_blaze_powder_img) { DeleteObject(_blaze_powder_img); _blaze_powder_img = nullptr; }
	if (_shield_img) { DeleteObject(_shield_img); _shield_img = nullptr; }
	if (_arrow_img) { delete _arrow_img; _arrow_img = nullptr; }
	if (_fire_img) { delete _fire_img; _fire_img = nullptr; }
}

void RenderManager::Release()
{
	if (_hBoardBmp) {
		DeleteObject(_hBoardBmp);
		_hBoardBmp = NULL;
	}
	for (int i = 1; i <= 4; ++i) {
		if (_helmets[i]) {
			DeleteObject(_helmets[i]);
			_helmets[i] = nullptr;
		}
	}
	for (int i = 1; i <= 6; ++i) {
		if (_swords[i]) {
			delete _swords[i];
			_swords[i] = nullptr;
		}
	}
	if (_player_head) { DeleteObject(_player_head); _player_head = nullptr; }
	for (int i = 1; i <= 10; ++i) {
		if (_mob_heads[i]) { DeleteObject(_mob_heads[i]); _mob_heads[i] = nullptr; }
	}
	if (_gold_icon) { DeleteObject(_gold_icon); _gold_icon = nullptr; }
	for (int i = 0; i < 20; ++i) {
		if (_item_images[i]) { DeleteObject(_item_images[i]); _item_images[i] = nullptr; }
	}
	if (_grass_img) { DeleteObject(_grass_img); _grass_img = nullptr; }
	if (_oak_sapling_img) { DeleteObject(_oak_sapling_img); _oak_sapling_img = nullptr; }
	if (_spruce_sapling_img) { DeleteObject(_spruce_sapling_img); _spruce_sapling_img = nullptr; }
	if (_cactus_img) { DeleteObject(_cactus_img); _cactus_img = nullptr; }
	if (_health_potion_img) { DeleteObject(_health_potion_img); _health_potion_img = nullptr; }
	if (_blaze_powder_img) { DeleteObject(_blaze_powder_img); _blaze_powder_img = nullptr; }
	if (_shield_img) { DeleteObject(_shield_img); _shield_img = nullptr; }
	if (_arrow_img) { delete _arrow_img; _arrow_img = nullptr; }
	if (_fire_img) { delete _fire_img; _fire_img = nullptr; }
	if (_end_portal_frame_img) { DeleteObject(_end_portal_frame_img); _end_portal_frame_img = nullptr; }
	if (_end_portal_img) { DeleteObject(_end_portal_img); _end_portal_img = nullptr; }
	if (_end_stone_img) { DeleteObject(_end_stone_img); _end_stone_img = nullptr; }
}

void RenderManager::Render(HWND hWnd)
{
	auto gm = GameManager::Instance();
	HDC hdc = GetDC(hWnd);
	RECT clientRect;
	GetClientRect(hWnd, &clientRect);
	int width = clientRect.right - clientRect.left;
	int height = clientRect.bottom - clientRect.top;
	if (width <= 0 || height <= 0) { ReleaseDC(hWnd, hdc); return; }

	HDC memDC = CreateCompatibleDC(hdc);
	HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
	HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
	HDC memDC2 = CreateCompatibleDC(hdc);

	// --- GDI+ UI Rendering (Graphics instance pulled up for head and tile rendering) ---
	Gdiplus::Graphics graphics(memDC);
	
	// 마인크래프트 특유의 도트(픽셀) 감성을 살리기 위해 NearestNeighbor(근접 보간) 필터 적용!
	graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
	graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf); // 픽셀 어긋남 방지

	// 배경 (검정색)
	FillRect(memDC, &clientRect, (HBRUSH)GetStockObject(BLACK_BRUSH));

	// 매크로가 없으면 15x15 기준으로 설정
	int cellWidth = width / VIEW_WIDTH;
	int cellHeight = height / VIEW_HEIGHT;

	// Viewport 계산 (Y-up 기준: bottom_y가 화면 맨 밑 줄)
static auto last_time = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	float dt = std::chrono::duration<float>(now - last_time).count();
	last_time = now;
	dt = std::min(dt, 0.1f); // cap dt

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
		// 고전 RPG 스타일: 카메라는 실제 타일(x, y) 단위로만 이동하여 그리드 렌더링 유지
		left_x = static_cast<float>(players[my_id].x - VIEW_WIDTH / 2);
		bottom_y = static_cast<float>(players[my_id].y - VIEW_HEIGHT / 2);
		left_x = std::clamp(left_x, 0.0f, (float)(WORLD_WIDTH - VIEW_WIDTH));
		bottom_y = std::clamp(bottom_y, 0.0f, (float)(WORLD_HEIGHT - VIEW_HEIGHT));
	}

	// 1. 지형 렌더링
	for (int y = 0; y < VIEW_HEIGHT; ++y) {
		for (int x = 0; x < VIEW_WIDTH; ++x) {
			int world_x = static_cast<int>(left_x) + x;
			int world_y = static_cast<int>(bottom_y) + y; // y=0이 화면 밑바닥

			// GDI 렌더링을 위해 모니터 좌표계로 변환 (상하 반전)
			int screen_y = (VIEW_HEIGHT - 1 - y);
			RECT rect = { x * cellWidth, screen_y * cellHeight, (x + 1) * cellWidth, (screen_y + 1) * cellHeight };

			uint8_t tile_id = gm->get_visual_tile(world_x, world_y);
			COLORREF color;
			bool is_oak = false, is_spruce = false, is_cactus = false;
			bool is_end_portal_frame = false, is_end_portal = false;
			bool is_end_stone = false;
			
			if (tile_id == 10) { is_oak = true; tile_id = 1; }
			else if (tile_id == 11) { is_spruce = true; tile_id = 6; }
			else if (tile_id == 12) { is_cactus = true; tile_id = 4; }
			else if (tile_id == 13) { is_end_portal_frame = true; tile_id = 1; }
			else if (tile_id == 14) { is_end_portal = true; tile_id = 1; }
			else if (tile_id == 15) { is_end_stone = true; tile_id = 1; }

			switch (tile_id) {
			case 0: color = RGB(65, 105, 225); break;  // WATER
			case 1: color = RGB(34, 139, 34); break;   // GRASS
			case 2: color = RGB(139, 69, 19); break;   // DIRT
			case 3: color = RGB(128, 128, 128); break; // STONE
			case 4: color = RGB(238, 214, 175); break; // SAND
			case 5: color = RGB(210, 180, 140); break; // SANDSTONE
			case 6: color = RGB(255, 250, 250); break; // SNOW
			case 7: color = RGB(175, 238, 238); break; // ICE
			default: color = RGB(47, 79, 79); break;   // 기본
			}

			if (is_end_stone) {
				if (_end_stone_img) {
					DrawBmpTransparent(memDC, memDC2, _end_stone_img, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
				} else {
					HBRUSH hBrush = CreateSolidBrush(RGB(222, 229, 168)); // End Stone fallback color
					FillRect(memDC, &rect, hBrush);
					DeleteObject(hBrush);
				}
			} else if (tile_id == 1 && _grass_img) {
				DrawBmpTransparent(memDC, memDC2, _grass_img, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
			} else {
				HBRUSH hBrush = CreateSolidBrush(color);
				FillRect(memDC, &rect, hBrush);
				DeleteObject(hBrush);
			}

			if (is_oak && _oak_sapling_img) {
				DrawBmpTransparent(memDC, memDC2, _oak_sapling_img, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
			} else if (is_spruce && _spruce_sapling_img) {
				DrawBmpTransparent(memDC, memDC2, _spruce_sapling_img, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
			} else if (is_cactus && _cactus_img) {
				DrawBmpTransparent(memDC, memDC2, _cactus_img, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
			} else if (is_end_portal_frame && _end_portal_frame_img) {
				DrawBmpTransparent(memDC, memDC2, _end_portal_frame_img, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
			} else if (is_end_portal && _end_portal_img) {
				DrawBmpTransparent(memDC, memDC2, _end_portal_img, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
			}
		}
	}
	SetBkMode(memDC, TRANSPARENT);
	SetTextColor(memDC, RGB(255, 255, 255));
	
	Gdiplus::FontFamily fontFamily(L"Malgun Gothic");
	Gdiplus::Font font(&fontFamily, 14, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	
	// 2. NPC 렌더링
	for (auto& [id, npc] : npcs) {
		float rel_x = npc.render_x - left_x;
		float rel_y = npc.render_y - bottom_y;

		bool is_boss = (npc.visual_id == 11);
		float margin = is_boss ? 8.0f : 2.0f;
		if (rel_x >= -margin && rel_x < VIEW_WIDTH + margin && rel_y >= -margin && rel_y < VIEW_HEIGHT + margin) {
			float screen_y = (VIEW_HEIGHT - 1.0f - rel_y); // NPC도 Y 반전

			int padX = cellWidth * 15 / 100;
			int padY = cellHeight * 15 / 100;
			int px = (int)(rel_x * cellWidth) + padX;
			int py = (int)(screen_y * cellHeight) + padY;
			int imgW = cellWidth - padX * 2;
			int imgH = cellHeight - padY * 2;

			if (npc.visual_id == 11) {
				int boss_w = cellWidth * 9;
				int boss_h = cellHeight * 9;
				int bx = (int)(rel_x * cellWidth) - cellWidth * 4 + padX;
				int by = (int)(screen_y * cellHeight) - cellHeight * 4 + padY;

				int eff_type = -1;
				for (auto& eff : gm->attack_effects()) {
					if (eff.id == id) {
						eff_type = eff.attack_type;
						break;
					}
				}

				if (eff_type == 5 && _ender_dragon_full_img) { // Tail attack (Rotate)
					float angle = (GetTickCount() % 1000) / 1000.0f * 360.0f;
					graphics.TranslateTransform(bx + boss_w/2.0f, by + boss_h/2.0f);
					graphics.RotateTransform(angle);
					Gdiplus::RectF destRect(-boss_w/2.0f, -boss_h/2.0f, (float)boss_w, (float)boss_h);
					graphics.DrawImage(_ender_dragon_full_img, destRect, 0, 0, 250.0f, 250.0f, Gdiplus::UnitPixel);
					graphics.ResetTransform();
				} else if (eff_type == 6 && _ender_dragon_full_img) { // Breath (Frame 64)
					Gdiplus::RectF destRect(bx, by, boss_w, boss_h);
					graphics.DrawImage(_ender_dragon_full_img, destRect, 0, 64 * 250.0f, 250.0f, 250.0f, Gdiplus::UnitPixel);
				} else {
					if (_ender_dragon_full_img) {
						int off_y = 0;
						if (eff_type == 0) { // Melee lunge
							off_y = (GetTickCount() % 200 < 100) ? 10 : -10;
						}
						int frame = (GetTickCount() / 150) % 8; // Use first 8 frames for walking
						Gdiplus::RectF destRect(bx, by + off_y, boss_w, boss_h);
						graphics.DrawImage(_ender_dragon_full_img, destRect, 0, frame * 250.0f, 250.0f, 250.0f, Gdiplus::UnitPixel);
					}
				}
			} else if (npc.visual_id == 12) {
				if (_fire_img) {
					auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
					int frame = (now_ms / 31) % 32;
					int srcY = frame * 16;
					for (int dy = -1; dy <= 1; dy++) {
						for (int dx = -1; dx <= 1; dx++) {
							Gdiplus::Rect destRect(px + dx * cellWidth, py + dy * cellHeight, imgW, imgH);
							graphics.DrawImage(_fire_img, destRect, 0, srcY, 16, 16, Gdiplus::UnitPixel);
						}
					}
				}
			} else if (npc.visual_id > 0 && npc.visual_id <= 10 && _mob_heads[npc.visual_id]) {
				DrawBmpTransparent(memDC, memDC2, _mob_heads[npc.visual_id], px, py, imgW, imgH);
			} else {
				HBRUSH hBrush = CreateSolidBrush(RGB(0, 255, 0));
				HBRUSH oldB = (HBRUSH)SelectObject(memDC, hBrush);
				Ellipse(memDC, px, py, px + imgW, py + imgH);
				SelectObject(memDC, oldB);
				DeleteObject(hBrush);
			}

			// --- 화염 이펙트 (Fire Aspect) ---
			auto now = std::chrono::steady_clock::now();
			if (now < npc.fire_end_time && _fire_img) {
				auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
				int frame = (now_ms / 31) % 32; // 32프레임 애니메이션 (약 1초 순환)
				int srcY = frame * 16;
				
				Gdiplus::Rect destRect(px, py, imgW, imgH);
				graphics.DrawImage(_fire_img, destRect, 0, srcY, 16, 16, Gdiplus::UnitPixel);
			}

			// --- 이름 텍스트 추가 (그림 위) ---
			if (npc.visual_id != 12) {
				std::wstring wName(npc.name.begin(), npc.name.end());
				Gdiplus::SolidBrush nameBrush(Gdiplus::Color(255, 255, 255, 255));
				Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(255, 0, 0, 0));
				graphics.DrawString(wName.c_str(), -1, &font, Gdiplus::PointF((float)(px + 1), (float)(py - 19)), &shadowBrush);
				graphics.DrawString(wName.c_str(), -1, &font, Gdiplus::PointF((float)px, (float)(py - 20)), &nameBrush);

				// --- 체력(HP) 바 그리기 ---
				std::wstring wHp = L"HP: " + std::to_wstring(npc.hp) + L"/" + std::to_wstring(npc.max_hp);
				Gdiplus::SolidBrush hpBrush(Gdiplus::Color(255, 255, 100, 100)); // 연한 빨강색
				graphics.DrawString(wHp.c_str(), -1, &font, Gdiplus::PointF((float)(px + 1), (float)(py - 34)), &shadowBrush);
				graphics.DrawString(wHp.c_str(), -1, &font, Gdiplus::PointF((float)px, (float)(py - 35)), &hpBrush);
			}
		}
	}

	// 3. Player 렌더링 (동일하게 Y 반전)
	for (auto& [id, player] : players) {
		float rel_x = player.render_x - left_x;
		float rel_y = player.render_y - bottom_y;

		if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
			float screen_y = (VIEW_HEIGHT - 1.0f - rel_y); // 플레이어도 Y 반전

			int padX = cellWidth * 15 / 100;
			int padY = cellHeight * 15 / 100;
			int px = (int)(rel_x * cellWidth) + padX;
			int py = (int)(screen_y * cellHeight) + padY;
			int imgW = cellWidth - padX * 2;
			int imgH = cellHeight - padY * 2;

			if (_player_head) {
				DrawBmpTransparent(memDC, memDC2, _player_head, px, py, imgW, imgH);
			} else {
				HBRUSH hBrush = CreateSolidBrush(id == my_id ? RGB(255, 0, 0) : RGB(0, 0, 255));
				HBRUSH oldB = (HBRUSH)SelectObject(memDC, hBrush);
				Ellipse(memDC, px, py, px + imgW, py + imgH);
				SelectObject(memDC, oldB);
				DeleteObject(hBrush);
			}

			// 방어 패시브(SkillType::RESISTANCE = 2)가 있는 본인이라면 방패 렌더링
			if (id == my_id && (gm->skills_mask() & (1 << 2))) {
				if (_shield_img) {
					int shieldW = imgW * 3 / 4; // 방패 크기를 약간 작게
					int shieldH = imgH * 3 / 4;
					// 머리의 오른쪽 아래에 살짝 겹치게 배치
					int shieldX = px + imgW - shieldW / 2;
					int shieldY = py + imgH - shieldH / 2;
					DrawBmpTransparent(memDC, memDC2, _shield_img, shieldX, shieldY, shieldW, shieldH);
				}
			}

			// --- 이름 출력 로직 추가 (그림자 포함) ---
			std::wstring wName(player.name.begin(), player.name.end());
			Gdiplus::SolidBrush nameBrush(Gdiplus::Color(255, 255, 255, 255));
			Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(255, 0, 0, 0));
			graphics.DrawString(wName.c_str(), -1, &font, Gdiplus::PointF((float)(px + 1), (float)(py - 19)), &shadowBrush);
			graphics.DrawString(wName.c_str(), -1, &font, Gdiplus::PointF((float)px, (float)(py - 20)), &nameBrush);

			// --- 체력(HP) 출력 로직 ---
			std::wstring wHp = L"HP: " + std::to_wstring(player.hp) + L"/" + std::to_wstring(player.max_hp);
			Gdiplus::SolidBrush hpBrush(Gdiplus::Color(255, 100, 255, 100)); // ?고븳 珥덈줉??
			graphics.DrawString(wHp.c_str(), -1, &font, Gdiplus::PointF((float)(px + 1), (float)(py - 34)), &shadowBrush);
			graphics.DrawString(wHp.c_str(), -1, &font, Gdiplus::PointF((float)px, (float)(py - 35)), &hpBrush);
		}
	}

	// 4. 우측 상단 내 좌표 UI 출력
	if (players.contains(my_id)) {
		int my_x = players[my_id].x;
		int my_y = players[my_id].y;

		std::wstring coordText = L"Pos: (" + std::to_wstring(my_x) + L", " + std::to_wstring(my_y) + L")";

		SetBkMode(memDC, TRANSPARENT);
		SetTextColor(memDC, RGB(255, 255, 0)); // 눈에 확 띄게 노란색으로 설정

		// 화면 우측 상단 영역 지정 (우측에서 20px, 위에서 10px 여백)
		RECT uiRect = { 0, 10, width - 20, 50 };
		DrawText(memDC, coordText.c_str(), -1, &uiRect, DT_RIGHT | DT_TOP | DT_SINGLELINE);
	}

	// --- GDI+ UI Rendering ---
	{
		Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
		Gdiplus::SolidBrush yellowBrush(Gdiplus::Color(255, 255, 255, 0));
		Gdiplus::SolidBrush blackTransBrush(Gdiplus::Color(150, 0, 0, 0));

		// 4.5 Draw In-Game Chat Bubbles
		DrawChatBubbles(graphics, font, left_x, bottom_y, cellWidth, cellHeight, players);
		DrawChatBubbles(graphics, font, left_x, bottom_y, cellWidth, cellHeight, npcs);

		// 4.5.5 Draw AGGRO indicators on NPC heads
		for (auto& [id, npc] : npcs) {
			if (npc.npc_state != 2) continue; // 2 = NpcState::AGGRO
			float rel_x = npc.render_x - left_x;
			float rel_y = npc.render_y - bottom_y;
			if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
				float screen_y = (VIEW_HEIGHT - 1.0f - rel_y);
				int px = (int)(rel_x * cellWidth) + cellWidth / 2;
				int py = (int)(screen_y * cellHeight) - 50; // HP바 위쪽으로 올리기 위해 -28에서 -50으로 변경
				Gdiplus::Font aggroFont(&fontFamily, 14, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
				Gdiplus::SolidBrush aggroBrush(Gdiplus::Color(255, 255, 50, 50));
				Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(200, 0, 0, 0));
				// 그림자
				graphics.DrawString(L"!", -1, &aggroFont, Gdiplus::PointF((float)(px - 3), (float)(py + 1)), &shadowBrush);
				// 느낌표
				graphics.DrawString(L"!", -1, &aggroFont, Gdiplus::PointF((float)(px - 4), (float)(py)), &aggroBrush);
			}
		}

		// 4.5.6 Draw Quest Indicators on Quest NPC (visual_id == 10)
		for (auto& [id, npc] : npcs) {
			if (npc.visual_id != 10) continue;
			float rel_x = npc.render_x - left_x;
			float rel_y = npc.render_y - bottom_y;
			if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
				float screen_y = (VIEW_HEIGHT - 1.0f - rel_y);
				int px = (int)(rel_x * cellWidth) + cellWidth / 2 - 6;
				int py = (int)(screen_y * cellHeight) - 60; // Moved higher above name/HP
				Gdiplus::Font questFont(&fontFamily, 20, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
				
				bool is_complete = (gm->quest_progress() >= gm->max_quest_progress() && gm->quest_stage() < 6);
				Gdiplus::SolidBrush markBrush(is_complete ? Gdiplus::Color(255, 50, 255, 50) : Gdiplus::Color(255, 255, 200, 50));
				Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(200, 0, 0, 0));
				
				const wchar_t* markStr = is_complete ? L"?" : L"!";
				if (gm->quest_stage() >= 6) markStr = L""; // All quests completed
				
				if (markStr[0] != L'\0') {
					graphics.DrawString(markStr, -1, &questFont, Gdiplus::PointF((float)(px + 1), (float)(py + 1)), &shadowBrush);
					graphics.DrawString(markStr, -1, &questFont, Gdiplus::PointF((float)(px), (float)(py)), &markBrush);
				}
			}
		}

		// 4.6 Draw Helmets for Players
		for (auto& [id, player] : players) {
			int average_tier = (player.head_tier + player.chest_tier + player.legs_tier + player.boots_tier) / 4;
			if (average_tier > 0 && average_tier <= 4) {
				float rel_x = player.render_x - left_x;
				float rel_y = player.render_y - bottom_y;
				if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
					float screen_y = (VIEW_HEIGHT - 1.0f - rel_y);
					int px = (int)(rel_x * cellWidth) + (cellWidth / 2);
					int py = (int)(screen_y * cellHeight);
					
					HBITMAP helmet_img = _helmets[average_tier];
					if (helmet_img) {
						BITMAP bmp;
						GetObject(helmet_img, sizeof(BITMAP), &bmp);
						int img_w = static_cast<int>(bmp.bmWidth * 1.5f); // smaller size
						int img_h = static_cast<int>(bmp.bmHeight * 1.5f)	;
						// Draw helmet top-right of the head
						DrawBmpTransparent(memDC, memDC2, helmet_img, px + (cellWidth / 4) - 6, py - 10, img_w, img_h);
					}
				}
			}
		}

		// 4.7 Draw Swords for Players
		for (auto& [id, player] : players) {
			if (player.weapon_tier > 0 && player.weapon_tier <= 6) {
				float rel_x = player.render_x - left_x;
				float rel_y = player.render_y - bottom_y;
				if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
					float screen_y = (VIEW_HEIGHT - 1.0f - rel_y);
					int px = (int)(rel_x * cellWidth) + (cellWidth / 2);
					int py = (int)(screen_y * cellHeight) + (cellHeight / 2);
					
					Gdiplus::Image* sword_img = _swords[player.weapon_tier];
					if (sword_img) {
						int img_w = static_cast<int>(sword_img->GetWidth() * 1.5f);
						int img_h = static_cast<int>(sword_img->GetHeight() * 1.5f);
						
						graphics.TranslateTransform(static_cast<float>(px), static_cast<float>(py));
						float angle = 0.0f;
						if (player.dir_y == 1) angle = -90.0f;       // Up
						else if (player.dir_y == -1) angle = 90.0f;  // Down
						else if (player.dir_x == -1) angle = 180.0f; // Left
						else if (player.dir_x == 1) angle = 0.0f;    // Right

						graphics.RotateTransform(angle);
						graphics.DrawImage(sword_img, 15, -img_h/2, img_w, img_h);
						graphics.ResetTransform();
					}
				}
			}
		}

		// 4.8 Draw Attack Effects
		auto& effects = gm->attack_effects();
		now = std::chrono::steady_clock::now();
		for (auto it = effects.begin(); it != effects.end(); ) {
			int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - it->start_time).count());
			if (elapsed > 300) {
				it = effects.erase(it);
			} else {
				short dx = it->dx;
				short dy = it->dy;
				
				// 플레이어는 하늘색, NPC는 빨간색 공격 범위 표시
				Gdiplus::SolidBrush effectBrush(
					gm->players().contains(it->id) ? 
					Gdiplus::Color(100, 50, 200, 255) : 
					Gdiplus::Color(100, 255, 0, 0)
				);

				if (it->attack_type == 1) { // Half-circle AoE
					int radius = 1 + it->tier;
					float px = (it->x - left_x) * cellWidth;
					float py = (VIEW_HEIGHT - 1.0f - (it->y - bottom_y)) * cellHeight;
					
					// Center of the player cell
					float cx = px + (float)cellWidth / 2.0f;
					float cy = py + (float)cellHeight / 2.0f;
					float r_px = static_cast<float>(radius * cellWidth); // Approximation for pixel radius

					// Determine angle based on direction
					float startAngle = 0.0f;
					if (dx == 1) startAngle = -90.0f; // Right -> facing east, sweep from north to south
					else if (dx == -1) startAngle = 90.0f; // Left
					else if (dy == 1) startAngle = 180.0f; // Up
					else if (dy == -1) startAngle = 0.0f; // Down
					
					graphics.FillPie(&effectBrush, cx - r_px, cy - r_px, r_px * 2, r_px * 2, startAngle, 180.0f);

					// Sword animation
					if (it->tier > 0 && it->tier <= 6) {
						Gdiplus::Image* sword_img = _swords[it->tier];
						if (sword_img) {
							float progress = elapsed / 300.0f; // 0.0 to 1.0
							float currentAngle = startAngle + (progress * 180.0f); // Sweep 180 degrees
							
							graphics.TranslateTransform(cx, cy);
							graphics.RotateTransform(currentAngle);
							
							// Draw sword with handle at center (left-bottom roughly)
							// Assuming 32x32 sword where handle is bottom-left (0, 32)
							graphics.DrawImage(sword_img, 0, -32, 32, 32);
							
							graphics.ResetTransform();
						}
					}
				} else if (it->attack_type == 2) { // 5-tile linear range attack (Skeleton)
					std::vector<std::pair<int, int>> cells;
					if (dx != 0) {
						int sign = dx > 0 ? 1 : -1;
						for (int r = 1; r <= 5; ++r) cells.emplace_back(it->x + sign * r, it->y);
					} else if (dy != 0) {
						int sign = dy > 0 ? 1 : -1;
						for (int r = 1; r <= 5; ++r) cells.emplace_back(it->x, it->y + sign * r);
					} else {
						cells.emplace_back(it->x, it->y - 1);
					}
					
					for(auto& cell : cells) {
						int rel_x = cell.first - static_cast<int>(left_x);
						int rel_y = cell.second - static_cast<int>(bottom_y);
						if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
							float screen_y = (VIEW_HEIGHT - 1.0f - rel_y);
							int px = rel_x * cellWidth;
							int py = static_cast<int>(screen_y * cellHeight);
							graphics.FillRectangle(&effectBrush, px, py, cellWidth, cellHeight);
						}
					}
				} else if (it->attack_type == 4 || it->attack_type == 7) { // 1-tile projectile (Arrow or Fireball)
					int rel_x = it->x - static_cast<int>(left_x);
					int rel_y = it->y - static_cast<int>(bottom_y);
					if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
						float screen_y = (VIEW_HEIGHT - 1.0f - rel_y);
						int px = rel_x * cellWidth;
						int py = static_cast<int>(screen_y * cellHeight);
						
						// 1. Draw the damage cell (semi-transparent colored box)
						graphics.FillRectangle(&effectBrush, px, py, cellWidth, cellHeight);
						
						// 2. Draw the projectile image
						if (it->attack_type == 7 && _dragon_fireball_img) {
							Gdiplus::Rect destRect(px, py, cellWidth, cellHeight);
							graphics.DrawImage(_dragon_fireball_img, destRect);
						} else if (it->attack_type == 4 && _arrow_img) {
							float cx = px + cellWidth / 2.0f;
							float cy = py + cellHeight / 2.0f;
							float angle = 0.0f;
							if (dx == 1) angle = 90.0f; // East
							else if (dx == -1) angle = -90.0f; // West
							else if (dy == 1) angle = 0.0f; // North
							else if (dy == -1) angle = 180.0f; // South
							
							// Original image points Top-Right (45 degrees clockwise from North)
							// Subtract 45 degrees to offset it so it points to the correct angle
							angle -= 45.0f;
							
							graphics.TranslateTransform(cx, cy);
							graphics.RotateTransform(angle);
							graphics.DrawImage(_arrow_img, -cellWidth/2, -cellHeight/2, cellWidth, cellHeight);
							graphics.ResetTransform();
						} else {
							int arrow_size = std::min(cellWidth, cellHeight) / 2;
							int offset_x = (cellWidth - arrow_size) / 2;
							int offset_y = (cellHeight - arrow_size) / 2;
							Gdiplus::SolidBrush arrowBrush(Gdiplus::Color(255, 200, 200, 200)); 
							graphics.FillRectangle(&arrowBrush, px + offset_x, py + offset_y, arrow_size, arrow_size);
						}
					}
				} else if (it->attack_type == 3 || it->attack_type == 5) { // 5x5 explosion AoE (Creeper) or Dragon Tail
					std::vector<std::pair<int, int>> cells;
					if (it->attack_type == 5) {
						for (int r = -3; r <= 3; ++r) {
							for (int c = -3; c <= 3; ++c) {
								if (std::abs(r) + std::abs(c) <= 3) {
									cells.emplace_back(it->x + c, it->y + r);
								}
							}
						}
					} else {
						for (int r = -2; r <= 2; ++r) {
							for (int c = -2; c <= 2; ++c) {
								cells.emplace_back(it->x + c, it->y + r);
							}
						}
					}
					for(auto& cell : cells) {
						int rel_x = cell.first - static_cast<int>(left_x);
						int rel_y = cell.second - static_cast<int>(bottom_y);
						if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
							float screen_y = (VIEW_HEIGHT - 1.0f - rel_y);
							int px = rel_x * cellWidth;
							int py = static_cast<int>(screen_y * cellHeight);
							graphics.FillRectangle(&effectBrush, px, py, cellWidth, cellHeight);
						}
					}
				} else { // Normal rectangle attack
					int range = 1;
					int width = 0;
					switch(it->tier) {
					case 1: range = 1; width = 0; break;
					case 2: range = 1; width = 1; break;
					case 3: range = 2; width = 1; break;
					case 4: range = 3; width = 1; break;
					case 5: range = 3; width = 1; break;
					case 6: range = 3; width = 2; break;
					default: range = 1; width = 0; break;
					}
					
					std::vector<std::pair<int, int>> cells;
					if (dx != 0) {
						int sign = dx > 0 ? 1 : -1;
						for (int r = 1; r <= range; ++r) {
							for (int w = -width; w <= width; ++w) cells.emplace_back(it->x + sign * r, it->y + w);
						}
					} else if (dy != 0) {
						int sign = dy > 0 ? 1 : -1;
						for (int r = 1; r <= range; ++r) {
							for (int w = -width; w <= width; ++w) cells.emplace_back(it->x + w, it->y + sign * r);
						}
					} else {
						cells.emplace_back(it->x, it->y - 1);
					}

					for(auto& cell : cells) {
						int rel_x = cell.first - static_cast<int>(left_x);
						int rel_y = cell.second - static_cast<int>(bottom_y);
						if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
							float screen_y = (VIEW_HEIGHT - 1.0f - rel_y);
							int px = rel_x * cellWidth;
							int py = static_cast<int>(screen_y * cellHeight);
							graphics.FillRectangle(&effectBrush, px, py, cellWidth, cellHeight);
						}
					}
				}
				++it;
			}
		}

		// 5. Draw Chat Logs (Bottom Left)
		int chatStartX = 10;
		int chatStartY = height - 100; // Up from bottom
		
		auto& logs = gm->chat_logs();
		int logY = static_cast<int>(chatStartY - (logs.size() * 18));
		
		if (!logs.empty()) {
			graphics.FillRectangle(&blackTransBrush, chatStartX, logY, 350, static_cast<int>(logs.size()) * 18 + 5);
			for (auto& wLog : logs) {
				graphics.DrawString(wLog.c_str(), -1, &font, Gdiplus::PointF((float)(chatStartX + 5.f), static_cast<float>(logY)), &whiteBrush);
				logY += 18;
			}
		}

		// 6. Draw Current Chat Input
		if (gm->is_chatting()) {
			graphics.FillRectangle(&blackTransBrush, chatStartX, chatStartY + 10, 350, 24);
			std::wstring wPrompt = L"Chat: " + gm->current_chat_input() + L"_";
			graphics.DrawString(wPrompt.c_str(), -1, &font, Gdiplus::PointF(static_cast<float>(chatStartX + 5.f), static_cast<float>(chatStartY + 14.f)), &yellowBrush);
		}

		// 7. Draw HUD (Hotbar)
		int hotbarWidth = 300;
		int hotbarHeight = 40;
		int hotbarX = (width - hotbarWidth) / 2;
		int hotbarY = height - hotbarHeight - 10;
		graphics.FillRectangle(&blackTransBrush, hotbarX, hotbarY, hotbarWidth, hotbarHeight);
		Gdiplus::Pen whitePen(Gdiplus::Color(255, 255, 255, 255), 2.0f);
		Gdiplus::StringFormat formatCenter;
		formatCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
		formatCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);

		for (int i = 0; i < 5; ++i) {
			int slotX = hotbarX + i * 40 + 55;
			int slotY = hotbarY + 5;
			graphics.DrawRectangle(&whitePen, slotX, slotY, 30, 30);
			
			// Draw slot numbers
			std::wstring slotNum = std::to_wstring(i + 1);
			graphics.DrawString(slotNum.c_str(), -1, &font, Gdiplus::PointF(static_cast<float>(slotX + 10), static_cast<float>(hotbarY - 15)), &whiteBrush);

			// Slot 1: Health Potion (mapped to ItemType::HEALTH_POTION)
			if (i == 0) {
				int potion_count = gm->my_inventory()[static_cast<int>(ItemType::HEALTH_POTION)];
				if (_health_potion_img) {
					DrawBmpTransparent(memDC, memDC2, _health_potion_img, slotX + 3, slotY + 3, 24, 24);
					Gdiplus::Font smallFont(&fontFamily, 10, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
					graphics.DrawString(std::to_wstring(potion_count).c_str(), -1, &smallFont, Gdiplus::PointF(static_cast<float>(slotX + 16), static_cast<float>(slotY + 16)), &whiteBrush);
				}

				int potionElapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - gm->last_potion_use()).count();
				if (potionElapsed < 5000) {
					graphics.FillRectangle(&blackTransBrush, slotX, slotY, 30, 30);
					float remain = (5000 - potionElapsed) / 1000.0f;
					wchar_t buf[16];
					swprintf(buf, 16, L"%.1f", remain);
					graphics.DrawString(buf, -1, &font, Gdiplus::RectF((float)slotX, (float)slotY, 30.0f, 30.0f), &formatCenter, &whiteBrush);
				}
			}
			// Slot 2: Buff Skill (mapped to Blaze Powder Image)
			else if (i == 1) {
				if (_blaze_powder_img) {
					DrawBmpTransparent(memDC, memDC2, _blaze_powder_img, slotX + 3, slotY + 3, 24, 24);
				}

				int skillElapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - gm->last_skill_use()).count();
				if (skillElapsed < 15000) {
					graphics.FillRectangle(&blackTransBrush, slotX, slotY, 30, 30);
					float remain = (15000 - skillElapsed) / 1000.0f;
					wchar_t buf[16];
					swprintf(buf, 16, L"%.1f", remain);
					graphics.DrawString(buf, -1, &font, Gdiplus::RectF((float)slotX, (float)slotY, 30.0f, 30.0f), &formatCenter, &whiteBrush);
				}
			}
			// Slot 3: Ender Pearl
			else if (i == 2) {
				if ((gm->skills_mask() & (1 << 3)) != 0) { // Fix mask from 4 to 3
					if (_ender_pearl_img) {
						DrawBmpTransparent(memDC, memDC2, _ender_pearl_img, slotX + 3, slotY + 3, 24, 24);
					}

					int pearlElapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - gm->last_ender_pearl_use()).count();
					if (pearlElapsed < 10000) {
						graphics.FillRectangle(&blackTransBrush, slotX, slotY, 30, 30);
						float remain = (10000 - pearlElapsed) / 1000.0f;
						wchar_t buf[16];
						swprintf(buf, 16, L"%.1f", remain);
						graphics.DrawString(buf, -1, &font, Gdiplus::RectF((float)slotX, (float)slotY, 30.0f, 30.0f), &formatCenter, &whiteBrush);
					}
				}
			}
		}

		// 8. Draw Inventory (if toggled)
		if (gm->show_inventory()) {
			int invWidth = 400;
			int invHeight = 300;
			int invX = (width - invWidth) / 2;
			int invY = (height - invHeight) / 2;
			Gdiplus::SolidBrush darkGrayBrush(Gdiplus::Color(220, 40, 40, 40));
			graphics.FillRectangle(&darkGrayBrush, invX, invY, invWidth, invHeight);
			graphics.DrawRectangle(&whitePen, invX, invY, invWidth, invHeight);
			graphics.DrawString(L"[ INVENTORY ]", -1, &font, Gdiplus::PointF(static_cast<float>(invX + 150), static_cast<float>(invY + 10)), &whiteBrush);
			
			// --- Draw Gold (Top-Left) ---
			if (_gold_icon) {
				DrawBmpTransparent(memDC, memDC2, _gold_icon, invX + 20, invY + 10, 24, 24);
			}
			std::wstring goldStr = std::to_wstring(gm->my_gold());
			graphics.DrawString(goldStr.c_str(), -1, &font, Gdiplus::PointF(static_cast<float>(invX + 50), static_cast<float>(invY + 14)), &whiteBrush);

			// --- Draw Eq text ---
			graphics.DrawString(L"장비:", -1, &font, Gdiplus::PointF(static_cast<float>(invX + 20), static_cast<float>(invY + 45)), &whiteBrush);

			// Equipment Slot Rendering
			int eqSlotX_helm = invX + 60;
			int eqSlotX_chest = invX + 100;
			int eqSlotX_legs = invX + 140;
			int eqSlotX_boots = invX + 180;
			int eqSlotX_sword = invX + 220;
			int eqSlotY = invY + 40;
			graphics.DrawRectangle(&whitePen, eqSlotX_helm, eqSlotY, 30, 30);
			graphics.DrawRectangle(&whitePen, eqSlotX_chest, eqSlotY, 30, 30);
			graphics.DrawRectangle(&whitePen, eqSlotX_legs, eqSlotY, 30, 30);
			graphics.DrawRectangle(&whitePen, eqSlotX_boots, eqSlotY, 30, 30);
			graphics.DrawRectangle(&whitePen, eqSlotX_sword, eqSlotY, 30, 30);

			if (gm->players().contains(gm->my_id())) {
				auto& my_player = gm->players()[gm->my_id()];
				if (my_player.head_tier > 0 && my_player.head_tier <= 4) {
					HBITMAP helmet_img = _helmets[my_player.head_tier];
					if (helmet_img) DrawBmpTransparent(memDC, memDC2, helmet_img, eqSlotX_helm + 3, eqSlotY + 3, 24, 24);
				}
				if (my_player.chest_tier > 0 && my_player.chest_tier <= 4) {
					HBITMAP img = _chestplates[my_player.chest_tier];
					if (img) DrawBmpTransparent(memDC, memDC2, img, eqSlotX_chest + 3, eqSlotY + 3, 24, 24);
				}
				if (my_player.legs_tier > 0 && my_player.legs_tier <= 4) {
					HBITMAP img = _leggings[my_player.legs_tier];
					if (img) DrawBmpTransparent(memDC, memDC2, img, eqSlotX_legs + 3, eqSlotY + 3, 24, 24);
				}
				if (my_player.boots_tier > 0 && my_player.boots_tier <= 4) {
					HBITMAP img = _boots[my_player.boots_tier];
					if (img) DrawBmpTransparent(memDC, memDC2, img, eqSlotX_boots + 3, eqSlotY + 3, 24, 24);
				}
				if (my_player.weapon_tier > 0 && my_player.weapon_tier <= 6) {
					Gdiplus::Image* sword_img = _swords[my_player.weapon_tier];
					if (sword_img) graphics.DrawImage(sword_img, eqSlotX_sword + 3, eqSlotY + 3, 24, 24);
				}

			}

			// --- Draw List-Based Inventory Items ---
			int item_idx = 0;
			for (const auto& [item_id, count] : gm->my_inventory()) {
				if (count <= 0 || item_id <= 0 || item_id >= 20) continue;

				int r = item_idx / 9;
				int c = item_idx % 9;
				int slotX = invX + 20 + c * 40;
				int slotY = invY + 80 + r * 40;
				
				graphics.DrawRectangle(&whitePen, slotX, slotY, 30, 30);
				
				HBITMAP img = _item_images[item_id];
				if (img) {
					DrawBmpTransparent(memDC, memDC2, img, slotX + 3, slotY + 3, 24, 24);
				}

				// Draw count at bottom-right
				Gdiplus::Font smallFont(&fontFamily, 10, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
				std::wstring countStr = std::to_wstring(count);
				graphics.DrawString(countStr.c_str(), -1, &smallFont, Gdiplus::PointF(static_cast<float>(slotX + 16), static_cast<float>(slotY + 16)), &whiteBrush);
				
				item_idx++;
			}
			
			// Draw remaining empty slots just for aesthetics
			for (; item_idx < 36; ++item_idx) {
				int r = item_idx / 9;
				int c = item_idx % 9;
				int slotX = invX + 20 + c * 40;
				int slotY = invY + 80 + r * 40;
				graphics.DrawRectangle(&whitePen, slotX, slotY, 30, 30);
			}
		}

		// 9. Draw Skill Tree (if toggled)
		if (gm->show_skill_tree()) {
			int skillWidth = 400;
			int skillHeight = 350;
			int skillX = (width - skillWidth) / 2;
			int skillY = (height - skillHeight) / 2;
			Gdiplus::SolidBrush darkGreenBrush(Gdiplus::Color(220, 20, 40, 20));
			graphics.FillRectangle(&darkGreenBrush, skillX, skillY, skillWidth, skillHeight);
			graphics.DrawRectangle(&whitePen, skillX, skillY, skillWidth, skillHeight);

			graphics.DrawString(L"[ SKILL TREE ]", -1, &font, Gdiplus::PointF(static_cast<float>(skillX + 140), static_cast<float>(skillY + 10)), &whiteBrush);
			graphics.DrawString(L"닫기: ESC 또는 K", -1, &font, Gdiplus::PointF(static_cast<float>(skillX + 250), static_cast<float>(skillY + 10)), &whiteBrush);
			std::wstring spStr = L"잔여 스킬 포인트(SP): " + std::to_wstring(gm->unspent_sp());
			Gdiplus::SolidBrush yellowBrush(Gdiplus::Color(255, 255, 255, 0));
			graphics.DrawString(spStr.c_str(), -1, &font, Gdiplus::PointF(static_cast<float>(skillX + 20), static_cast<float>(skillY + 40)), &yellowBrush);
			auto has_skill = [&](int skill_enum) {
				return (gm->skills_mask() & (1 << skill_enum)) != 0;
				};
			int curY = skillY + 80;
			auto draw_skill_item = [&](int num, const std::wstring& name, const std::wstring& desc, int skill_enum) {
				std::wstring prefix = L"[" + std::to_wstring(num) + L"] " + name;
				graphics.DrawString(prefix.c_str(), -1, &font, Gdiplus::PointF(static_cast<float>(skillX + 20), static_cast<float>(curY)), &whiteBrush);

				Gdiplus::Font smallFont(&fontFamily, 12, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
				graphics.DrawString(desc.c_str(), -1, &smallFont, Gdiplus::PointF(static_cast<float>(skillX + 30), static_cast<float>(curY + 20)), &whiteBrush);
				std::wstring status;
				Gdiplus::SolidBrush* statusBrush = &whiteBrush;
				Gdiplus::SolidBrush greenBrush(Gdiplus::Color(255, 100, 255, 100));
				Gdiplus::SolidBrush redBrush(Gdiplus::Color(255, 255, 100, 100));
				if (has_skill(skill_enum)) {
					status = L"배움";
					statusBrush = &yellowBrush;
				}
				else {
					if (gm->unspent_sp() > 0) {
						status = L"숫자키 " + std::to_wstring(num) + L" 눌러 획득";
						statusBrush = &greenBrush;
					}
					else {
						status = L"포인트 부족";
						statusBrush = &redBrush;
					}
				}
				graphics.DrawString(status.c_str(), -1, &font, Gdiplus::PointF(static_cast<float>(skillX + 220), static_cast<float>(curY)), statusBrush);
				curY += 50;
				};
			// SkillType::FIRE_ASPECT = 0
			draw_skill_item(1, L"발화 (1티어)", L"공격 시 적을 불태워 3초간 도트 데미지를 줍니다.", 0);
			// SkillType::LIFE_STEAL = 1
			draw_skill_item(2, L"흡혈 (1티어)", L"공격 시 가한 데미지의 일부(15%)를 회복합니다.", 1);
			// SkillType::RESISTANCE = 2
			draw_skill_item(3, L"저항 (2티어)", L"받는 데미지가 영구적으로 30% 감소합니다.", 2);
			// SkillType::ENDER_PEARL = 3
			draw_skill_item(4, L"엔더 진주 (2티어)", L"3칸 앞 몬스터나 마우스 방향으로 순간이동합니다.", 3);
		}

		// Trade UI
		if (gm->is_trading()) {
			int tradeX = width / 2 - 200;
			int tradeY = height / 2 - 150;
			int tradeW = 400;
			int tradeH = 300;

			Gdiplus::SolidBrush darkBlueBrush(Gdiplus::Color(220, 20, 20, 60));
			graphics.FillRectangle(&darkBlueBrush, tradeX, tradeY, tradeW, tradeH);
			graphics.DrawRectangle(&whitePen, tradeX, tradeY, tradeW, tradeH);
			
			std::wstring title = L"[ 거래 ]";
			int v_id = gm->trade_visual_id();
			if (v_id == 6) title = L"[ 성직자 ]";
			else if (v_id == 7) title = L"[ 대장장이 ]";
			else if (v_id == 8) title = L"[ 무기장인 ]";
			else if (v_id == 9) title = L"[ 사서 (인챈터) ]";

			graphics.DrawString(title.c_str(), -1, &font, Gdiplus::PointF(static_cast<float>(tradeX + 150), static_cast<float>(tradeY + 10)), &whiteBrush);

			if (v_id >= 6 && v_id <= 9 && _mob_heads[v_id]) {
				DrawBmpTransparent(memDC, memDC2, _mob_heads[v_id], tradeX + 20, tradeY + 20, 48, 48);
			}

			std::wstring options = L"";
			if (v_id == 6) {
				options = L"[1] 썩은고기 10개->50G\n[2] 뼈다귀 5개->100G\n[3] 화약 3개->150G\n[4] 철괴 1개->200G\n[5] 다이아몬드 1개->500G\n[6] 체력 포션 구매(30G)";
			} else if (v_id == 7) {
				options = L"[1] 투구 업그레이드\n[2] 흉갑 업그레이드\n[3] 바지 업그레이드\n[4] 부츠 업그레이드\n(비용: 다음 티어 * 150G)";
			} else if (v_id == 8) {
				options = L"[1] 무기 다음 티어 강화\n(조건: 다음 티어 * 5 레벨\n 비용: 다음 티어 * 200G)";
			} else if (v_id == 9) {
				options = L"[1] 무기 마법 부여 (인챈트)\n(비용: 500G, 확률: 30%)";
			}

			graphics.DrawString(options.c_str(), -1, &font, Gdiplus::PointF(static_cast<float>(tradeX + 30), static_cast<float>(tradeY + 80)), &whiteBrush);
			graphics.DrawString(L"숫자 키(1~6)를 눌러 선택하세요.\nESC를 눌러 거래를 종료합니다.", -1, &font, Gdiplus::PointF(static_cast<float>(tradeX + 30), static_cast<float>(tradeY + 240)), &whiteBrush);
		}

		// Buff Gauge
		if (now < gm->buff_end_time()) {
			int buffRemain = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(gm->buff_end_time() - now).count());
			float buffSec = buffRemain / 1000.0f;
			wchar_t buffText[64];
			swprintf(buffText, 64, L"스킬 유지: %.1f초", buffSec);
			Gdiplus::SolidBrush orangeBrush(Gdiplus::Color(255, 200, 100, 0));
			graphics.DrawString(buffText, -1, &font, Gdiplus::PointF(static_cast<float>(width / 2 - 50), 50.0f), &orangeBrush);
		}
	}

	if (gm->players().contains(gm->my_id())) {
		auto& my_player = gm->players()[gm->my_id()];

		// Draw Quest Tracker UI
		{
			Gdiplus::Font questFont(&fontFamily, 14, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
			Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(200, 0, 0, 0));
			Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 200, 50));
			Gdiplus::SolidBrush completeBrush(Gdiplus::Color(255, 50, 255, 50));
			
			std::wstring q_text = L"[메인 퀘스트] ";
			int stage = gm->quest_stage();
			int prog = gm->quest_progress();
			int max_prog = gm->max_quest_progress();
			
			if (stage == 0) q_text += L"철골렘 사냥";
			else if (stage == 1) q_text += L"좀비 사냥";
			else if (stage == 2) q_text += L"스켈레톤 사냥";
			else if (stage == 3) q_text += L"크리퍼 사냥";
			else if (stage == 4) q_text += L"엔더맨 사냥";
			else if (stage == 5) q_text += L"엔더드래곤 토벌";
			else q_text += L"모든 임무 완료!";
			
			if (stage < 6) {
				q_text += L" (" + std::to_wstring(prog) + L" / " + std::to_wstring(max_prog) + L")";
			}
			
			int q_x = 20;
			int q_y = 20;
			
			graphics.DrawString(q_text.c_str(), -1, &questFont, Gdiplus::PointF((float)q_x+1, (float)q_y+1), &shadowBrush);
			graphics.DrawString(q_text.c_str(), -1, &questFont, Gdiplus::PointF((float)q_x, (float)q_y), prog >= max_prog && stage < 6 ? &completeBrush : &textBrush);
		}

		// Draw EXP Bar at the top
		{
			int barWidth = 400;
			int barHeight = 12;
			int barX = (width - barWidth) / 2;
			int barY = 10; // Top of the screen

			unsigned long long required_exp = static_cast<unsigned long long>(my_player.level) * my_player.level * 100;
			float expRatio = required_exp > 0 ? (float)my_player.exp / required_exp : 0.0f;
			if (expRatio > 1.0f) expRatio = 1.0f;
			int fillWidth = static_cast<int>(barWidth * expRatio);

			// Background
			Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 50, 50, 50));
			graphics.FillRectangle(&bgBrush, barX, barY, barWidth, barHeight);

			// Fill (Minecraft XP style green)
			Gdiplus::SolidBrush fillBrush(Gdiplus::Color(255, 128, 255, 32));
			graphics.FillRectangle(&fillBrush, barX, barY, fillWidth, barHeight);

			// Border
			Gdiplus::Pen borderPen(Gdiplus::Color(255, 0, 0, 0), 2);
			graphics.DrawRectangle(&borderPen, barX, barY, barWidth, barHeight);

			// Texts
			Gdiplus::Font smallFont(&fontFamily, 12, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
			Gdiplus::Font boldFont(&fontFamily, 16, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
			Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
			Gdiplus::SolidBrush cyanBrush(Gdiplus::Color(255, 0, 255, 255));

			Gdiplus::StringFormat formatCenter;
			formatCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
			formatCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);

			// EXP Text inside the bar
			std::wstring expStr = L"Exp: " + std::to_wstring(my_player.exp) + L" / " + std::to_wstring(required_exp);
			graphics.DrawString(expStr.c_str(), -1, &smallFont, Gdiplus::RectF(static_cast<float>(barX), static_cast<float>(barY), static_cast<float>(barWidth), static_cast<float>(barHeight)), &formatCenter, &whiteBrush);

			// Level Text below the bar
			std::wstring lvStr = L"Lv. " + std::to_wstring(my_player.level);
			graphics.DrawString(lvStr.c_str(), -1, &boldFont, Gdiplus::RectF(static_cast<float>(barX), static_cast<float>(barY + 16), static_cast<float>(barWidth), 20.0f), &formatCenter, &cyanBrush);
		}

		// Boss HP Bar
		for (auto& [id, npc] : npcs) {
			if (npc.visual_id == 11 && npc.hp > 0) {
				int bossBarW = 600;
				int bossBarH = 20;
				int bossBarX = (width - bossBarW) / 2;
				int bossBarY = 50;
				
				Gdiplus::SolidBrush bossBg(Gdiplus::Color(200, 50, 50, 50));
				Gdiplus::SolidBrush bossFg(Gdiplus::Color(255, 150, 0, 150));
				
				graphics.FillRectangle(&bossBg, bossBarX, bossBarY, bossBarW, bossBarH);
				int fillW = (int)((float)npc.hp / npc.max_hp * bossBarW);
				graphics.FillRectangle(&bossFg, bossBarX, bossBarY, fillW, bossBarH);
				
				Gdiplus::Pen whitePen(Gdiplus::Color(255, 255, 255, 255), 2);
				graphics.DrawRectangle(&whitePen, bossBarX, bossBarY, bossBarW, bossBarH);
				
				Gdiplus::Font bossFont(&fontFamily, 24, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
				Gdiplus::SolidBrush purpleBrush(Gdiplus::Color(255, 200, 50, 255));
				Gdiplus::StringFormat formatCenter;
				formatCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
				formatCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
				
				graphics.DrawString(L"Ender Dragon", -1, &bossFont, Gdiplus::RectF(static_cast<float>(bossBarX), static_cast<float>(bossBarY - 30), static_cast<float>(bossBarW), 30.0f), &formatCenter, &purpleBrush);
				break;
			}
		}
		static bool is_dead = false;
		static std::chrono::time_point<std::chrono::steady_clock> death_time;
		
		if (my_player.hp <= 0) {
			if (!is_dead) {
				is_dead = true;
				death_time = now;
			}
			auto current_now = std::chrono::steady_clock::now();
			int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(current_now - death_time).count());
			int countdown = 5 - elapsed;
			if (countdown < 0) countdown = 0;
			
			// Dim screen
			Gdiplus::SolidBrush darkOverlay(Gdiplus::Color(180, 0, 0, 0));
			graphics.FillRectangle(&darkOverlay, 0, 0, width, height);
			
			// Draw Countdown
			if (countdown > 0) {
				Gdiplus::Font hugeFont(&fontFamily, 72, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
				Gdiplus::SolidBrush redBrush(Gdiplus::Color(255, 255, 50, 50));
				std::wstring text = std::to_wstring(countdown);
				
				Gdiplus::StringFormat formatCenter;
				formatCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
				formatCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
				
				graphics.DrawString(text.c_str(), -1, &hugeFont, Gdiplus::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)), &formatCenter, &redBrush);
			}
		} else {
			is_dead = false;
		}
	}

	// 화면 복사
	BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
	SelectObject(memDC, oldBitmap);
	DeleteObject(memBitmap);
	DeleteDC(memDC);
	DeleteDC(memDC2);
	ReleaseDC(hWnd, hdc);
}
