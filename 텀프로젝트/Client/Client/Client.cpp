#include "pch.h"

#include "GameManager.h"
#include "NetworkManager.h"
#include "RenderManager.h"


int g_client_move_cooldown_ms = 50;
auto g_last_move_time = std::chrono::steady_clock::now();
void load_client_config() {
	std::ifstream file("Data/player_config.lua");
	if (!file.is_open()) {
		std::cout << "[Client] Warning: Could not open player_config.lua, using defaults.\n";
		return;
	}
	std::string line;
	std::regex re("move_cooldown_ms\\s*=\\s*(\\d+)");
	std::smatch match;
	while (std::getline(file, line)) {
		if (std::regex_search(line, match, re)) {
			g_client_move_cooldown_ms = std::stoi(match[1].str());
			std::cout << "[Client] Loaded move_cooldown_ms: " << g_client_move_cooldown_ms << "ms\n";
			return;
		}
	}
}

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

		if (gm->players()[gm->my_id()].hp <= 0) {
			if (wParam == VK_ESCAPE) {
				gm->set_running(false);
				return 0;
			}
			return 0; // Ignore other input when dead
		}


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
					strncpy_s(p.message, sizeof(p.message), msg.c_str(), _TRUNCATE);
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

		if (gm->is_trading()) {
			if (wParam == VK_ESCAPE) {
				gm->set_trading(false);
				return 0;
			}
			if (wParam >= '1' && wParam <= '9') {
				C2S_TradeCommand p;
				p.size = sizeof(p);
				p.type = C2S_TRADE_COMMAND;
				p.trade_index = static_cast<char>(wParam - '0');
				p.npc_id = gm->trade_npc_id();
				NetworkManager::Instance()->send_packet(&p);
				// We don't automatically close trading. The user can trade multiple times until they press ESC.
				return 0;
			}
			return 0; // Ignore other keys while trading
		}

		if (gm->show_skill_tree()) {
			if (wParam == VK_ESCAPE) {
				gm->toggle_skill_tree();
				return 0;
			}
			if (wParam >= '1' && wParam <= '4') {
				C2S_LearnSkill p;
				p.size = sizeof(p);
				p.type = C2S_LEARN_SKILL;
				p.skill_type = static_cast<char>(wParam - '1'); // Map '1'-'4' to 0-3
				NetworkManager::Instance()->send_packet(&p);
				return 0;
			}
			return 0; // Ignore other keys while skill tree is open
		}

		if (wParam == 'E') {
			gm->toggle_inventory();
			return 0;
		}
		if (wParam == 'K') {
			gm->toggle_skill_tree();
			return 0;
		}
		if (wParam == 'F') {
			C2S_InteractNpc p;
			p.size = sizeof(p);
			p.type = C2S_INTERACT_NPC;
			NetworkManager::Instance()->send_packet(&p);
			return 0;
		}
		if (wParam == '1' || wParam == '2' || wParam == '3') {
			auto now = std::chrono::steady_clock::now();
			if (wParam == '1' && std::chrono::duration_cast<std::chrono::milliseconds>(now - gm->last_potion_use()).count() >= 5000) {
				if (gm->my_inventory()[static_cast<int>(ItemType::HEALTH_POTION)] > 0) {
					gm->start_potion_cooldown();
					C2S_UseQuickSlot p;
					p.size = sizeof(p);
					p.type = C2S_USE_QUICKSLOT;
					p.slot_id = 1;
					NetworkManager::Instance()->send_packet(&p);
				}
			} else if (wParam == '2' && std::chrono::duration_cast<std::chrono::milliseconds>(now - gm->last_skill_use()).count() >= 10000) {
				gm->start_skill_cooldown();
				C2S_UseQuickSlot p;
				p.size = sizeof(p);
				p.type = C2S_USE_QUICKSLOT;
				p.slot_id = 2;
				NetworkManager::Instance()->send_packet(&p);
			} else if (wParam == '3' && std::chrono::duration_cast<std::chrono::milliseconds>(now - gm->last_ender_pearl_use()).count() >= 10000) {
				gm->start_ender_pearl_cooldown();
				C2S_UseQuickSlot p;
				p.size = sizeof(p);
				p.type = C2S_USE_QUICKSLOT;
				p.slot_id = 3;
				NetworkManager::Instance()->send_packet(&p);
			}
			return 0;
		}
		if (wParam >= VK_F1 && wParam <= VK_F7) {
			if ((lParam & 0x40000000) == 0) { // Ignore autorepeat
				C2S_Cheat p;
				p.size = sizeof(p);
				p.type = C2S_CHEAT;
				p.cheat_type = static_cast<CheatType>(wParam - VK_F1 + 1);
				NetworkManager::Instance()->send_packet(&p);
			}
			return 0;
		}
		if (wParam >= '4' && wParam <= '8') {
			if ((lParam & 0x40000000) == 0) { // Ignore autorepeat
				C2S_Cheat p;
				p.size = sizeof(p);
				p.type = C2S_CHEAT;
				// TP_IRON_GOLEM is 8. '4' maps to 8, '8' maps to 12.
				p.cheat_type = static_cast<CheatType>(8 + (wParam - '4'));
				NetworkManager::Instance()->send_packet(&p);
			}
			return 0;
		}
		if (wParam == VK_SPACE) {
			C2S_Attack p;
			p.size = sizeof(p);
			p.type = C2S_ATTACK;
			NetworkManager::Instance()->send_packet(&p);
			return 0;
		}

		switch (wParam) {
		case VK_LEFT:
		case VK_RIGHT:
		case VK_UP:
		case VK_DOWN:
			return 0; // handled by GetAsyncKeyState in main loop
		case VK_ESCAPE: gm->set_running(false); return 0;
		default: return DefWindowProc(hWnd, message, wParam, lParam);
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

	RECT rect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
	int wndWidth = rect.right - rect.left;
	int wndHeight = rect.bottom - rect.top;

	HWND hWnd = CreateWindowEx(0, CLASS_NAME, L"Chess Client - PKJ", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, wndWidth, wndHeight, NULL, NULL, hI, NULL);

	AllocConsole();
	FILE* f;
	freopen_s(&f, "CONIN$", "r", stdin);
	freopen_s(&f, "CONOUT$", "w", stdout);
	freopen_s(&f, "CONOUT$", "w", stderr);

	auto gm = GameManager::Instance();
	auto nm = NetworkManager::Instance();
	auto rm = RenderManager::Instance();

	gm->Init();
	load_client_config();

	std::string server_ip;
	std::cout << "Enter server IP (default 127.0.0.1): ";
	std::getline(std::cin, server_ip);
	if (server_ip.empty()) server_ip = "127.0.0.1";

	std::string user_id;
	std::cout << "Enter user id: ";
	std::getline(std::cin, user_id);
	if (user_id.length() >= MAX_NAME_LEN) user_id = user_id.substr(0, MAX_NAME_LEN - 1);
	gm->set_user_id(user_id);

	std::string username;
	std::cout << "Enter username: ";
	std::getline(std::cin, username);
	if (username.length() >= MAX_NAME_LEN) username = username.substr(0, MAX_NAME_LEN - 1);
	gm->set_username(username);

	HBITMAP hBoardBmp = (HBITMAP)LoadImage(NULL, L"chessmap.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
	rm->set_board_bitmap(hBoardBmp);

	if (nm->Connect(server_ip, PORT) == false) {
		std::cout << "Connect failed\n";
		system("pause");
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
			if (gm->players().contains(gm->my_id()) && gm->players()[gm->my_id()].hp > 0 && !gm->is_chatting() && !gm->show_inventory() && !gm->is_trading()) {
				auto now = std::chrono::steady_clock::now();
				auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_move_time).count();
				if (elapsed >= g_client_move_cooldown_ms) {
					short dx = 0;
					short dy = 0;
					if (GetAsyncKeyState(VK_LEFT) & 0x8000) dx -= 1;
					if (GetAsyncKeyState(VK_RIGHT) & 0x8000) dx += 1;
					if (GetAsyncKeyState(VK_UP) & 0x8000) dy += 1;
					if (GetAsyncKeyState(VK_DOWN) & 0x8000) dy -= 1;

					// WASD support
					if (GetAsyncKeyState('A') & 0x8000) dx -= 1;
					if (GetAsyncKeyState('D') & 0x8000) dx += 1;
					if (GetAsyncKeyState('W') & 0x8000) dy += 1;
					if (GetAsyncKeyState('S') & 0x8000) dy -= 1;

					// Clamp to -1, 0, 1
					dx = std::clamp(dx, static_cast<short>(-1), static_cast<short>(1));
					dy = std::clamp(dy, static_cast<short>(-1), static_cast<short>(1));

					if (dx != 0 || dy != 0) {
						int curr_x = gm->players()[gm->my_id()].x;
						int curr_y = gm->players()[gm->my_id()].y;
						if (gm->can_move(curr_x + dx, curr_y + dy)) {
							C2S_Move p;
							p.size = sizeof(p);
							p.type = C2S_MOVE;
							p.x = dx;
							p.y = dy;
							p.move_time = 0;
							NetworkManager::Instance()->send_packet(&p);
							g_last_move_time = now;
						}
					}
				}
			}

			nm->process_network();
			rm->Render(hWnd);
		}
	}
	nm->Disconnect();
	gm->Release();
	Gdiplus::GdiplusShutdown(gdiplusToken);

	return 0;
}
