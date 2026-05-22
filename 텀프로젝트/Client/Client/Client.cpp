#include "pch.h"
#include "GameManager.h"
#include "NetworkManager.h"
#include "RenderManager.h"

LRESULT CALLBACK window_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	auto gm = GameManager::Instance();

	switch (message) {
	case WM_CHAR: {
		if (gm->is_chatting()) {
			if (wParam == VK_BACK) {
				if (!gm->current_chat_input().empty()) {
					gm->current_chat_input().pop_back();
				}
			} else if (wParam == VK_RETURN) {
				// Handled in WM_KEYDOWN
			} else if (wParam >= 32) {
				gm->current_chat_input() += (wchar_t)wParam;
			}
			return 0;
		}
		break;
	}
	case WM_KEYDOWN: {
		if (!gm->players().contains(gm->my_id())) break;

		// Chat Toggle & Sending
		if (wParam == VK_RETURN) {
			if (gm->is_chatting()) {
				// Send Chat
				std::wstring wmsg = gm->current_chat_input();
				if (!wmsg.empty()) {
					int len = WideCharToMultiByte(CP_ACP, 0, wmsg.c_str(), -1, NULL, 0, NULL, NULL);
					std::string msg(len, 0);
					WideCharToMultiByte(CP_ACP, 0, wmsg.c_str(), -1, &msg[0], len, NULL, NULL);
					if (!msg.empty() && msg.back() == '\0') msg.pop_back();

					C2S_Chat p;
					p.size = sizeof(p);
					p.type = C2S_CHAT;
					strncpy_s(p.message, msg.c_str(), MAX_CHAT_MSG_LEN - 1);
					NetworkManager::Instance()->send_packet(&p);
					gm->current_chat_input().clear();
				}
				gm->set_chatting(false);
			} else {
				gm->set_chatting(true);
			}
			return 0;
		}

		if (gm->is_chatting()) return 0; // Ignore other keys when chatting

		if (wParam == 'E') {
			gm->toggle_inventory();
			return 0;
		}
		if (wParam == VK_SPACE) {
			// Interact / Next dialogue (placeholder for later)
			return 0;
		}

		int curr_x = gm->players()[gm->my_id()].x;
		int curr_y = gm->players()[gm->my_id()].y;

		short dx = 0;
		short dy = 0;

		switch (wParam) {
		case VK_LEFT:  dx = -1; break;
		case VK_RIGHT: dx = 1;  break;
		case VK_UP:    dy = 1;  break; // Y-up: 위로 가면 Y 증가 (+1)
		case VK_DOWN:  dy = -1; break; // Y-up: 아래로 가면 Y 감소 (-1)
		case VK_ESCAPE: gm->set_running(false); return 0;
		default: return DefWindowProc(hWnd, message, wParam, lParam);
		}

		// 클라이언트 사이드 충돌 체크 (현재 위치 + 방향)
		if (gm->can_move(curr_x + dx, curr_y + dy)) {

			// C2S_Move는 방향(dx, dy)을 담아서 서버로 전송
			C2S_Move p;
			p.size = sizeof(p);
			p.type = C2S_MOVE;
			p.x = dx;
			p.y = dy;
			p.move_time = 0;

			NetworkManager::Instance()->send_packet(&p);

			// 참고: S2C_MOVE_OBJECT를 받기 전까지 클라이언트 화면은
			// 갱신되지 않으므로, 0.5초 쿨타임 처리는 서버가 담당합니다.
		}
		return 0;
	}
	break;
	case WM_DESTROY: GameManager::Instance()->set_running(false); PostQuitMessage(0); return 0; break;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR gdiplusToken;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
	const wchar_t CLASS_NAME[] = L"ServerTestWindowClass";
	WNDCLASS wc = {};
	wc.lpfnWndProc = window_proc;
	wc.hInstance = hI;
	wc.lpszClassName = CLASS_NAME;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	RegisterClass(&wc);

	HWND hWnd = CreateWindowEx(0, CLASS_NAME, L"Chess Client - PKJ", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 600, 600, NULL, NULL, hI, NULL);

	AllocConsole();
	FILE* f;
	freopen_s(&f, "CONIN$", "r", stdin);
	freopen_s(&f, "CONOUT$", "w", stdout);
	freopen_s(&f, "CONOUT$", "w", stderr);

	auto gm = GameManager::Instance();
	auto nm = NetworkManager::Instance();
	auto rm = RenderManager::Instance();

	gm->Init();

	std::string server_ip;
	std::cout << "Enter server IP (default 127.0.0.1): ";
	std::getline(std::cin, server_ip);
	if (server_ip.empty()) server_ip = "127.0.0.1";

	std::string username;
	std::cout << "Enter username: ";
	std::getline(std::cin, username);
	if (username.length() >= MAX_NAME_LEN) username = username.substr(0, MAX_NAME_LEN - 1);
	gm->set_username(username);

	HBITMAP hBoardBmp = (HBITMAP)LoadImage(NULL, L"chessmap.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
	rm->set_board_bitmap(hBoardBmp);

	if (nm->Connect(server_ip, PORT) == false) {
		std::cout << "Connect failed\n";
		return 0;
	}

	ShowWindow(hWnd, nS);

	MSG msg = {};
	while (gm->is_running()) {
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {
			nm->process_network();
			rm->Render(hWnd);
		}
	}
	nm->Disconnect();
	gm->Release();
	Gdiplus::GdiplusShutdown(gdiplusToken);

	return 0;
}
