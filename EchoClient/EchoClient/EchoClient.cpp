// EchoClient.cpp : Win32 API 및 WinSock2 Overlapped I/O를 사용한 채팅 클라이언트
#include "pch.h"
#include "framework.h"
#include "EchoClient.h"

#include <clocale>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

#define MAX_LOADSTRING 100

// --- 네트워킹 설정 및 전역 변수 ---
const char* SERVER_IP = "127.0.0.1";
constexpr short SERVER_PORT = 3500;
constexpr int BUFFER_SIZE = 4096;

char g_recv_buffer[BUFFER_SIZE];
char g_send_buffer[BUFFER_SIZE];
WSABUF g_recv_wsa_buf{ BUFFER_SIZE, g_recv_buffer };
WSABUF g_send_wsa_buf{ BUFFER_SIZE, g_send_buffer };
WSAOVERLAPPED g_recv_overlapped{}, g_send_overlapped{};
SOCKET g_s_socket;

// --- UI 전역 변수 ---
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
HWND hChatLog;      // 채팅 내역 출력용 Read-only Edit
HWND hInputBox;     // 메시지 입력용 Edit
WNDPROC oldInputProc; // 입력창 기본 프로시저 저장용

// --- 패킷 클래스 (제공된 구조 유지) ---
class PACKET {
public:
    unsigned char m_size;
    unsigned char m_sender_id;
    char m_buf[BUFFER_SIZE];
    PACKET(int sender, const char* mess) : m_sender_id(static_cast<unsigned char>(sender))
    {
        m_size = static_cast<unsigned char>(strlen(mess) + 3);
        strcpy_s(m_buf, mess);
    }
};

// --- 함수 선언 ---
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK    InputSubclassProc(HWND, UINT, WPARAM, LPARAM);
void                AddLog(const std::wstring& text);
void                error_display(const wchar_t* msg, int err_no);
void                recv_from_server();
void CALLBACK       recv_callback(DWORD error, DWORD bytes_transferred, LPWSAOVERLAPPED overlapped, DWORD flags);
void CALLBACK       send_callback(DWORD error, DWORD bytes_transferred, LPWSAOVERLAPPED overlapped, DWORD flags);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int
    nCmdShow)
{
    setlocale(LC_ALL, "korean");
    // WinSock 초기화
    WSADATA wsa_data{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return FALSE;

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_ECHOCLIENT, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow)) return FALSE;

    // 서버 연결
    g_s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    SOCKADDR_IN server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (WSAConnect(g_s_socket, (SOCKADDR*)&server_addr, sizeof(server_addr), nullptr, nullptr, nullptr, nullptr)
        == SOCKET_ERROR) {
        error_display(L"서버 연결 실패", WSAGetLastError());
    }
	recv_from_server(); // 서버로부터 메시지 수신 시작

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_ECHOCLIENT));
    MSG msg;

    // Alertable Wait 상태를 지원하는 메시지 루프 (소켓 콜백 실행을 위해 필수)
    while (true)
    {
        DWORD result = MsgWaitForMultipleObjectsEx(0, NULL, INFINITE, QS_ALLINPUT, MWMO_ALERTABLE |
            MWMO_INPUTAVAILABLE);

        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) {
                WSACleanup();
                return (int)msg.wParam;
            }
            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEX) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ECHOCLIENT));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;
    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 600, 500, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return FALSE;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        // 채팅 로그창 (Read-only)
        hChatLog = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            10, 10, 560, 380, hWnd, (HMENU)101, hInst, NULL);

        // 입력창
        hInputBox = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            10, 400, 560, 30, hWnd, (HMENU)102, hInst, NULL);

        // 입력창 서브클래싱 (엔터 키 감지용)
        oldInputProc = (WNDPROC)SetWindowLongPtr(hInputBox, GWLP_WNDPROC, (LONG_PTR)InputSubclassProc);
        SetFocus(hInputBox);
        break;

    case WM_SIZE:
        MoveWindow(hChatLog, 10, 10, LOWORD(lParam) - 20, HIWORD(lParam) - 60, TRUE);
        MoveWindow(hInputBox, 10, HIWORD(lParam) - 40, LOWORD(lParam) - 20, 30, TRUE);
        break;

    case WM_DESTROY:
        closesocket(g_s_socket);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// 입력창에서 엔터를 누르면 서버로 전송
LRESULT CALLBACK InputSubclassProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_CHAR && wParam == VK_RETURN)
    {
        WCHAR wbuf[1024];
        GetWindowText(hWnd, wbuf, 1024);
        if (wcslen(wbuf) > 0)
        {
            char msg_ansi[1024];
            size_t converted;
            wcstombs_s(&converted, msg_ansi, sizeof(msg_ansi), wbuf, _TRUNCATE);

            g_send_wsa_buf.len = (ULONG)strlen(msg_ansi) + 1;
            memcpy(g_send_wsa_buf.buf, msg_ansi, g_send_wsa_buf.len);
            ZeroMemory(&g_send_overlapped, sizeof(g_send_overlapped));

            if (WSASend(g_s_socket, &g_send_wsa_buf, 1, nullptr, 0, &g_send_overlapped, send_callback) ==
                SOCKET_ERROR) {
                if (WSAGetLastError() != WSA_IO_PENDING) error_display(L"전송 실패", WSAGetLastError());
            }
            SetWindowText(hWnd, L""); // 입력창 비우기
        }
        return 0;
    }
    return CallWindowProc(oldInputProc, hWnd, message, wParam, lParam);
}

void AddLog(const std::wstring& text)
{
    int len = GetWindowTextLength(hChatLog);
    SendMessage(hChatLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hChatLog, EM_REPLACESEL, 0, (LPARAM)(text + L"\r\n").c_str());
}

void error_display(const wchar_t* msg, int err_no)
{
    WCHAR* lpMsgBuf;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, err_no, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
    AddLog(std::wstring(msg) + L" (Error: " + std::to_wstring(err_no) + L") " + lpMsgBuf);
    LocalFree(lpMsgBuf);
}

void recv_from_server()
{
    DWORD recv_flag = 0;
    ZeroMemory(&g_recv_overlapped, sizeof(g_recv_overlapped));
    if (WSARecv(g_s_socket, &g_recv_wsa_buf, 1, nullptr, &recv_flag, &g_recv_overlapped, recv_callback) ==
        SOCKET_ERROR) {
        if (WSAGetLastError() != WSA_IO_PENDING) error_display(L"수신 실패", WSAGetLastError());
    }
}

void CALLBACK recv_callback(DWORD error, DWORD bytes_transferred, LPWSAOVERLAPPED overlapped, DWORD flags)
{
    if (error != 0 || bytes_transferred == 0) return;

    PACKET* packet = reinterpret_cast<PACKET*>(g_recv_buffer);
    int remain = bytes_transferred;
    while (remain > 0) {
        if (remain < 2) break;
        int size = packet->m_size;

        WCHAR wbuf[BUFFER_SIZE];
        size_t converted;
        mbstowcs_s(&converted, wbuf, BUFFER_SIZE, packet->m_buf, _TRUNCATE);

        AddLog(L"Client [" + std::to_wstring(packet->m_sender_id) + L"] " + wbuf);

        remain -= size;
        packet = reinterpret_cast<PACKET*>(reinterpret_cast<char*>(packet) + packet->m_size);
    }
    recv_from_server();
}
void CALLBACK send_callback(DWORD error, DWORD bytes_transferred, LPWSAOVERLAPPED overlapped, DWORD flags) {}