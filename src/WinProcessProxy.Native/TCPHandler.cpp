#include "TCPHandler.h"
#include "EventHandler.h"
#include "TlsClientHello.h"
#include "Utils.h"

namespace
{
	string PeekTlsServerName(SOCKET client)
	{
		auto deadline = chrono::steady_clock::now() + chrono::milliseconds(350);
		vector<BYTE> data(65536);

		while (chrono::steady_clock::now() < deadline)
		{
			fd_set sockets;
			FD_ZERO(&sockets);
			FD_SET(client, &sockets);
			timeval timeout{ 0, 50000 };
			int selected = select(0, &sockets, NULL, NULL, &timeout);
			if (selected == SOCKET_ERROR)
				return {};
			if (selected == 0)
				continue;

			int length = recv(client, (char*)data.data(), (int)data.size(), MSG_PEEK);
			if (length == 0 || length == SOCKET_ERROR)
				return {};
			vector<BYTE> available(data.begin(), data.begin() + length);
			auto serverName = TlsClientHello::ParseServerName(available);
			if (!serverName.empty())
				return serverName;

			if (length >= 5 && data[0] == 0x16)
			{
				USHORT recordLength = ((USHORT)data[3] << 8) | data[4];
				if (length >= 5 + recordLength)
					return {};
			}
			else
			{
				return {};
			}

			this_thread::sleep_for(chrono::milliseconds(5));
		}

		return {};
	}

	bool SendAll(SOCKET socket, const char* buffer, int length)
	{
		int sent = 0;
		while (sent < length)
		{
			int result = send(socket, buffer + sent, length - sent, 0);
			if (result == 0 || result == SOCKET_ERROR)
				return false;
			sent += result;
		}
		return true;
	}

	wstring FormatAddress(PSOCKADDR_IN6 address)
	{
		WCHAR buffer[MAX_PATH] = L"";
		DWORD length = MAX_PATH;
		DWORD addressLength = address->sin6_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6);
		if (WSAAddressToStringW((PSOCKADDR)address, addressLength, nullptr, buffer, &length) == 0)
			return buffer;
		return L"unknown";
	}
}

SOCKET tcpSocket = INVALID_SOCKET;
USHORT tcpListen = 0;

mutex tcpLock;
struct TCP_CONTEXT
{
	SOCKADDR_IN6 target;
	DWORD processId;
};
map<USHORT, TCP_CONTEXT> tcpContext;

bool TCPHandler::INIT()
{
	auto lg = lock_guard<mutex>(tcpLock);

	if (tcpSocket != INVALID_SOCKET)
	{
		closesocket(tcpSocket);

		tcpSocket = INVALID_SOCKET;
	}

	auto client = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (client == INVALID_SOCKET)
	{
		printf("[WinProcessProxy][TCPHandler::INIT] Create socket failed: %d\n", WSAGetLastError());
		return false;
	}

	{
		int v6only = 0;
		if (setsockopt(client, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&v6only, sizeof(v6only)) == SOCKET_ERROR)
		{
			printf("[WinProcessProxy][TCPHandler::INIT] Set socket option failed: %d\n", WSAGetLastError());

			closesocket(client);
			return false;
		}
	}

	{
		SOCKADDR_IN6 addr;
		IN6ADDR_SETANY(&addr);

		if (bind(client, (PSOCKADDR)&addr, sizeof(SOCKADDR_IN6)) == SOCKET_ERROR)
		{
			printf("[WinProcessProxy][TCPHandler::INIT] Bind socket failed: %d\n", WSAGetLastError());

			closesocket(client);
			return false;
		}
	}
	
	if (listen(client, 1024) == SOCKET_ERROR)
	{
		printf("[WinProcessProxy][TCPHandler::INIT] Listen socket failed: %d\n", WSAGetLastError());

		closesocket(client);
		return false;
	}

	{
		SOCKADDR_IN6 addr;
		int addrLength = sizeof(SOCKADDR_IN6);
		if (getsockname(client, (PSOCKADDR)&addr, &addrLength) == SOCKET_ERROR)
		{
			printf("[WinProcessProxy][TCPHandler::INIT] Get listen address failed: %d\n", WSAGetLastError());

			closesocket(client);
			return false;
		}

		tcpListen = (addr.sin6_family == AF_INET6) ? addr.sin6_port : ((PSOCKADDR_IN)&addr)->sin_port;
	}

	tcpSocket = client;

	thread(TCPHandler::Accept).detach();
	return true;
}

void TCPHandler::FREE()
{
	auto lg = lock_guard<mutex>(tcpLock);

	if (tcpSocket != INVALID_SOCKET)
	{
		closesocket(tcpSocket);

		tcpSocket = INVALID_SOCKET;
	}
	tcpListen = 0;

	tcpContext.clear();

}

void TCPHandler::CreateHandler(SOCKADDR_IN6 client, SOCKADDR_IN6 remote, DWORD processId)
{
	auto lg = lock_guard<mutex>(tcpLock);

	auto id = (client.sin6_family == AF_INET) ? ((PSOCKADDR_IN)&client)->sin_port : client.sin6_port;
	if (tcpContext.find(id) != tcpContext.end())
		tcpContext.erase(id);

	tcpContext[id] = { remote, processId };
}

void TCPHandler::DeleteHandler(SOCKADDR_IN6 client)
{
	auto lg = lock_guard<mutex>(tcpLock);

	auto id = (client.sin6_family == AF_INET) ? ((PSOCKADDR_IN)&client)->sin_port : client.sin6_port;
	if (tcpContext.find(id) != tcpContext.end())
		tcpContext.erase(id);
}

void TCPHandler::Accept()
{
	while (tcpSocket != INVALID_SOCKET)
	{
		auto client = accept(tcpSocket, NULL, NULL);
		if (client == INVALID_SOCKET)
		{
			int lasterr = WSAGetLastError();
			if (lasterr == 10004)
				return;

			printf("[WinProcessProxy][TCPHandler::Accept] Accept client failed: %d\n", lasterr);
			return;
		}

		thread(TCPHandler::Handle, client).detach();
	}
}

void TCPHandler::Handle(SOCKET client)
{
	USHORT id = 0;

	{
		SOCKADDR_IN6 addr;
		int addrLength = sizeof(SOCKADDR_IN6);

		if (getpeername(client, (PSOCKADDR)&addr, &addrLength) == SOCKET_ERROR)
		{
			closesocket(client);
			return;
		}

		id = (addr.sin6_family == AF_INET) ? ((PSOCKADDR_IN)&addr)->sin_port : addr.sin6_port;
	}

	tcpLock.lock();
	if (tcpContext.find(id) == tcpContext.end())
	{
		tcpLock.unlock();

		closesocket(client);
		return;
	}

	auto context = tcpContext[id];
	tcpLock.unlock();
	auto target = context.target;

	auto remote = new SocksHelper::TCP();
	auto port = (target.sin6_family == AF_INET) ? ((PSOCKADDR_IN)&target)->sin_port : target.sin6_port;
	auto serverName = (ntohs(port) == 443) ? PeekTlsServerName(client) : string{};
	if (!serverName.empty())
	{
		LogProxyEvent(
			AIO_LOG_TCP_DOMAIN,
			context.processId,
			s2ws(serverName) + L":" + to_wstring(ntohs(port)));
	}
	else
	{
		auto reason = ntohs(port) == 443 ? L" reason=no-sni" : L" reason=non-tls-port";
		LogProxyEvent(
			AIO_LOG_TCP_IP_FALLBACK,
			context.processId,
			FormatAddress(&target) + reason);
	}
	bool connected = serverName.empty() ? remote->Connect(&target) : remote->Connect(serverName, port);
	if (!connected)
	{
		if (!serverName.empty())
		{
			LogProxyEvent(
				AIO_LOG_TCP_DOMAIN_FAIL,
				context.processId,
				s2ws(serverName) + L":" + to_wstring(ntohs(port)));
		}
		closesocket(client);
		delete remote;
		return;
	}

	thread sender(TCPHandler::Send, client, remote);
	TCPHandler::Read(client, remote);
	shutdown(client, SD_BOTH);
	sender.join();

	closesocket(client);
	delete remote;
}

void TCPHandler::Read(SOCKET client, SocksHelper::PTCP remote)
{
	char buffer[1446];
	
	while (tcpSocket != INVALID_SOCKET)
	{
		int length = remote->Read(buffer, sizeof(buffer));
		if (length == 0 || length == SOCKET_ERROR)
			return;

		if (!SendAll(client, buffer, length))
			return;
	}
}

void TCPHandler::Send(SOCKET client, SocksHelper::PTCP remote)
{
	char buffer[1446];

	while (tcpSocket != INVALID_SOCKET)
	{
		int length = recv(client, buffer, sizeof(buffer), 0);
		if (length == 0 || length == SOCKET_ERROR)
			return;

		if (remote->Send(buffer, length) != length)
			return;
	}
}
