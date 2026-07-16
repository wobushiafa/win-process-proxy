#include "TCPHandler.h"

namespace
{
	bool ReadUInt16(const vector<BYTE>& data, size_t offset, USHORT& value)
	{
		if (offset + 2 > data.size())
			return false;
		value = ((USHORT)data[offset] << 8) | data[offset + 1];
		return true;
	}

	string ParseTlsServerName(const vector<BYTE>& data)
	{
		USHORT recordLength = 0;
		if (data.size() < 9 || data[0] != 0x16 || data[5] != 0x01 ||
			!ReadUInt16(data, 3, recordLength) || data.size() < 5 + recordLength)
			return {};
		size_t recordEnd = 5 + recordLength;

		size_t offset = 9 + 2 + 32;
		if (offset >= recordEnd)
			return {};
		offset += 1 + data[offset];

		USHORT length = 0;
		if (offset + 2 > recordEnd || !ReadUInt16(data, offset, length))
			return {};
		offset += 2 + length;
		if (offset >= recordEnd)
			return {};
		offset += 1 + data[offset];

		USHORT extensionsLength = 0;
		if (offset + 2 > recordEnd || !ReadUInt16(data, offset, extensionsLength))
			return {};
		offset += 2;
		size_t extensionsEnd = offset + extensionsLength;
		if (extensionsEnd > recordEnd)
			return {};

		while (offset + 4 <= extensionsEnd)
		{
			USHORT type = 0;
			USHORT extensionLength = 0;
			ReadUInt16(data, offset, type);
			ReadUInt16(data, offset + 2, extensionLength);
			offset += 4;
			if (offset + extensionLength > extensionsEnd)
				return {};

			if (type == 0x0000 && extensionLength >= 5)
			{
				USHORT listLength = 0;
				if (!ReadUInt16(data, offset, listLength) || offset + 2 + listLength > extensionsEnd)
					return {};

				size_t nameOffset = offset + 2;
				size_t listEnd = nameOffset + listLength;
				while (nameOffset + 3 <= listEnd)
				{
					BYTE nameType = data[nameOffset];
					USHORT nameLength = 0;
					if (!ReadUInt16(data, nameOffset + 1, nameLength) || nameOffset + 3 + nameLength > listEnd)
						return {};
					if (nameType == 0 && nameLength != 0 && nameLength <= 253)
					{
						string host((const char*)data.data() + nameOffset + 3, nameLength);
						if (host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") == string::npos)
							return host;
					}
					nameOffset += 3 + nameLength;
				}
			}

			offset += extensionLength;
		}

		return {};
	}

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
			auto serverName = ParseTlsServerName(available);
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
}

SOCKET tcpSocket = INVALID_SOCKET;
USHORT tcpListen = 0;

mutex tcpLock;
map<USHORT, SOCKADDR_IN6> tcpContext;

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

void TCPHandler::CreateHandler(SOCKADDR_IN6 client, SOCKADDR_IN6 remote)
{
	auto lg = lock_guard<mutex>(tcpLock);

	auto id = (client.sin6_family == AF_INET) ? ((PSOCKADDR_IN)&client)->sin_port : client.sin6_port;
	if (tcpContext.find(id) != tcpContext.end())
		tcpContext.erase(id);

	tcpContext[id] = remote;
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

	auto target = tcpContext[id];
	tcpLock.unlock();

	auto remote = new SocksHelper::TCP();
	auto port = (target.sin6_family == AF_INET) ? ((PSOCKADDR_IN)&target)->sin_port : target.sin6_port;
	auto serverName = (ntohs(port) == 443) ? PeekTlsServerName(client) : string{};
	if (!serverName.empty())
		printf("[WinProcessProxy][TCPHandler::Handle] SOCKS5 domain target: %s:%u\n", serverName.c_str(), ntohs(port));
	bool connected = serverName.empty() ? remote->Connect(&target) : remote->Connect(serverName, port);
	if (!connected)
	{
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
