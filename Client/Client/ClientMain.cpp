#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

#include <chrono>
#include <string>
#include <iostream>

#pragma pack(push, 1)
struct CSMovePacket
{
	uint32_t	size;
	int32_t		type;
	struct vector2
	{
		int x;
		int y;
	} dir;
};
struct SCMovePacket
{
	uint32_t	size;
	int32_t		type;
	struct vector2
	{
		int x;
		int y;
	} pos;
};
#pragma pack(pop)
void error_display(const std::wstring& msg, int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	std::wcout << msg;
	std::wcout << L"=== 에러 " << lpMsgBuf << std::endl;
	__debugbreak();
	// 디버깅 용
	LocalFree(lpMsgBuf);
}

std::string SERVER_IP = "127.0.0.1";
constexpr short SERVER_PORT = 3001;
constexpr int BUFFER_SIZE = 4096;
struct Position {
	int x;
	int y;
};
// Global state
Position playerPos = { 0, 0 };
constexpr int BOARD_SIZE = 8;
bool isRunning = true;
SOCKET g_socket = INVALID_SOCKET;
// Timing
std::chrono::steady_clock::time_point lastTime;

void send_move_packet(int dx, int dy) {
	CSMovePacket packet{};
	packet.size = sizeof(packet);
	packet.type = 1; // Move Packet Type
	packet.dir.x = dx;
	packet.dir.y = dy;
	WSABUF send_BUF{ sizeof(packet), reinterpret_cast<char*>(&packet) };
	DWORD send_size = 0;
	int ret = WSASend(g_socket, &send_BUF, 1, &send_size, 0, nullptr, nullptr);
	if (SOCKET_ERROR == ret)
	{
		error_display(L"WSASend Error", WSAGetLastError());
	}
}

static void recv_move_packet()
{
	char recv_buffer[BUFFER_SIZE];
	WSABUF recv_BUF{ BUFFER_SIZE, recv_buffer };
	DWORD recv_size = 0;
	DWORD recv_flag = 0;
	int ret = WSARecv(g_socket, &recv_BUF, 1, &recv_size, &recv_flag, nullptr, nullptr);
	if (SOCKET_ERROR == ret)
	{
		error_display(L"WSARecv Error", WSAGetLastError());
	}
	SCMovePacket movePacket{};
	memcpy(&movePacket, recv_buffer, sizeof(movePacket));
	if (movePacket.size != sizeof(movePacket))
	{
		error_display(L"Invalid packet size", -1);
	}
	playerPos.x = static_cast<int>(movePacket.pos.x);
	playerPos.y = static_cast<int>(movePacket.pos.y);
}

void update(float deltaTime) {
	
}

void render(HWND hWnd) {
	HDC hdc = GetDC(hWnd);

	RECT clientRect;
	GetClientRect(hWnd, &clientRect);
	int width = clientRect.right - clientRect.left;
	int height = clientRect.bottom - clientRect.top;

	if (width == 0 || height == 0) {
		ReleaseDC(hWnd, hdc);
		return;
	}

	int cellWidth = width / BOARD_SIZE;
	int cellHeight = height / BOARD_SIZE;

	HDC memDC = CreateCompatibleDC(hdc);
	HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
	HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

	// Fill background
	HBRUSH hBackground = CreateSolidBrush(RGB(255, 255, 255));
	FillRect(memDC, &clientRect, hBackground);
	DeleteObject(hBackground);

	// Draw 8x8 Checkerboard
	for (int y = 0; y < BOARD_SIZE; ++y) {
		for (int x = 0; x < BOARD_SIZE; ++x) {
			RECT rect = {
				x * cellWidth,
				y * cellHeight,
				(x + 1) * cellWidth,
				(y + 1) * cellHeight
			};
			HBRUSH hBrush = CreateSolidBrush((x + y) % 2 == 0 ? RGB(255, 255, 255) : RGB(200, 200, 200));
			FillRect(memDC, &rect, hBrush);
			DeleteObject(hBrush);
		}
	}

	// Draw Movable Piece
	HBRUSH hRedBrush = CreateSolidBrush(RGB(255, 0, 0));
	HBRUSH hOldBrush = (HBRUSH)SelectObject(memDC, hRedBrush);

	int padX = cellWidth * 15 / 100;
	int padY = cellHeight * 15 / 100;

	Ellipse(memDC,
		playerPos.x * cellWidth + padX,
		playerPos.y * cellHeight + padY,
		(playerPos.x + 1) * cellWidth - padX,
		(playerPos.y + 1) * cellHeight - padY);

	SelectObject(memDC, hOldBrush);
	DeleteObject(hRedBrush);

	// Copy to screen
	BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

	// Clean up
	SelectObject(memDC, oldBitmap);
	DeleteObject(memBitmap);
	DeleteDC(memDC);
	ReleaseDC(hWnd, hdc);
}

LRESULT CALLBACK window_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		BeginPaint(hWnd, &ps);
		render(hWnd);
		EndPaint(hWnd, &ps);
	}
				 break;

	case WM_KEYDOWN: {
		switch (wParam) {
		case VK_UP: 
		case VK_DOWN:  
		case VK_LEFT:  
		case VK_RIGHT:
			send_move_packet(
				(wParam == VK_LEFT) ? -1 : (wParam == VK_RIGHT) ? 1 : 0,
				(wParam == VK_UP) ? -1 : (wParam == VK_DOWN) ? 1 : 0
			);
			recv_move_packet();
			break;
		case VK_ESCAPE: isRunning = false; break;
		}
	}
				   break;

	case WM_DESTROY:
		isRunning = false;
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd) {
	const wchar_t CLASS_NAME[] = L"ServerTestWindowClass";

	WNDCLASS wc = {};
	wc.lpfnWndProc = window_proc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.style = CS_HREDRAW | CS_VREDRAW;

	RegisterClass(&wc);

	HWND hWnd = CreateWindowEx(
		0, CLASS_NAME, L"Server Test Client - PKJ",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 600, 600,
		NULL, NULL, hInstance, NULL
	);


	std::wcout.imbue(std::locale("korean"));
	WSADATA wsa_data{};
	WSAStartup(MAKEWORD(2, 2), &wsa_data);

	g_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);
	SOCKADDR_IN server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	inet_pton(AF_INET, SERVER_IP.c_str(), &server_addr.sin_addr);


	int ret = WSAConnect(g_socket, reinterpret_cast<sockaddr*>(&server_addr),
		sizeof(server_addr), nullptr, nullptr, nullptr, nullptr);
	if (SOCKET_ERROR == ret)
	{
		error_display(L"WSAConnect Error", WSAGetLastError());
	}

	if (hWnd == NULL) return 0;

	ShowWindow(hWnd, nShowCmd);

	lastTime = std::chrono::steady_clock::now();

	// --- GAME LOOP ---
	MSG msg = {};
	while (isRunning) {
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) break;
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {
			// Calculate Delta Time
			auto currentTime = std::chrono::steady_clock::now();
			float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
			lastTime = currentTime;

			update(deltaTime);
			render(hWnd);

		}
	}
	WSACleanup();
	return (int)msg.wParam;
}
