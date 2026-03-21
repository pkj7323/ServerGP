#include <iostream>
#include <WS2tcpip.h>
using namespace std;
#pragma comment (lib, "WS2_32.LIB")
const short SERVER_PORT = 3000;
const int BUFSIZE = 256;

void error_display(const wstring& msg, int err_no)
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

int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 0), &WSAData);

	SOCKET s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, 0);
	SOCKADDR_IN server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	auto ret = bind(s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	if (ret == SOCKET_ERROR)
	{
		error_display(L"bind() 실패", WSAGetLastError());
	}
	listen(s_socket, SOMAXCONN);

	INT addr_size = sizeof(server_addr);
	//내가 넣어주는 서버 주소 구조체의 크기를 넣어줘야함 딱 맞춰서 넣어줘야 짤리거나 오버플로우 문제가 안남
	SOCKET c_socket = WSAAccept(s_socket, reinterpret_cast<sockaddr*>(&server_addr), &addr_size, nullptr, 0);
	//Accept에서 연결이 완료되면 클라이언트 소켓이 반환됨 
	for (;;) {
		char recv_buf[BUFSIZE];
		WSABUF mybuf{BUFSIZE, recv_buf};
		DWORD recv_byte = 0; // 몇 바이트가 recv되는지 리턴되는 변수
		DWORD recv_flag = 0; // recv_flag는 recv할 때 옵션을 줄 수 있는 변수인데 지금은 옵션이 없으니까 0으로 초기화 해주면 됨
		WSARecv(c_socket, &mybuf, 1, &recv_byte, 
			&recv_flag, nullptr, nullptr);
		cout << "Client Sent [" << recv_byte << "bytes] : " << recv_buf << endl;
		
		DWORD sent_byte; // 몇 바이트가 send가 되는지 리턴되는 변수
		mybuf.len = recv_byte; // BUFSIZE로 하면 256바이트를 보내버리니까 recv_byte로 바꿔줘야 내가 받은 만큼만 보내는거임
		WSASend(c_socket, &mybuf, 1, &sent_byte, 0, nullptr, nullptr);
		std::cout << "Server Sent [" << sent_byte << "bytes] : " << recv_buf << std::endl;
	}
	WSACleanup();
}
