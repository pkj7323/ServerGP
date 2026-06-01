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
	int left_x = 0, bottom_y = 0;
	int my_id = gm->my_id();
	auto& players = gm->players();
	auto& npcs = gm->npcs();

	if (players.contains(my_id)) {
		left_x = players[my_id].x - VIEW_WIDTH / 2;
		bottom_y = players[my_id].y - VIEW_HEIGHT / 2;
		left_x = std::clamp(left_x, 0, WORLD_WIDTH - VIEW_WIDTH);
		bottom_y = std::clamp(bottom_y, 0, WORLD_HEIGHT - VIEW_HEIGHT);
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
	// 2. NPC 렌더링
	for (auto& [id, npc] : npcs) {
		int rel_x = npc.x - left_x;
		int rel_y = npc.y - bottom_y;

		if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
			int screen_y = (VIEW_HEIGHT - 1 - rel_y); // NPC도 Y 반전

			HBRUSH hBrush = CreateSolidBrush(RGB(0, 255, 0));
			HBRUSH oldB = (HBRUSH)SelectObject(memDC, hBrush);

			int padX = cellWidth * 15 / 100;
			int padY = cellHeight * 15 / 100;
			int px = rel_x * cellWidth + padX;
			int py = screen_y * cellHeight + padY;
			int pr = (rel_x + 1) * cellWidth - padX;
			int pb = (screen_y + 1) * cellHeight - padY;

			Ellipse(memDC, px, py, pr, pb);

			// --- 이름 출력 로직 추가 ---
			// 유니코드 환경 호환성을 위해 std::string -> std::wstring 변환
			std::wstring wName(npc.name.begin(), npc.name.end());
			TextOut(memDC, px, py - 20, wName.c_str(), (int)wName.length());

			SelectObject(memDC, oldB);
			DeleteObject(hBrush);
		}
	}

	// 3. Player 렌더링 (동일하게 Y 반전)
	for (auto& [id, player] : players) {
		int rel_x = player.x - left_x;
		int rel_y = player.y - bottom_y;

		if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
			int screen_y = (VIEW_HEIGHT - 1 - rel_y); // 플레이어도 Y 반전

			HBRUSH hBrush = CreateSolidBrush(id == my_id ? RGB(255, 0, 0) : RGB(0, 0, 255));
			HBRUSH oldB = (HBRUSH)SelectObject(memDC, hBrush);

			int padX = cellWidth * 15 / 100;
			int padY = cellHeight * 15 / 100;
			int px = rel_x * cellWidth + padX;
			int py = screen_y * cellHeight + padY;
			int pr = (rel_x + 1) * cellWidth - padX;
			int pb = (screen_y + 1) * cellHeight - padY;

			Ellipse(memDC, px, py, pr, pb);
			// --- 이름 출력 로직 추가 ---
			std::wstring wName(player.name.begin(), player.name.end());
			TextOut(memDC, px, py - 20, wName.c_str(), (int)wName.length());

			SelectObject(memDC, oldB);
			DeleteObject(hBrush);
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
		Gdiplus::Graphics graphics(memDC);
		Gdiplus::FontFamily fontFamily(L"Malgun Gothic");
		Gdiplus::Font font(&fontFamily, 14, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
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

				int rel_x = obj.x - left_x;
				int rel_y = obj.y - bottom_y;
				if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
					int screen_y = (VIEW_HEIGHT - 1 - rel_y);
					int px = rel_x * cellWidth + (cellWidth / 2);
					int py = screen_y * cellHeight - 30; // Above head

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
				int rel_x = player.x - left_x;
				int rel_y = player.y - bottom_y;
				if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
					int screen_y = (VIEW_HEIGHT - 1 - rel_y);
					int px = rel_x * cellWidth + (cellWidth / 2);
					int py = screen_y * cellHeight;
					
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
				int rel_x = player.x - left_x;
				int rel_y = player.y - bottom_y;
				if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
					int screen_y = (VIEW_HEIGHT - 1 - rel_y);
					int px = rel_x * cellWidth + (cellWidth / 2);
					int py = screen_y * cellHeight + (cellHeight / 2);
					
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
						int screen_y = (VIEW_HEIGHT - 1 - rel_y);
						int px = rel_x * cellWidth;
						int py = screen_y * cellHeight;
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
			
			// Draw some dummy slots
			for(int r=0; r<4; ++r) {
				for(int c=0; c<9; ++c) {
					graphics.DrawRectangle(&whitePen, invX + 20 + c*40, invY + 50 + r*40, 30, 30);
				}
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
