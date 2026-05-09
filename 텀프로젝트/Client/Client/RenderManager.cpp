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
			TextOut(memDC, px, py - 20, wName.c_str(), wName.length());

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
			TextOut(memDC, px, py - 20, wName.c_str(), wName.length());

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

	// 화면 복사
	BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
	SelectObject(memDC, oldBitmap);
	DeleteObject(memBitmap);
	DeleteDC(memDC);
	ReleaseDC(hWnd, hdc);
}
