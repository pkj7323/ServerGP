#include "pch.h"
#include "RenderManager.h"
#include "GameManager.h"

RenderManager::~RenderManager()
{
	if (_hBoardBmp) {
		DeleteObject(_hBoardBmp);
		_hBoardBmp = NULL;
	}
	for (int i = 1; i <= 4; ++i) {
		if (_helmets[i]) {
			delete _helmets[i];
			_helmets[i] = nullptr;
		}
	}
	for (int i = 1; i <= 6; ++i) {
		if (_swords[i]) {
			delete _swords[i];
			_swords[i] = nullptr;
		}
	}
	if (_player_head) { delete _player_head; _player_head = nullptr; }
	for (int i = 1; i <= 5; ++i) {
		if (_mob_heads[i]) { delete _mob_heads[i]; _mob_heads[i] = nullptr; }
	}
	if (_gold_icon) { delete _gold_icon; _gold_icon = nullptr; }
	for (int i = 0; i < 20; ++i) {
		if (_item_images[i]) { delete _item_images[i]; _item_images[i] = nullptr; }
	}
}

void RenderManager::Release()
{
	if (_hBoardBmp) {
		DeleteObject(_hBoardBmp);
		_hBoardBmp = NULL;
	}
	for (int i = 1; i <= 4; ++i) {
		if (_helmets[i]) {
			delete _helmets[i];
			_helmets[i] = nullptr;
		}
	}
	for (int i = 1; i <= 6; ++i) {
		if (_swords[i]) {
			delete _swords[i];
			_swords[i] = nullptr;
		}
	}
	if (_player_head) { delete _player_head; _player_head = nullptr; }
	for (int i = 1; i <= 5; ++i) {
		if (_mob_heads[i]) { delete _mob_heads[i]; _mob_heads[i] = nullptr; }
	}
	if (_gold_icon) { delete _gold_icon; _gold_icon = nullptr; }
	for (int i = 0; i < 20; ++i) {
		if (_item_images[i]) { delete _item_images[i]; _item_images[i] = nullptr; }
	}
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

	// 배경 (검은색)
	FillRect(memDC, &clientRect, (HBRUSH)GetStockObject(BLACK_BRUSH));

	// 매크로가 없으면 15x15 기준으로 설정
	int cellWidth = width / VIEW_WIDTH;
	int cellHeight = height / VIEW_HEIGHT;

	// Viewport 계산 (Y-up 기준: bottom_y가 화면 맨 밑 줄)
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

	// 1. 지형 렌더링
	for (int y = 0; y < VIEW_HEIGHT; ++y) {
		for (int x = 0; x < VIEW_WIDTH; ++x) {
			int world_x = left_x + x;
			int world_y = bottom_y + y; // y=0이 화면 밑바닥

			// GDI 렌더링을 위해 모니터 좌표계로 변환 (상하 반전)
			int screen_y = (VIEW_HEIGHT - 1 - y);
			RECT rect = { x * cellWidth, screen_y * cellHeight, (x + 1) * cellWidth, (screen_y + 1) * cellHeight };

			uint8_t tile_id = gm->get_visual_tile(world_x, world_y);
			COLORREF color;
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

			HBRUSH hBrush = CreateSolidBrush(color);
			FillRect(memDC, &rect, hBrush);
			DeleteObject(hBrush);
		}
	}
	SetBkMode(memDC, TRANSPARENT);
	SetTextColor(memDC, RGB(255, 255, 255));
	
	// --- GDI+ UI Rendering (Graphics instance pulled up for head rendering) ---
	Gdiplus::Graphics graphics(memDC);
	
	// 마인크래프트 특유의 도트(픽셀) 감성을 살리기 위해 NearestNeighbor(근접 보간) 필터링 적용!
	graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
	graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf); // 픽셀 어긋남 방지

	Gdiplus::FontFamily fontFamily(L"Malgun Gothic");
	Gdiplus::Font font(&fontFamily, 14, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	
	// 2. NPC 렌더링
	for (auto& [id, npc] : npcs) {
		float rel_x = npc.render_x - left_x;
		float rel_y = npc.render_y - bottom_y;

		if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
			float screen_y = (VIEW_HEIGHT - 1.0f - rel_y); // NPC도 Y 반전

			int padX = cellWidth * 15 / 100;
			int padY = cellHeight * 15 / 100;
			int px = (int)(rel_x * cellWidth) + padX;
			int py = (int)(screen_y * cellHeight) + padY;
			int imgW = cellWidth - padX * 2;
			int imgH = cellHeight - padY * 2;

			if (npc.visual_id > 0 && npc.visual_id <= 5 && _mob_heads[npc.visual_id]) {
				graphics.DrawImage(_mob_heads[npc.visual_id], px, py, imgW, imgH);
			} else {
				HBRUSH hBrush = CreateSolidBrush(RGB(0, 255, 0));
				HBRUSH oldB = (HBRUSH)SelectObject(memDC, hBrush);
				Ellipse(memDC, px, py, px + imgW, py + imgH);
				SelectObject(memDC, oldB);
				DeleteObject(hBrush);
			}

			// --- 이름 출력 로직 추가 ---
			std::wstring wName(npc.name.begin(), npc.name.end());
			TextOut(memDC, px, py - 20, wName.c_str(), (int)wName.length());
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
				graphics.DrawImage(_player_head, px, py, imgW, imgH);
			} else {
				HBRUSH hBrush = CreateSolidBrush(id == my_id ? RGB(255, 0, 0) : RGB(0, 0, 255));
				HBRUSH oldB = (HBRUSH)SelectObject(memDC, hBrush);
				Ellipse(memDC, px, py, px + imgW, py + imgH);
				SelectObject(memDC, oldB);
				DeleteObject(hBrush);
			}

			// --- 이름 출력 로직 추가 ---
			std::wstring wName(player.name.begin(), player.name.end());
			TextOut(memDC, px, py - 20, wName.c_str(), (int)wName.length());
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
		auto draw_bubble = [&](auto& obj_map) {
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
					Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 0, 0, 0));
					
					graphics.FillRectangle(&bubbleBrush, px - bounds.Width / 2 - 5, py - bounds.Height - 5, bounds.Width + 10, bounds.Height + 10);
					graphics.DrawString(obj.chat_msg.c_str(), -1, &font, Gdiplus::PointF(px - bounds.Width / 2, py - bounds.Height), &textBrush);
				}
			}
		};
		draw_bubble(players);
		draw_bubble(npcs);

		// 4.6 Draw Helmets for Players
		for (auto& [id, player] : players) {
			if (player.armor_tier > 0 && player.armor_tier <= 4) {
				float rel_x = player.render_x - left_x;
				float rel_y = player.render_y - bottom_y;
				if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
					float screen_y = (VIEW_HEIGHT - 1.0f - rel_y);
					int px = (int)(rel_x * cellWidth) + (cellWidth / 2);
					int py = (int)(screen_y * cellHeight);
					
					Gdiplus::Image* helmet_img = _helmets[player.armor_tier];
					if (helmet_img) {
						int img_w = helmet_img->GetWidth() * 2; // scale if needed
						int img_h = helmet_img->GetHeight() * 2;
						// Draw helmet above the head
						graphics.DrawImage(helmet_img, px - img_w / 2, py - 40, img_w, img_h);
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
		auto now = std::chrono::steady_clock::now();
		for (auto it = effects.begin(); it != effects.end(); ) {
			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - it->start_time).count() > 300) {
				it = effects.erase(it);
			} else {
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

				Gdiplus::SolidBrush effectBrush(Gdiplus::Color(100, 255, 0, 0));

				short dx = it->dx;
				short dy = it->dy;
				
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
					int rel_x = cell.first - left_x;
					int rel_y = cell.second - bottom_y;
					if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
						float screen_y = (VIEW_HEIGHT - 1.0f - rel_y);
						int px = rel_x * cellWidth;
						int py = (int)(screen_y * cellHeight);
						graphics.FillRectangle(&effectBrush, px, py, cellWidth, cellHeight);
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

		// 7. Draw HUD (Hotbar Placeholder)
		int hotbarWidth = 300;
		int hotbarHeight = 40;
		int hotbarX = (width - hotbarWidth) / 2;
		int hotbarY = height - hotbarHeight - 10;
		graphics.FillRectangle(&blackTransBrush, hotbarX, hotbarY, hotbarWidth, hotbarHeight);
		Gdiplus::Pen whitePen(Gdiplus::Color(255, 255, 255, 255), 2.0f);
		for (int i = 0; i < 5; ++i) {
			graphics.DrawRectangle(&whitePen, hotbarX + i * 40 + 55, hotbarY + 5, 30, 30);
		}
		graphics.DrawString(L"1", -1, &font, Gdiplus::PointF(static_cast<float>(hotbarX + 55 + 10), static_cast<float>(hotbarY - 15)), &whiteBrush);
		graphics.DrawString(L"2", -1, &font, Gdiplus::PointF(static_cast<float>(hotbarX + 95 + 10), static_cast<float>(hotbarY - 15)), &whiteBrush);

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
				graphics.DrawImage(_gold_icon, invX + 20, invY + 10, 24, 24);
			}
			std::wstring goldStr = std::to_wstring(gm->my_gold());
			graphics.DrawString(goldStr.c_str(), -1, &font, Gdiplus::PointF(static_cast<float>(invX + 50), static_cast<float>(invY + 14)), &whiteBrush);

			// --- Draw Eq text ---
			graphics.DrawString(L"Eq:", -1, &font, Gdiplus::PointF(static_cast<float>(invX + 20), static_cast<float>(invY + 45)), &whiteBrush);

			// Equipment Slot Rendering
			int eqSlotX_helm = invX + 50;
			int eqSlotY = invY + 40;
			int eqSlotX_sword = invX + 90;
			graphics.DrawRectangle(&whitePen, eqSlotX_helm, eqSlotY, 30, 30);
			graphics.DrawRectangle(&whitePen, eqSlotX_sword, eqSlotY, 30, 30);

			if (gm->players().contains(gm->my_id())) {
				auto& my_player = gm->players()[gm->my_id()];
				if (my_player.armor_tier > 0 && my_player.armor_tier <= 4) {
					Gdiplus::Image* helmet_img = _helmets[my_player.armor_tier];
					if (helmet_img) graphics.DrawImage(helmet_img, eqSlotX_helm + 3, eqSlotY + 3, 24, 24);
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
				
				Gdiplus::Image* img = _item_images[item_id];
				if (img) {
					graphics.DrawImage(img, slotX + 3, slotY + 3, 24, 24);
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
	}

	// 화면 복사
	BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
	SelectObject(memDC, oldBitmap);
	DeleteObject(memBitmap);
	DeleteDC(memDC);
	ReleaseDC(hWnd, hdc);
}
