#include "pch.h"
#include "NetworkManager.h"

#include "common.h"
#include "GameManager.h"
#include "Object.h"

void error_display(const std::wstring& msg, int err_no) {
	WCHAR* lpMsgBuf;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
	std::wcout << msg << L"=== 에러: " << lpMsgBuf << std::endl;
	LocalFree(lpMsgBuf);
}

bool NetworkManager::Connect(const std::string& ip, int port)
{
	WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
	_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);

	// Set non-blocking
	unsigned long arg = 1;
	ioctlsocket(_socket, FIONBIO, &arg);

	SOCKADDR_IN addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

	connect(_socket, (sockaddr*)&addr, sizeof(addr));

	C2S_Login login_pkt;
	login_pkt.size = sizeof(login_pkt);
	login_pkt.type = C2S_LOGIN;
	strcpy_s(login_pkt.username, GameManager::Instance()->username().c_str());
	
	send_packet(&login_pkt);

	return true;
}

void NetworkManager::Disconnect()
{
	if (_socket != INVALID_SOCKET) {
		closesocket(_socket);
		_socket = INVALID_SOCKET;
	}
}

bool NetworkManager::send_packet(void* packet)
{
	unsigned char size = *reinterpret_cast<unsigned char*>(packet);
	WSABUF buf{ (ULONG)size, (char*)packet };
	DWORD sent;
	if (WSASend(_socket, &buf, 1, &sent, 0, nullptr, nullptr) == SOCKET_ERROR) {
		if (WSAGetLastError() != WSAEWOULDBLOCK) {
			GameManager::Instance()->set_running(false);
			return false;
		}
	}
	return true;
}

void NetworkManager::process_network()
{
	static char recv_buf[1024];
	static int curr_size = 0;

	fd_set read_set;
	FD_ZERO(&read_set);
	FD_SET(_socket, &read_set);
	timeval tv{ 0, 0 };

	if (select(0, &read_set, nullptr, nullptr, &tv) > 0) {
		int ret = recv(_socket, recv_buf + curr_size, 1024 - curr_size, 0);
		if (ret == 0) {
			GameManager::Instance()->set_running(false);
			return;
		}
		if (ret == SOCKET_ERROR) {
			if (WSAGetLastError() != WSAEWOULDBLOCK) GameManager::Instance()->set_running(false);
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

void NetworkManager::process_packet(char* ptr)
{
	PACKET_TYPE type = *reinterpret_cast<PACKET_TYPE*>(ptr + 1);
	auto gm = GameManager::Instance();

	switch (type) {
	case PACKET_TYPE::S2C_LOGIN_RESULT: {
		S2C_LoginResult* p = reinterpret_cast<S2C_LoginResult*>(ptr);
		if (p->success) {
			std::cout << "Login successful. Message: " << p->message << "\n";
		}
		else {
			std::cout << "Login failed. Message: " << p->message << "\n";
			gm->set_running(false);
		}
		break;
	}
	case S2C_AVATAR_INFO: {
		S2C_AvatarInfo* p = reinterpret_cast<S2C_AvatarInfo*>(ptr);
		gm->set_my_id(p->playerId);
		gm->players()[p->playerId] = { p->playerId, gm->username(), p->x, p->y };
		std::cout << "Avatar Info: ID=" << p->playerId << " at (" << p->x << ", " << p->y << ")\n";
		break;
	}
	case S2C_ADD_PLAYER: {
		S2C_AddPlayer* p = reinterpret_cast<S2C_AddPlayer*>(ptr);
		if (is_npc_id(p->playerId))
		{
			gm->npcs().emplace(p->playerId, Object{ p->playerId, p->username, p->x, p->y });
		}
		else if (p->playerId == gm->my_id()) break;
		else {
			gm->players().emplace(p->playerId, Object{ p->playerId, p->username, p->x, p->y });
			std::cout << "Add Player: ID=" << p->playerId << ", Name=" << p->username << " at (" << p->x << ", " << p->y << ")\n";
		}
		break;
	}
	case S2C_MOVE_PLAYER: {
		S2C_MovePlayer* p = reinterpret_cast<S2C_MovePlayer*>(ptr);
		if (is_npc_id(p->playerId))
		{
			if (gm->npcs().contains(p->playerId)) {
				gm->npcs()[p->playerId].x = p->x;
				gm->npcs()[p->playerId].y = p->y;
			}
		}
		else if (gm->players().contains(p->playerId)) {
			gm->players()[p->playerId].x = p->x;
			gm->players()[p->playerId].y = p->y;
		}
		break;
	}
	case S2C_REMOVE_PLAYER: {
		S2C_RemovePlayer* p = reinterpret_cast<S2C_RemovePlayer*>(ptr);
		if (is_npc_id(p->playerId))
		{
			if (gm->npcs().contains(p->playerId)) {
				gm->npcs().erase(p->playerId);
				std::cout << "Remove NPC: ID=" << p->playerId << "\n";
			}
		}
		else if (!gm->players().contains(p->playerId)) break;
		else
		{
			gm->players().erase(p->playerId);
			std::cout << "Remove Player: ID=" << p->playerId << "\n";
		}
		break;
	}
	default:
		std::cout << "Unknown packet type: " << (int)type << "\n";
		__debugbreak();
		break;
	}
}
