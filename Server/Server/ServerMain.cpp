#include <ws2tcpip.h>
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

SOCKET g_c_socket = INVALID_SOCKET;
void main()
{
	std::wcout.imbue(std::locale("korean"));
	WSADATA wsa_data{};
	WSAStartup(MAKEWORD(2, 2), &wsa_data);

	SOCKET a_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);
	SOCKADDR_IN server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	int ret = bind(a_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	if (ret == SOCKET_ERROR)
	{
		error_display(L"소켓 바인드 실패", WSAGetLastError());
	}
	ret = listen(a_socket, SOMAXCONN);
	if (ret == SOCKET_ERROR)
	{
		error_display(L"리스닝 실패", WSAGetLastError());
	}

	INT addr_len = sizeof(server_addr);
	g_c_socket = WSAAccept(a_socket, reinterpret_cast<sockaddr*>(&server_addr),
		&addr_len, nullptr, 0);
	while (true)
	{
		char recv_buf[BUFFER_SIZE];
		WSABUF mybuf{ BUFFER_SIZE, recv_buf };
		DWORD recv_byte = 0; // 몇 바이트가 recv되는지 리턴되는 변수
		DWORD recv_flag = 0; // recv_flag는 recv할 때 옵션을 줄 수 있는 변수인데 지금은 옵션이 없으니까 0으로 초기화 해주면 됨
		WSARecv(g_c_socket, &mybuf, 1, &recv_byte,
			&recv_flag, nullptr, nullptr);

		CSMovePacket move_packet;
		memcpy(&move_packet, mybuf.buf, recv_byte);
		Position newPos = { playerPos.x + move_packet.dir.x, playerPos.y + move_packet.dir.y };

		std::cout << "Received Move Packet: Direction (" << move_packet.dir.x << ", " << move_packet.dir.y << ")\n";

		if (newPos.x < BOARD_SIZE && newPos.x > -1)
		{
			playerPos.x = newPos.x;
		}
		if (newPos.y < BOARD_SIZE && newPos.y > -1)
		{
			playerPos.y = newPos.y;
		}

		SCMovePacket scMovePacket{};
		scMovePacket.size = sizeof(SCMovePacket);
		scMovePacket.type = 2; // type 2는 move 패킷이야 라는 뜻
		scMovePacket.pos.x = playerPos.x;
		scMovePacket.pos.y = playerPos.y;

		char send_buf[BUFFER_SIZE];
		memcpy(send_buf, &scMovePacket, sizeof(SCMovePacket));
		WSABUF send_wsa_buffer{ sizeof(SCMovePacket), send_buf };

		DWORD sent_byte; // 몇 바이트가 send가 되는지 리턴되는 변수
		WSASend(g_c_socket, &send_wsa_buffer, 1, &sent_byte, 0, nullptr, nullptr);
		std::cout << "Server Sent [" << sent_byte << "bytes]" << " : Player Position (" << playerPos.x << ", " << playerPos.y << ")\n";
	}
	WSACleanup();
}