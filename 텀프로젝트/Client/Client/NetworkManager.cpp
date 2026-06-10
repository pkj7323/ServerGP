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

void NetworkManager::Release()
{
	if (_socket != INVALID_SOCKET) {
		closesocket(_socket);
		_socket = INVALID_SOCKET;
	}
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
	strcpy_s(login_pkt.user_id, GameManager::Instance()->user_id().c_str());
	
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
		gm->players()[p->playerId] = { p->playerId, gm->username(), p->x, p->y, (float)p->x, (float)p->y, p->armor_tier, p->weapon_tier, p->dir_x, p->dir_y, 0, p->hp, p->max_hp };
		std::cout << "Avatar Info: ID=" << p->playerId << " at (" << p->x << ", " << p->y << "), ArmorTier=" << (int)p->armor_tier << "\n";
		break;
	}
	case S2C_ADD_OBJECT: {
		S2C_AddObject* p = reinterpret_cast<S2C_AddObject*>(ptr);
		if (is_npc_id(p->object_id))
		{
			gm->npcs().emplace(p->object_id, Object{ p->object_id, p->obj_name, p->x, p->y, (float)p->x, (float)p->y, p->armor_tier, p->weapon_tier, p->dir_x, p->dir_y, p->visual_id, p->hp, p->max_hp, L"", std::chrono::steady_clock::now(), p->npc_state });
		}
		else if (p->object_id == gm->my_id()) break;
		else {
			gm->players().emplace(p->object_id, Object{ p->object_id, p->obj_name, p->x, p->y, (float)p->x, (float)p->y, p->armor_tier, p->weapon_tier, p->dir_x, p->dir_y, p->visual_id, p->hp, p->max_hp });
			std::cout << "Add Player: ID=" << p->object_id << ", Name=" << p->obj_name << " at (" << p->x << ", " << p->y << "), ArmorTier=" << (int)p->armor_tier << "\n";
		}
		break;
	}
	case S2C_MOVE_OBJECT: {
		S2C_MoveObject* p = reinterpret_cast<S2C_MoveObject*>(ptr);
		if (is_npc_id(p->object_id))
		{
			if (gm->npcs().contains(p->object_id)) {
				gm->npcs()[p->object_id].x = p->x;
				gm->npcs()[p->object_id].y = p->y;
				gm->npcs()[p->object_id].dir_x = p->dir_x;
				gm->npcs()[p->object_id].dir_y = p->dir_y;
			}
		}
		else if (gm->players().contains(p->object_id)) {
			// 고무줄(서버 강제 동기화) 처리: 만약 나라면 render_x/y 도 순간이동(서버가 거절한 이동이므로)
			if (p->object_id == gm->my_id()) {
				float distSq = static_cast<float>(gm->players()[p->object_id].x - p->x)*(gm->players()[p->object_id].x - p->x) + (gm->players()[p->object_id].y - p->y)*(gm->players()[p->object_id].y - p->y);
				if (distSq > 1.0f) {
					gm->players()[p->object_id].render_x = static_cast<float>(p->x);
					gm->players()[p->object_id].render_y = static_cast<float>(p->y);
				}
			}
			
			gm->players()[p->object_id].x = p->x;
			gm->players()[p->object_id].y = p->y;
			gm->players()[p->object_id].dir_x = p->dir_x;
			gm->players()[p->object_id].dir_y = p->dir_y;
		}
		break;
	}
	case S2C_ATTACK_EFFECT: {
		S2C_AttackEffect* p = reinterpret_cast<S2C_AttackEffect*>(ptr);
		gm->attack_effects().push_back({ p->object_id, p->weapon_tier, p->attack_type, p->x, p->y, p->dir_x, p->dir_y, std::chrono::steady_clock::now() });
		break;
	}
	case S2C_CHAT_MESSAGE: {
		S2C_ChatMessage* p = reinterpret_cast<S2C_ChatMessage*>(ptr);
		std::wstring sender = L"Unknown";

		std::string msg_str = p->message;
		int wlen = MultiByteToWideChar(CP_ACP, 0, msg_str.c_str(), -1, NULL, 0);
		std::wstring wmsg(wlen, 0);
		MultiByteToWideChar(CP_ACP, 0, msg_str.c_str(), -1, &wmsg[0], wlen);
		if (!wmsg.empty() && wmsg.back() == L'\0') wmsg.pop_back();

		// In-game Chat Bubble Logic
		if (is_npc_id(p->object_id) && gm->npcs().contains(p->object_id)) {
			std::string s_name = gm->npcs()[p->object_id].name;
			int n_len = MultiByteToWideChar(CP_ACP, 0, s_name.c_str(), -1, NULL, 0);
			sender.assign(n_len, 0);
			MultiByteToWideChar(CP_ACP, 0, s_name.c_str(), -1, &sender[0], n_len);
			if (!sender.empty() && sender.back() == L'\0') sender.pop_back();

			gm->npcs()[p->object_id].chat_msg = wmsg;
			gm->npcs()[p->object_id].chat_time = std::chrono::steady_clock::now();
		}
		else if (gm->players().contains(p->object_id)) {
			std::string s_name = gm->players()[p->object_id].name;
			int n_len = MultiByteToWideChar(CP_ACP, 0, s_name.c_str(), -1, NULL, 0);
			sender.assign(n_len, 0);
			MultiByteToWideChar(CP_ACP, 0, s_name.c_str(), -1, &sender[0], n_len);
			if (!sender.empty() && sender.back() == L'\0') sender.pop_back();

			gm->players()[p->object_id].chat_msg = wmsg;
			gm->players()[p->object_id].chat_time = std::chrono::steady_clock::now();
		}

		std::wstring logMsg = L"[" + sender + L"] " + wmsg;
		gm->add_chat_log(logMsg);
		break;
	}
	case S2C_REMOVE_OBJECT: {
		S2C_RemoveObject* p = reinterpret_cast<S2C_RemoveObject*>(ptr);
		if (is_npc_id(p->object_id))
		{
			if (gm->npcs().contains(p->object_id)) {
				gm->npcs().erase(p->object_id);
				std::cout << "Remove NPC: ID=" << p->object_id << "\n";
			}
		}
		else if (!gm->players().contains(p->object_id)) break;
		else
		{
			gm->players().erase(p->object_id);
			std::cout << "Remove Player: ID=" << p->object_id << "\n";
		}
		break;
	}
	case S2C_STATUS_CHANGE: {
		S2C_StatusChange* p = reinterpret_cast<S2C_StatusChange*>(ptr);
		if (gm->players().contains(p->object_id)) {
			gm->players()[p->object_id].armor_tier = p->armor_tier;
			gm->players()[p->object_id].weapon_tier = p->weapon_tier;
			gm->players()[p->object_id].hp = p->hp;
			gm->players()[p->object_id].max_hp = p->max_hp;
		}
		else if (gm->npcs().contains(p->object_id)) {
			gm->npcs()[p->object_id].armor_tier = p->armor_tier;
			gm->npcs()[p->object_id].weapon_tier = p->weapon_tier;
			gm->npcs()[p->object_id].hp = p->hp;
			gm->npcs()[p->object_id].max_hp = p->max_hp;
		}
		break;
	}
	case S2C_INVENTORY_SYNC:
	{
		S2C_InventorySync* p = reinterpret_cast<S2C_InventorySync*>(ptr);
		gm->set_my_gold(p->gold);
		gm->my_inventory().clear();
		for (int i = 0; i < p->item_count; ++i) {
			gm->my_inventory()[p->items[i].item_id] = p->items[i].count;
		}
		std::cout << "Inventory Sync: Gold=" << p->gold << " Items=" << p->item_count << "\n";
		break;
	}
	case S2C_NPC_STATE_CHANGE:
	{
		S2C_NpcStateChange* p = reinterpret_cast<S2C_NpcStateChange*>(ptr);
		auto& npcs = gm->npcs();
		auto it = npcs.find(p->object_id);
		if (it != npcs.end()) {
			it->second.npc_state = p->npc_state;
		}
		break;
	}
	default:
		std::cout << "Unknown packet type: " << (int)type << "\n";
		__debugbreak();
		break;
	}
}