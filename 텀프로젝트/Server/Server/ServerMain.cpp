#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <chrono>
#include <string>
#include <iostream>
#include <unordered_map>
#include <memory>

constexpr short SERVER_PORT = 3001;
constexpr int BUFFER_SIZE = 4096;
// Global state
constexpr int BOARD_SIZE = 8;

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
	struct vector2
	{
		int x;
		int y;
	} dir;
};
struct SCMovePacket
{
	uint32_t	size;
	PacketType	type;
	long long	client_id;
	struct vector2
	{
		int x;
		int y;
	} pos;
};
struct SCLoginAckPacket 
{
	uint32_t	size;
	PacketType	type;
	long long	client_id;
};
struct CSLoginPacket
{
	uint32_t	size;
	PacketType	type;
};
struct SCLogoutPacket
{
	uint32_t	size;
	PacketType	type;
	long long	client_id;
};
struct CSLogoutPacket
{
	uint32_t	size;
	PacketType	type;
};
struct SCSpawnPacket 
{
	uint32_t	size;
	PacketType	type;
	long long	client_id;
	struct vector2
	{
		int x;
		int y;
	} pos;
};
#pragma pack(pop)

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags);
void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags);

class EXP_OVER {
public:
	WSAOVERLAPPED	_over;
	long long		_id;
	WSABUF			_wsabuffer[1];
	char			_buf[BUFFER_SIZE];
	EXP_OVER(long long client_id, int num_bytes, char* data) : _id(client_id)
	{
		ZeroMemory(&_over, sizeof(_over));
		_wsabuffer[0].buf = _buf;
		_wsabuffer[0].len = num_bytes;
		memcpy(_buf, data, num_bytes);
	}
};

struct Position {
	int x;
	int y;
};

class SESSION {
public:
	SOCKET			_client;
	EXP_OVER		_recv_exp_over;
	long long		_id;
	Position		_pos;
	int				_recv_bytes;

	SESSION(int id, SOCKET so) : _client(so), _recv_exp_over(id, 0, nullptr), _id(id), _pos{ 0, 0 }, _recv_bytes(0)
	{
	}
	~SESSION()
	{
	}
	void do_recv()
	{
		_recv_exp_over._id = _id;
		_recv_exp_over._wsabuffer[0].buf = _recv_exp_over._buf + _recv_bytes; // 남은 데이터 뒤부터 이어 받음
		_recv_exp_over._wsabuffer[0].len = BUFFER_SIZE - _recv_bytes;
		DWORD recv_flag = 0;
		ZeroMemory(&_recv_exp_over._over, sizeof(WSAOVERLAPPED));
		WSARecv(_client, _recv_exp_over._wsabuffer, 1, nullptr, &recv_flag, &_recv_exp_over._over, recv_callback);
	}
	void do_send(int num_bytes, char* mess)
	{
		EXP_OVER* o = new EXP_OVER(_id, num_bytes, mess);
		WSASend(_client, o->_wsabuffer, 1, 0, 0, &o->_over, send_callback);
	}
};


std::unordered_map<long long, std::shared_ptr<SESSION>> clients;
void BroadCast(int num_bytes, char* mess)
{
	for (auto& [id, session] : clients) {
		session->do_send(num_bytes, mess);
	}
}

void DisconnectClient(long long id) {
	auto it = clients.find(id);
	if (it != clients.end()) {
		SOCKET s = it->second->_client;
		clients.erase(it);
		SCLogoutPacket logout_packet{};
		logout_packet.size = sizeof(SCLogoutPacket);
		logout_packet.type = PacketType::SC_Logout;
		logout_packet.client_id = id;
		BroadCast(sizeof(SCLogoutPacket), reinterpret_cast<char*>(&logout_packet));
		closesocket(s);
		std::cout << "[시스템] 클라이언트 접속 종료. ID: " << id << "\n";
	}
}



void ProcessPacket(std::shared_ptr<SESSION> session, char* buffer)
{
	PacketType type = *reinterpret_cast<PacketType*>(buffer + 4);
	switch (type)
	{
	case PacketType::CS_Move: // Move Packet
		{
			CSMovePacket* move_packet = reinterpret_cast<CSMovePacket*>(buffer);
			// 이동 처리
			Position newPos = { session->_pos.x + move_packet->dir.x, session->_pos.y + move_packet->dir.y };
			if (newPos.x < BOARD_SIZE && newPos.x > -1) session->_pos.x = newPos.x;
			if (newPos.y < BOARD_SIZE && newPos.y > -1) session->_pos.y = newPos.y;

			//std::cout << "Client[" << session->_id << "] Move: New Pos(" << session->_pos.x << ", " << session->_pos.y << ")\n";

			// 응답 패킷 만들기
			SCMovePacket send_packet{};
			send_packet.size = sizeof(SCMovePacket);
			send_packet.type = PacketType::SC_Move;
			send_packet.client_id = session->_id;
			send_packet.pos.x = session->_pos.x;
			send_packet.pos.y = session->_pos.y;
			BroadCast(sizeof(SCMovePacket), reinterpret_cast<char*>(&send_packet)); 
			break;
		}
	case PacketType::CS_Login:
		{
			//std::cout << "Received Login Packet from Client[" << session->_id << "]\n";
			session->_pos = { 0, 0 }; // 초기 위치 설정
			SCLoginAckPacket ack_packet{};
			ack_packet.size = sizeof(SCLoginAckPacket);
			ack_packet.type = PacketType::SC_LoginAck;
			ack_packet.client_id = session->_id;
			session->do_send(sizeof(SCLoginAckPacket), reinterpret_cast<char*>(&ack_packet));

			SCSpawnPacket spawn_packet{};
			spawn_packet.size = sizeof(SCSpawnPacket);
			spawn_packet.type = PacketType::SC_Spawn;
			spawn_packet.client_id = session->_id;
			spawn_packet.pos.x = session->_pos.x;
			spawn_packet.pos.y = session->_pos.y;
			BroadCast(sizeof(SCSpawnPacket), reinterpret_cast<char*>(&spawn_packet));

			for (auto& [id, client] : clients)
			{
				if (id == session->_id)
				{
					continue;
				}
				SCSpawnPacket other_spawn_packet{};
				other_spawn_packet.size = sizeof(SCSpawnPacket);
				other_spawn_packet.type = PacketType::SC_Spawn;
				other_spawn_packet.client_id = client->_id;
				other_spawn_packet.pos.x = client->_pos.x;
				other_spawn_packet.pos.y = client->_pos.y;
				session->do_send(sizeof(SCSpawnPacket), reinterpret_cast<char*>(&other_spawn_packet));
			}
			break;
		}
		case PacketType::CS_Logout:
		{
			//std::cout << "Received Logout Packet from Client[" << session->_id << "]\n";
			SCLogoutPacket logout_packet{};
			logout_packet.size = sizeof(SCLogoutPacket);
			logout_packet.type = PacketType::SC_Logout;
			logout_packet.client_id = session->_id;
			BroadCast(sizeof(SCLogoutPacket), reinterpret_cast<char*>(&logout_packet));
			DisconnectClient(session->_id);
			break;
		}
	}
	
}

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags)
{
	EXP_OVER* o = reinterpret_cast<EXP_OVER*>(over);
	long long client_id = o->_id;

	// 1. 맵에서 안전하게 shared_ptr 확보
	auto it = clients.find(client_id);
	if (it == clients.end()) return;
	std::shared_ptr<SESSION> client = it->second;

	if (err != 0 || num_bytes == 0) {
		DisconnectClient(client_id);
		return;
	}

	client->_recv_bytes += num_bytes;
	int read_pos = 0;

	while (true) {
		int remain_bytes = client->_recv_bytes - read_pos;
		if (remain_bytes < sizeof(uint32_t)) break;

		uint32_t packet_size = *reinterpret_cast<uint32_t*>(client->_recv_exp_over._buf + read_pos);

		if (remain_bytes >= packet_size) {
			ProcessPacket(client, client->_recv_exp_over._buf + read_pos);
			read_pos += packet_size;
			
			// 로그아웃 등으로 세션이 제거되었는지 확인
			if (!clients.contains(client_id)) return;
		}
		else {
			break;
		}
	}

	int left_over = client->_recv_bytes - read_pos;
	if (left_over > 0 && read_pos > 0) {
		memmove(client->_recv_exp_over._buf, client->_recv_exp_over._buf + read_pos, left_over);
	}
	client->_recv_bytes = left_over;

	client->do_recv(); 
}

void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags)
{
	EXP_OVER* o = reinterpret_cast<EXP_OVER*>(over);
	delete o;
}



void main()
{
	std::wcout.imbue(std::locale("korean"));
	WSADATA wsa_data{};
	WSAStartup(MAKEWORD(2, 2), &wsa_data);

	SOCKET server_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	bind(server_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(server_socket, SOMAXCONN);

	//std::cout << "Server started on port " << SERVER_PORT << std::endl;

	INT addr_len = sizeof(server_addr);
	SOCKADDR_IN cl_addr;
	
	for (int i = 1; ; ++i) {
		// WSAAccept를 사용하여 콜백이 실행될 수 있는 Alertable Wait 상태를 중간중간 만듦
		// 실제 상용 서버에서는 Accept 전전용 스레드를 두거나 AcceptEx를 사용함
		SOCKET client = WSAAccept(server_socket, reinterpret_cast<sockaddr*>(&cl_addr), &addr_len, NULL, NULL);
		
		if (client != INVALID_SOCKET) {
			auto session = std::make_shared<SESSION>(i, client);
			clients[i] = session;
			session->do_recv();
		}

		// 콜백들이 실행될 기회를 주기 위해 0ms Alertable Wait
		SleepEx(0, TRUE);
	}
	WSACleanup();
}