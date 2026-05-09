#include "pch.h"
#include "RenderManager.h"
#include "GameManager.h"

RenderManager::~RenderManager()
{
	if (_hBoardBmp) {
		DeleteObject(_hBoardBmp);
		_hBoardBmp = NULL;
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

	HDC boardDC = CreateCompatibleDC(hdc);
	HBITMAP oldBoardBmp = NULL;
	if (_hBoardBmp) oldBoardBmp = (HBITMAP)SelectObject(boardDC, _hBoardBmp);

	// Background
	FillRect(memDC, &clientRect, (HBRUSH)GetStockObject(WHITE_BRUSH));

	int cellWidth = width / VIEW_WIDTH;
	int cellHeight = height / VIEW_HEIGHT;

	// Viewport calculation (Centered on player, clamped to world bounds)
	int left_x = 0, top_y = 0;
	int my_id = gm->my_id();
	auto& players = gm->players();
	auto& npcs = gm->npcs();

	if (players.contains(my_id)) {
		left_x = players[my_id].x - VIEW_WIDTH / 2;
		top_y = players[my_id].y - VIEW_HEIGHT / 2;

		// Clamp to map boundaries
		left_x = std::clamp(left_x, 0, WORLD_WIDTH - VIEW_WIDTH);
		top_y = std::clamp(top_y, 0, WORLD_WIDTH - VIEW_WIDTH);
	}

	// Checkerboard
	for (int y = 0; y < VIEW_HEIGHT; ++y) {
		for (int x = 0; x < VIEW_WIDTH; ++x) {
			int world_x = left_x + x;
			int world_y = top_y + y;

			if (world_x < 0 || world_x >= WORLD_WIDTH || world_y < 0 || world_y >= WORLD_HEIGHT)
				continue;

			if (_hBoardBmp) {
				int srcX = (world_x / 3 + world_y / 3) % 2 == 0 ? 5 : 69;
				int srcY = 5;
				StretchBlt(memDC, x * cellWidth, y * cellHeight, cellWidth, cellHeight, boardDC, srcX, srcY, 65, 65, SRCCOPY);
			}
			else {
				RECT rect = { x * cellWidth, y * cellHeight, (x + 1) * cellWidth, (y + 1) * cellHeight };
				HBRUSH hBrush = CreateSolidBrush((world_x + world_y) % 2 == 0 ? RGB(255, 255, 255) : RGB(230, 230, 230));
				FillRect(memDC, &rect, hBrush);
				DeleteObject(hBrush);
			}
		}
	}

	// NPCs
	for (auto& [id, npc] : npcs) {
		int rel_x = npc.x - left_x;
		int rel_y = npc.y - top_y;

		if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
			HBRUSH hBrush = CreateSolidBrush(RGB(0, 255, 0)); // Green for NPCs
			HBRUSH oldB = (HBRUSH)SelectObject(memDC, hBrush);

			int padX = cellWidth * 15 / 100;
			int padY = cellHeight * 15 / 100;

			int px = rel_x * cellWidth + padX;
			int py = rel_y * cellHeight + padY;
			int pr = (rel_x + 1) * cellWidth - padX;
			int pb = (rel_y + 1) * cellHeight - padY;

			Ellipse(memDC, px, py, pr, pb);

			// Draw Name
			SetBkMode(memDC, TRANSPARENT);
			SetTextColor(memDC, RGB(0, 255, 255)); // Cyan for NPC names
			SetTextAlign(memDC, TA_CENTER);

			std::wstring wname(npc.name.begin(), npc.name.end());
			int centerX = px + (pr - px) / 2;
			TextOut(memDC, centerX, py - 20, wname.c_str(), (int)wname.length());

			SetTextAlign(memDC, TA_LEFT);
			SelectObject(memDC, oldB);
			DeleteObject(hBrush);
		}
	}

	// Players
	for (auto& [id, player] : players) {
		int rel_x = player.x - left_x;
		int rel_y = player.y - top_y;

		if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
			HBRUSH hBrush = CreateSolidBrush(id == my_id ? RGB(255, 0, 0) : RGB(0, 0, 255));
			HBRUSH oldB = (HBRUSH)SelectObject(memDC, hBrush);

			int padX = cellWidth * 15 / 100;
			int padY = cellHeight * 15 / 100;

			int px = rel_x * cellWidth + padX;
			int py = rel_y * cellHeight + padY;
			int pr = (rel_x + 1) * cellWidth - padX;
			int pb = (rel_y + 1) * cellHeight - padY;

			Ellipse(memDC, px, py, pr, pb);

			// Draw Name
			SetBkMode(memDC, TRANSPARENT);
			SetTextColor(memDC, RGB(255, 255, 0)); // Yellow
			SetTextAlign(memDC, TA_CENTER);        // Center alignment

			std::wstring wname(player.name.begin(), player.name.end());
			int centerX = px + (pr - px) / 2;
			TextOut(memDC, centerX, py - 20, wname.c_str(), (int)wname.length());

			SetTextAlign(memDC, TA_LEFT); // Reset alignment for other text
			SelectObject(memDC, oldB);
			DeleteObject(hBrush);
		}
	}

	// Status Text
	if (players.contains(my_id)) {
		wchar_t status[128];
		swprintf_s(status, L"Pos: (%d, %d)", players[my_id].x, players[my_id].y);
		SetTextColor(memDC, RGB(0, 0, 0));
		TextOut(memDC, 10, 10, status, (int)wcslen(status));
	}

	BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

	if (oldBoardBmp) SelectObject(boardDC, oldBoardBmp);
	DeleteDC(boardDC);

	SelectObject(memDC, oldBitmap);
	DeleteObject(memBitmap);
	DeleteDC(memDC);
	ReleaseDC(hWnd, hdc);
}
