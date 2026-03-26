#include <iostream>
#include <WS2tcpip.h>
#include <unordered_map>
#pragma comment(lib, "WS2_32.lib")
using namespace std;
constexpr int PORT_NUM = 3500;
constexpr int BUF_SIZE = 200;

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags);
void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags);

class EXP_OVER {
public:
	WSAOVERLAPPED	_send_over;
	long long		_id;
	WSABUF			_send_wsabuffer[1];
	char			_buff[BUF_SIZE];
	EXP_OVER(long long client_id, int num_bytes, char* mess) : _id(client_id)
	{
		ZeroMemory(&_send_over, sizeof(_send_over));
		_send_wsabuffer[0].buf = _buff;
		_send_wsabuffer[0].len = num_bytes + 2;
		_buff[0] = num_bytes + 2;
		_buff[1] = static_cast<char>(client_id);
		memcpy(_buff + 2, mess, num_bytes);
	}
};

class SESSION;
unordered_map<long long, SESSION> clients;
class SESSION {
	SOCKET			client;
	WSAOVERLAPPED	_recv_over;
	WSABUF			_recv_wsabuf[1];
	long long		_id;
public:
	CHAR c_mess[BUF_SIZE];
	SESSION() { exit(-1); }
	SESSION(int id, SOCKET so) : _id(id), client(so)
	{
		_recv_wsabuf[0].buf = c_mess;
	}
	~SESSION()
	{
		closesocket(client);
	}
	void do_recv()
	{
		_recv_wsabuf[0].len = BUF_SIZE;
		DWORD recv_flag = 0;
		memset(&_recv_over, 0, sizeof(_recv_over));
		_recv_over.hEvent = reinterpret_cast<HANDLE>(_id);
		WSARecv(client, _recv_wsabuf, 1, 0, &recv_flag, &_recv_over, recv_callback);
	}
	void do_send(int sender_id, int num_bytes, char* mess)
	{
		EXP_OVER* o = new EXP_OVER(sender_id, num_bytes, mess);
		WSASend(client, o->_send_wsabuffer, 1, 0, 0, &o->_send_over, send_callback);
	}
};

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags)
{
	int client_id = static_cast<int>(reinterpret_cast<long long>(over->hEvent));
	cout << "Client[" << client_id << "] sent: " << clients[client_id].c_mess << endl;
	for (auto& cl : clients)
		cl.second.do_send(client_id, num_bytes, clients[client_id].c_mess);
	clients[client_id].do_recv();
}

void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags)
{
	EXP_OVER* o = reinterpret_cast<EXP_OVER*>(over);
	delete over;
}

int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	SOCKET server = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_NUM);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	bind(server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(server, SOMAXCONN);
	SOCKADDR_IN cl_addr;
	int addr_size = sizeof(cl_addr);
	for (int i = 1; ; ++i) {
		SOCKET client = WSAAccept(server,
			reinterpret_cast<sockaddr*>(&cl_addr), &addr_size, NULL, NULL);
		clients.try_emplace(i, i, client);
		clients[i].do_recv();
	}
	closesocket(server);
	WSACleanup();
}
