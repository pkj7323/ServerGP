#include <windows.h>
#include <vector>
#include <string>
#include <chrono>

// Naming Conventions from GEMINI.md:
// Types: PascalCase
// Functions: snake_case
// Member Variables: _camelCase

struct Position {
    int x;
    int y;
};

// Global state
Position _playerPos = { 0, 0 };
const int BOARD_SIZE = 8;
bool _isRunning = true;

// Timing
std::chrono::steady_clock::time_point _lastTime;

void update(float deltaTime) {
    // Game logic update (e.g., animations, server reconciliation)
    // Currently, movement is handled via WM_KEYDOWN for simplicity, 
    // but we could move it here for smoother input polling.
}

void render(HWND hWnd) {
    PAINTSTRUCT ps;
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
        _playerPos.x * cellWidth + padX, 
        _playerPos.y * cellHeight + padY, 
        (_playerPos.x + 1) * cellWidth - padX, 
        (_playerPos.y + 1) * cellHeight - padY);

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
        case VK_UP:    if (_playerPos.y > 0) _playerPos.y--; break;
        case VK_DOWN:  if (_playerPos.y < BOARD_SIZE - 1) _playerPos.y++; break;
        case VK_LEFT:  if (_playerPos.x > 0) _playerPos.x--; break;
        case VK_RIGHT: if (_playerPos.x < BOARD_SIZE - 1) _playerPos.x++; break;
        case VK_ESCAPE: _isRunning = false; break;
        }
    }
    break;

    case WM_DESTROY:
        _isRunning = false;
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
        0, CLASS_NAME, L"Server Test Client - Game Loop Pattern",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 600,
        NULL, NULL, hInstance, NULL
    );

    if (hWnd == NULL) return 0;

    ShowWindow(hWnd, nShowCmd);

    _lastTime = std::chrono::steady_clock::now();

    // --- GAME LOOP ---
    MSG msg = {};
    while (_isRunning) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            // Calculate Delta Time
            auto currentTime = std::chrono::steady_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - _lastTime).count();
            _lastTime = currentTime;

            update(deltaTime);
            render(hWnd);

        }
    }

    return (int)msg.wParam;
}
