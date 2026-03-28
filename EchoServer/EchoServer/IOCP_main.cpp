#include <iostream>
#include <WS2tcpip.h>
#include <unordered_map>
#pragma comment(lib, "WS2_32.lib")
#include <MSWSock.h>
#pragma comment(lib, "MSWSock.lib")

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
using namespace std;
constexpr int SERVER_PORT = 3500;
constexpr int BUF_SIZE = 200;

enum class io_type
{
	send,
	recv,
	accept,
	count
};


void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags);
void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags);

class EXP_OVER {
public:
	WSAOVERLAPPED	_over;
	io_type			_type = io_type::count;
	WSABUF			_wsabuffer;
	char			_buff[BUF_SIZE];
	EXP_OVER()
	{
		ZeroMemory(&_over, sizeof(_over));
		_wsabuffer.buf = _buff;
		_wsabuffer.len = BUF_SIZE;
	}
	EXP_OVER(io_type type, int num_bytes, char* mess) : _type{type}
	{
		ZeroMemory(&_over, sizeof(_over));
		//memcpy(_buff, mess, num_bytes);
		_wsabuffer.buf = _buff;
		_wsabuffer.len = num_bytes + 2;
	}
};

class SESSION;
unordered_map<long long, SESSION> clients;
class SESSION {
	SOCKET			_client;
	long long		_id;
public:
	EXP_OVER		_recv_over;
	SESSION() { exit(-1); }
	SESSION(int id, SOCKET so) : _id(id), _client(so)
	{
		// 객체 대입을 하지 말고 직접 초기화하세요.
		ZeroMemory(&_recv_over._over, sizeof(WSAOVERLAPPED));
		_recv_over._type = io_type::recv;
		_recv_over._wsabuffer.buf = _recv_over._buff; // 본인의 버퍼 주소로 재설정
		_recv_over._wsabuffer.len = BUF_SIZE;
	}
	~SESSION()
	{
		closesocket(_client);
	}
	void do_recv()
	{
		DWORD recv_flag = 0;
		_recv_over._over = {};
		_recv_over._type = io_type::recv;
		_recv_over._wsabuffer.buf = _recv_over._buff; // 안전하게 여기서 다시 잡아줌
		WSARecv(_client, &_recv_over._wsabuffer, 1, 0, &recv_flag, &_recv_over._over, nullptr);
	}
	void do_send(int sender_id, int num_bytes, char* mess)
	{
		EXP_OVER* o = new EXP_OVER(io_type::send, num_bytes, mess);
		o->_buff[0] = num_bytes + 2;
		o->_buff[1] = static_cast<char>(sender_id);
		memcpy(o->_buff + 2, mess, num_bytes);
		WSASend(_client, &o->_wsabuffer, 1, 0, 0, &o->_over, nullptr);
	}
};

int main()
{
	wcout.imbue(locale("korean"));
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	SOCKET server = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;

	bind(server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));

	listen(server, SOMAXCONN);

	HANDLE h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	CreateIoCompletionPort((HANDLE)server, h_iocp, 0, 0);

	SOCKADDR_IN cl_addr;
	SOCKET client = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	EXP_OVER accept_over(io_type::accept, 0, nullptr);
	
	int i = 1;
	CreateIoCompletionPort((HANDLE)client, h_iocp, i, 0);
	AcceptEx(server, client, &accept_over._buff, 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, NULL, &accept_over._over);
	while (true)
	{
		DWORD bytes_transferred;
		ULONG_PTR key;
		LPOVERLAPPED over;
		GetQueuedCompletionStatus(h_iocp, &bytes_transferred, &key, &over, INFINITE);
		if (over == nullptr)
		{
			error_display(L"GetQueuedCompletionStatus Error", WSAGetLastError());
			continue;
		}
		EXP_OVER* o = reinterpret_cast<EXP_OVER*>(over);
		switch (o->_type)
		{
			case io_type::accept:
			{
				std::cout << "New Client Connected!" << std::endl;
				CreateIoCompletionPort((HANDLE)client, h_iocp, i, 0);
				clients.try_emplace(i, i, client );
				clients[i].do_recv();
				i++;
				client = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
				
				accept_over = EXP_OVER(io_type::accept, 0, nullptr);
				AcceptEx(server, client, &accept_over._buff, 0,
					sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
					NULL, &accept_over._over);
				break;
			}
			case io_type::recv:
			{
				int client_id = static_cast<int>(key);
				// 0바이트를 받았다면 클라이언트가 접속을 종료한 것임
				if (bytes_transferred == 0) {
					cout << "Client[" << client_id << "] Disconnected" << endl;
					clients.erase(client_id); // 소켓은 소멸자에서 닫힘
					break;
				}

				// 수신된 데이터에 널 문자를 넣어 안전하게 출력
				clients[client_id]._recv_over._buff[bytes_transferred] = '\0';
				cout << "Client[" << client_id << "] sent: " << clients[client_id]._recv_over._buff << endl;
				for (auto& cl : clients)
					cl.second.do_send(client_id, static_cast<int>(bytes_transferred), clients[client_id]._recv_over._buff);
				clients[client_id].do_recv();
				break;
			}
			case io_type::send:
			{
				delete o;
				break;
			}
		}
	}
	closesocket(server);
	WSACleanup();
}
