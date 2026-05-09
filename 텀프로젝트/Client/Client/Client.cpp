#include "pch.h"
#include "GameManager.h"
#include "NetworkManager.h"
#include "RenderManager.h"

LRESULT CALLBACK window_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_KEYDOWN: {
		C2S_Move p;
		p.size = sizeof(p);
		p.type = C2S_MOVE;
		
		switch (wParam) {
		case VK_LEFT:  p.dir = LEFT; break;
		case VK_RIGHT: p.dir = RIGHT; break;
		case VK_UP:    p.dir = UP; break;
		case VK_DOWN:  p.dir = DOWN; break;
		case VK_ESCAPE: GameManager::Instance()->set_running(false); break;
		}

		NetworkManager::Instance()->send_packet(&p);
		
		return 0;
	}
	case WM_DESTROY: GameManager::Instance()->set_running(false); PostQuitMessage(0); return 0;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
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

	return 0;
}
