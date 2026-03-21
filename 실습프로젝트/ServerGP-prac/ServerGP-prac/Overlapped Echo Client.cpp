#include <iostream>
#include <ws2tcpip.h>
#include <string>
#pragma comment(lib, "ws2_32.lib")

constexpr char SERVER_IP[] = "127.0.0.1";
constexpr short SERVER_PORT = 3000;
constexpr int BUFFER_SIZE = 4096;
char g_recv_buffer[BUFFER_SIZE];
char g_send_buffer[BUFFER_SIZE];
WSABUF g_send_buf{ BUFFER_SIZE, g_send_buffer };
WSABUF g_recv_buf{ BUFFER_SIZE, g_recv_buffer };
WSAOVERLAPPED g_send_overlapped{};
WSAOVERLAPPED g_recv_overlapped{};
SOCKET s_socket = INVALID_SOCKET;
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
void SendToServer();
void CALLBACK recv_callback(DWORD error, DWORD bytes_transferred, LPWSAOVERLAPPED overlapped, DWORD flags)
{
	if (error != 0)
	{
		error_display(L"recv_callback Error", WSAGetLastError());
	}
	std::cout << "Received " << g_recv_buffer;
	std::cout << ", Size: " << bytes_transferred << " bytes" << std::endl;

	SendToServer();
	
}
void CALLBACK send_callback(DWORD error, DWORD bytes_transferred, LPWSAOVERLAPPED overlapped, DWORD flags)
{
	if (error != 0)
	{
		error_display(L"send_callback Error", WSAGetLastError());
	}
	std::cout << "Sent " << bytes_transferred <<" bytes to server" << std::endl;

	DWORD recv_size = 0;
	DWORD recv_flag = 0;
	ZeroMemory(&g_recv_overlapped, sizeof(g_recv_overlapped));
	int ret = WSARecv(s_socket, &g_recv_buf, 1, nullptr,
		&recv_flag, &g_recv_overlapped, recv_callback);
	if (SOCKET_ERROR == ret)
	{
		int err_no = WSAGetLastError();
		if (err_no != WSA_IO_PENDING)
		{
			error_display(L"WSARecv Error", WSAGetLastError());
		}
	}
}

void SendToServer()
{
	
	std::cout << "Enter message to send ";
	std::cin.getline(g_send_buffer, BUFFER_SIZE);

	g_send_buf.len = static_cast<ULONG>(strlen(g_send_buffer)) + 1;
	ZeroMemory(&g_send_overlapped, sizeof(g_send_overlapped));
	DWORD sent_size = 0;
	int ret = WSASend(s_socket, &g_send_buf, 1, &sent_size, 0, &g_send_overlapped, send_callback);
	if (SOCKET_ERROR == ret)
	{
		error_display(L"WSASend Error", WSAGetLastError());
	}
}
int main()
{
	std::wcout.imbue(std::locale("korean"));
	WSADATA wsa_data{};
	WSAStartup(MAKEWORD(2, 2), &wsa_data); //마이크로소프트 네트워크사용할거면 초기화하고 해라 - 빌게이츠 <- 야이 새끼야!

	s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

	int ret = WSAConnect(s_socket, reinterpret_cast<sockaddr*>(&server_addr),
		sizeof(server_addr), nullptr, nullptr, nullptr, nullptr);
	if (SOCKET_ERROR == ret)
	{
		error_display(L"WSAConnect Error", WSAGetLastError());
	}

	SendToServer();
	for (;;)
	{
		SleepEx(0, TRUE); // Overlapped I/O의 콜백이 실행될 시간을 주기 위해 잠시 대기
	}
	WSACleanup();
	return 0;
}