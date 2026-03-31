#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

#include <chrono>
#include <string>
#include <iostream>
#include <unordered_map>

enum class PacketType : int32_t
{
	CS_Move = 1,
	SC_Move = 2,
	CS_Login = 10,
	SC_LoginAck = 11,
	CS_Logout = 12,
	SC_Logout = 13,
	SC_Spawn = 20,
};

#pragma pack(push, 1)
struct CSMovePacket
{
	uint32_t	size;
	PacketType	type;
	struct { int x, y; } dir;
};
struct SCMovePacket
{
	uint32_t	size;
	PacketType	type;
	long long	client_id;
	struct { int x, y; } pos;
};
struct CSLoginPacket
{
	uint32_t	size;
	PacketType	type;
};
struct SCLoginAckPacket
{
	uint32_t	size;
	PacketType	type;
	long long	client_id;
};
struct SCSpawnPacket
{
	uint32_t	size;
	PacketType	type;
	long long	client_id;
	struct { int x, y; } pos;
};
struct SCLogoutPacket
{
	uint32_t	size;
	PacketType	type;
	long long	client_id;
};
#pragma pack(pop)

void error_display(const std::wstring& msg, int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
	std::wcout << msg << L"=== 에러 " << lpMsgBuf << std::endl;
	LocalFree(lpMsgBuf);
}

std::string SERVER_IP = "127.0.0.1";
constexpr short SERVER_PORT = 3001;
constexpr int BUFFER_SIZE = 4096;
constexpr int BOARD_SIZE = 8;

struct Position { int x, y; };
std::unordered_map<long long, Position> g_players;
long long g_my_id = -1;

SOCKET g_socket = INVALID_SOCKET;
bool isRunning = true;
std::chrono::steady_clock::time_point lastTime;

void send_packet(void* packet, int size) {
	WSABUF buf{ (ULONG)size, (char*)packet };
	DWORD sent;
	WSASend(g_socket, &buf, 1, &sent, 0, nullptr, nullptr);
}

void process_packet(char* ptr) {
	PacketType type = *reinterpret_cast<PacketType*>(ptr + 4);
	switch (type) {
	case PacketType::SC_LoginAck: {
		SCLoginAckPacket* p = reinterpret_cast<SCLoginAckPacket*>(ptr);
		g_my_id = p->client_id;
		std::cout << "Login Success. My ID: " << g_my_id << "\n";
		break;
	}
	case PacketType::SC_Spawn: {
		SCSpawnPacket* p = reinterpret_cast<SCSpawnPacket*>(ptr);
		g_players[p->client_id] = { p->pos.x, p->pos.y };
		break;
	}
	case PacketType::SC_Move: {
		SCMovePacket* p = reinterpret_cast<SCMovePacket*>(ptr);
		g_players[p->client_id] = { p->pos.x, p->pos.y };
		break;
	}
	case PacketType::SC_Logout: {
		SCLogoutPacket* p = reinterpret_cast<SCLogoutPacket*>(ptr);
		g_players.erase(p->client_id);
		break;
	}
	}
}

void process_network() {
	static char recv_buf[BUFFER_SIZE];
	static int curr_size = 0;

	fd_set read_set;
	FD_ZERO(&read_set);
	FD_SET(g_socket, &read_set);
	timeval tv{ 0, 0 };

	if (select(0, &read_set, nullptr, nullptr, &tv) > 0) {
		int ret = recv(g_socket, recv_buf + curr_size, BUFFER_SIZE - curr_size, 0);
		if (ret <= 0) 
		{ 
			isRunning = false; 
			return; 
		}
		curr_size += ret;

		while (curr_size >= 4) {
			uint32_t packet_size = *reinterpret_cast<uint32_t*>(recv_buf);
			if (curr_size < (int)packet_size) break;
			process_packet(recv_buf);
			curr_size -= packet_size;
			if (curr_size > 0) memmove(recv_buf, recv_buf + packet_size, curr_size);
		}
	}
}

void render(HWND hWnd) {
	HDC hdc = GetDC(hWnd);
	RECT clientRect;
	GetClientRect(hWnd, &clientRect);
	int width = clientRect.right - clientRect.left;
	int height = clientRect.bottom - clientRect.top;
	if (width <= 0 || height <= 0) { ReleaseDC(hWnd, hdc); return; }

	int cellWidth = width / BOARD_SIZE;
	int cellHeight = height / BOARD_SIZE;

	HDC memDC = CreateCompatibleDC(hdc);
	HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
	HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

	FillRect(memDC, &clientRect, (HBRUSH)GetStockObject(WHITE_BRUSH));

	for (int y = 0; y < BOARD_SIZE; ++y) {
		for (int x = 0; x < BOARD_SIZE; ++x) {
			RECT rect = { x * cellWidth, y * cellHeight, (x + 1) * cellWidth, (y + 1) * cellHeight };
			HBRUSH hBrush = CreateSolidBrush((x + y) % 2 == 0 ? RGB(255, 255, 255) : RGB(230, 230, 230));
			FillRect(memDC, &rect, hBrush);
			DeleteObject(hBrush);
		}
	}

	for (auto& [id, pos] : g_players) {
		HBRUSH hBrush = CreateSolidBrush(id == g_my_id ? RGB(255, 0, 0) : RGB(0, 0, 255));
		HBRUSH oldB = (HBRUSH)SelectObject(memDC, hBrush);
		int padX = cellWidth * 15 / 100;
		int padY = cellHeight * 15 / 100;
		Ellipse(memDC, pos.x * cellWidth + padX, pos.y * cellHeight + padY, 
			(pos.x + 1) * cellWidth - padX, (pos.y + 1) * cellHeight - padY);
		SelectObject(memDC, oldB);
		DeleteObject(hBrush);
	}

	BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
	SelectObject(memDC, oldBitmap);
	DeleteObject(memBitmap);
	DeleteDC(memDC);
	ReleaseDC(hWnd, hdc);
}

LRESULT CALLBACK window_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_KEYDOWN: {
		switch (wParam) {
		case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT: {
			CSMovePacket p;
			p.size = sizeof(p); p.type = PacketType::CS_Move;
			p.dir.x = (wParam == VK_LEFT) ? -1 : (wParam == VK_RIGHT) ? 1 : 0;
			p.dir.y = (wParam == VK_UP) ? -1 : (wParam == VK_DOWN) ? 1 : 0;
			send_packet(&p, sizeof(p));
			break;
		}
		case VK_ESCAPE: isRunning = false; break;
		}
		return 0;
	}
	case WM_DESTROY: isRunning = false; PostQuitMessage(0); return 0;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
	const wchar_t CLASS_NAME[] = L"ServerTestWindowClass";
	WNDCLASS wc = {}; wc.lpfnWndProc = window_proc; wc.hInstance = hI; wc.lpszClassName = CLASS_NAME;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW); RegisterClass(&wc);

	HWND hWnd = CreateWindowEx(0, CLASS_NAME, L"Server Test Client - PKJ", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 600, 600, NULL, NULL, hI, NULL);

	AllocConsole();
	FILE* f; 
	freopen_s(&f, "CONIN$", "r", stdin);
	freopen_s(&f, "CONOUT$", "w", stdout);
	freopen_s(&f, "CONOUT$", "w", stderr);

	std::cout << "Server IP (Default 127.0.0.1): ";
	std::string input_ip;
	std::getline(std::cin, input_ip);
	if (!input_ip.empty()) SERVER_IP = input_ip;

	WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
	g_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);
	SOCKADDR_IN addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(SERVER_PORT);
	inet_pton(AF_INET, SERVER_IP.c_str(), &addr.sin_addr);

	if (connect(g_socket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
		error_display(L"Connect Fail", WSAGetLastError());
		return 0;
	}

	CSLoginPacket login_packet; 
	login_packet.size = sizeof(login_packet);
	login_packet.type = PacketType::CS_Login;
	send_packet(&login_packet, sizeof(login_packet));

	ShowWindow(hWnd, nS);
	lastTime = std::chrono::steady_clock::now();
	MSG msg = {};
	while (isRunning) {
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg); DispatchMessage(&msg);
		}
		else {
			process_network();
			render(hWnd);
			Sleep(10);
		}
	}
	WSACleanup();
	return 0;
}