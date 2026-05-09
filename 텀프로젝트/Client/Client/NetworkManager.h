#pragma once

#include "Singleton.h"
#include <string>

class NetworkManager : public Singleton<NetworkManager>
{
	friend class Singleton<NetworkManager>;
public:
	bool Connect(const std::string& ip, int port);
	void Disconnect();
	
	bool send_packet(void* packet);

	void process_network();
	void process_packet(char* ptr);

private:
	SOCKET _socket = INVALID_SOCKET;
};
