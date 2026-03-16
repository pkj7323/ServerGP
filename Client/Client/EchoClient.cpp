#include <iostream>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

constexpr char SERVER_IP[] = "127.0.0.1";
constexpr int SERVER_PORT = 9001;

constexpr int BUFFER_SIZE = 4096;

int main()
{
	std::wcout.imbue(std::locale("korean"));
	WSADATA wsa_data{};
	WSAStartup(MAKEWORD(2, 2), &wsa_data); //마이크로소프트 네트워크사용할거면 초기화하고 해라 - 빌게이츠 <- 야이 새끼야!

	SOCKET s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);
	SOCKADDR_IN server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

	WSAConnect(s_socket, reinterpret_cast<sockaddr*>(&server_addr), 
		sizeof(server_addr), nullptr, nullptr, nullptr, nullptr);

	for (;;)
	{
		char input[BUFFER_SIZE];
		std::cout << "Enter message to send ";
		std::cin.getline(input, BUFFER_SIZE);
		WSABUF wsa_buf{ (ULONG)(strlen(input) + 1), input };
		DWORD sent_size = 0;
		WSASend(s_socket, &wsa_buf, 1, &sent_size, 0, nullptr, nullptr);

		char recv_buffer[BUFFER_SIZE];
		WSABUF recv_BUF{BUFFER_SIZE, recv_buffer};
		DWORD recv_size = 0;
		DWORD recv_flag = 0;
		WSARecv(s_socket, &recv_BUF, 1, &recv_size, &recv_flag, nullptr, nullptr);
	}

}