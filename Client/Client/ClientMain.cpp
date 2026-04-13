#define NOMINMAX
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

#include <algorithm>
#include <chrono>
#include <string>
#include <iostream>
#include <unordered_map>
#include "../../Server/Server/Protocol.h"

// Player struct to store information for ourselves and others
struct Player {
    int id;
    std::string name;
    int16_t x, y;
};

// Global state
std::unordered_map<int, Player> g_players;
int g_my_id = -1;
SOCKET g_socket = INVALID_SOCKET;
bool isRunning = true;
std::string g_username;
HBITMAP g_hBoardBmp = NULL;

// Constants for rendering
constexpr int CELL_SIZE = 65;
constexpr int VIEW_WIDTH = 16;
constexpr int VIEW_HEIGHT = 16;
constexpr int WINDOW_WIDTH = VIEW_WIDTH * CELL_SIZE;
constexpr int WINDOW_HEIGHT = VIEW_HEIGHT * CELL_SIZE;

void error_display(const std::wstring& msg, int err_no) {
    WCHAR* lpMsgBuf;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, err_no,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
    std::wcout << msg << L"=== 에러: " << lpMsgBuf << std::endl;
    LocalFree(lpMsgBuf);
}

void send_packet(void* packet) {
    unsigned char size = *reinterpret_cast<unsigned char*>(packet);
    WSABUF buf{ (ULONG)size, (char*)packet };
    DWORD sent;
    if (WSASend(g_socket, &buf, 1, &sent, 0, nullptr, nullptr) == SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            isRunning = false;
        }
    }
}

void process_packet(char* ptr) {
    packet_header* header = reinterpret_cast<packet_header*>(ptr);
    switch (header->type) {
    case packet_type::S2C_LOGIN_ACK: {
        s2c_login_ack* p = reinterpret_cast<s2c_login_ack*>(ptr);
        if (p->success) {
            std::cout << "Login successful. Message: " << p->msg << "\n";
        } else {
            std::cout << "Login failed. Message: " << p->msg << "\n";
            isRunning = false;
        }
        break;
    }
    case packet_type::S2C_AVATAR_INFO: {
        s2c_avatar_info* p = reinterpret_cast<s2c_avatar_info*>(ptr);
        g_my_id = p->id;
        g_players[g_my_id] = { p->id, g_username, p->x, p->y };
        std::cout << "Avatar Info: ID=" << g_my_id << " at (" << p->x << ", " << p->y << ")\n";
        break;
    }
    case packet_type::S2C_ADD_PLAYER: {
        s2c_add_player* p = reinterpret_cast<s2c_add_player*>(ptr);
        if (p->id == g_my_id) break;
        g_players[p->id] = { p->id, p->userName, p->x, p->y };
        std::cout << "Add Player: ID=" << p->id << ", Name=" << p->userName << " at (" << p->x << ", " << p->y << ")\n";
        break;
    }
    case packet_type::S2C_PLAYER_MOVE: {
        s2c_player_move* p = reinterpret_cast<s2c_player_move*>(ptr);
        if (g_players.count(p->id)) {
            g_players[p->id].x = p->x;
            g_players[p->id].y = p->y;
        }
        break;
    }
    case packet_type::S2C_REMOVE_PLAYER: {
        s2c_remove_player* p = reinterpret_cast<s2c_remove_player*>(ptr);
        g_players.erase(p->id);
        std::cout << "Remove Player: ID=" << p->id << "\n";
        break;
    }
    }
}

void process_network() {
    static char recv_buf[1024];
    static int curr_size = 0;

    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(g_socket, &read_set);
    timeval tv{ 0, 0 };

    if (select(0, &read_set, nullptr, nullptr, &tv) > 0) {
        int ret = recv(g_socket, recv_buf + curr_size, 1024 - curr_size, 0);
        if (ret == 0) {
            isRunning = false;
            return;
        }
        if (ret == SOCKET_ERROR) {
            if (WSAGetLastError() != WSAEWOULDBLOCK) isRunning = false;
            return;
        }
        curr_size += ret;

        while (curr_size >= 1) {
            unsigned char packet_size = static_cast<unsigned char>(recv_buf[0]);
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

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    HDC boardDC = CreateCompatibleDC(hdc);
    HBITMAP oldBoardBmp = NULL;
    if (g_hBoardBmp) oldBoardBmp = (HBITMAP)SelectObject(boardDC, g_hBoardBmp);

    // Background
    FillRect(memDC, &clientRect, (HBRUSH)GetStockObject(WHITE_BRUSH));

    int cellWidth = width / VIEW_WIDTH;
    int cellHeight = height / VIEW_HEIGHT;

    // Viewport calculation (Centered on player, clamped to world bounds)
    int left_x = 0, top_y = 0;
    if (g_players.contains(g_my_id)) {
        left_x = g_players[g_my_id].x - VIEW_WIDTH / 2;
        top_y = g_players[g_my_id].y - VIEW_HEIGHT / 2;

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

            if (g_hBoardBmp) {
                int srcX = (world_x / 3 + world_y / 3) % 2 == 0 ? 5 : 69;
                int srcY = 5;
                StretchBlt(memDC, x * cellWidth, y * cellHeight, cellWidth, cellHeight, boardDC, srcX, srcY, 65, 65, SRCCOPY);
            } else {
                RECT rect = { x * cellWidth, y * cellHeight, (x + 1) * cellWidth, (y + 1) * cellHeight };
                HBRUSH hBrush = CreateSolidBrush((world_x + world_y) % 2 == 0 ? RGB(255, 255, 255) : RGB(230, 230, 230));
                FillRect(memDC, &rect, hBrush);
                DeleteObject(hBrush);
            }
        }
    }

    // Players
    for (auto& [id, player] : g_players) {
        int rel_x = player.x - left_x;
        int rel_y = player.y - top_y;

        if (rel_x >= 0 && rel_x < VIEW_WIDTH && rel_y >= 0 && rel_y < VIEW_HEIGHT) {
            HBRUSH hBrush = CreateSolidBrush(id == g_my_id ? RGB(255, 0, 0) : RGB(0, 0, 255));
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
    if (g_players.contains(g_my_id)) {
        wchar_t status[128];
        swprintf_s(status, L"Pos: (%d, %d)", g_players[g_my_id].x, g_players[g_my_id].y);
        SetTextColor(memDC, RGB(0, 0, 0));
        TextOut(memDC, 10, 10, status, (int)wcslen(status));
    }

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
    
    if (g_hBoardBmp) SelectObject(boardDC, oldBoardBmp);
    DeleteDC(boardDC);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    ReleaseDC(hWnd, hdc);
}

LRESULT CALLBACK window_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_KEYDOWN: {
        int16_t dx = 0, dy = 0;
        switch (wParam) {
        case VK_LEFT:  dx = -1; break;
        case VK_RIGHT: dx = 1; break;
        case VK_UP:    dy = -1; break;
        case VK_DOWN:  dy = 1; break;
        case VK_ESCAPE: isRunning = false; break;
        }
        if (dx != 0 || dy != 0) {
            c2s_move p;
            p.size = sizeof(p);
            p.type = packet_type::C2S_MOVE;
            p.dir.x = dx;
            p.dir.y = dy;
            send_packet(&p);
        }
        return 0;
    }
    case WM_DESTROY: isRunning = false; PostQuitMessage(0); return 0;
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

    std::cout << "Enter username: ";
    std::getline(std::cin, g_username);
    if (g_username.length() >= MAX_NAME_LEN) g_username = g_username.substr(0, MAX_NAME_LEN - 1);

    g_hBoardBmp = (HBITMAP)LoadImage(NULL, L"chessmap.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    g_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);
    
    // Set non-blocking
    unsigned long arg = 1;
    ioctlsocket(g_socket, FIONBIO, &arg);

    SOCKADDR_IN addr{}; 
    addr.sin_family = AF_INET; 
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    connect(g_socket, (sockaddr*)&addr, sizeof(addr));

    c2s_login login_pkt;
    login_pkt.size = sizeof(login_pkt);
    login_pkt.type = packet_type::C2S_LOGIN;
    strcpy_s(login_pkt.userName, g_username.c_str());
    send_packet(&login_pkt);

    ShowWindow(hWnd, nS);
    
    MSG msg = {};
    while (isRunning) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); 
            DispatchMessage(&msg);
        } else {
            process_network();
            render(hWnd);
        }
    }
    
    if (g_socket != INVALID_SOCKET) closesocket(g_socket);
    WSACleanup();
    return 0;
}
