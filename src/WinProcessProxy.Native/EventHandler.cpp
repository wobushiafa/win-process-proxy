#include "EventHandler.h"

#include "DNSHandler.h"
#include "TCPHandler.h"

extern bool filterParent;
extern bool filterTCP;
extern bool filterUDP;
extern bool filterDNS;
extern bool dnsOnly;

extern vector<wstring> bypassList;
extern vector<wstring> handleList;

extern USHORT tcpListen;
extern void(__cdecl* logCallback)(int eventType, DWORD processId, LPCWSTR message);

DWORD CurrentID = 0;

mutex udpContextLock;
map<ENDPOINT_ID, shared_ptr<SocksHelper::UDP>> udpContext;
map<ENDPOINT_ID, DWORD> udpProcessIds;

mutex proxyLogLock;
set<wstring> proxiedApplications;
set<ENDPOINT_ID> proxiedTcpEndpoints;

atomic_ullong UP = { 0 };
atomic_ullong DL = { 0 };

wstring ConvertIP(PSOCKADDR addr)
{
	WCHAR buffer[MAX_PATH] = L"";
	DWORD bufferLength = MAX_PATH;
	DWORD length = (addr->sa_family == AF_INET) ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6);
	WSAAddressToStringW(addr, length, nullptr, buffer, &bufferLength);
	return buffer;
}

wstring GetProcessName(DWORD id)
{
	if (id == 0)
	{
		return L"Idle";
	}

	if (id == 4)
	{
		return L"System";
	}

	wchar_t name[MAX_PATH];
	if (!nf_getProcessNameFromKernel(id, name, MAX_PATH))
	{
		if (!nf_getProcessNameW(id, name, MAX_PATH))
		{
			return L"Unknown";
		}
	}

	wchar_t data[MAX_PATH];
	if (GetLongPathNameW(name, data, MAX_PATH))
	{
		return data;
	}

	return name;
}

void LogProxyEvent(AIO_LOG_EVENT eventType, DWORD processId, const wstring& message)
{
	if (logCallback == nullptr)
		return;
	logCallback(eventType, processId, message.c_str());
}

void LogProxiedApplication(DWORD processId)
{
	auto processName = GetProcessName(processId);
	lock_guard<mutex> lock(proxyLogLock);
	if (proxiedApplications.emplace(processName).second)
		LogProxyEvent(AIO_LOG_APPLICATION, processId, processName);
}

bool checkBypassName(DWORD id)
{
	auto name = GetProcessName(id);

	for (size_t i = 0; i < bypassList.size(); i++)
	{
		if (regex_search(name, wregex(bypassList[i], regex_constants::icase)))
		{
			return true;
		}
	}

	return false;
}

bool checkHandleName(DWORD id)
{
	{
		auto name = GetProcessName(id);

		for (size_t i = 0; i < handleList.size(); i++)
		{
			if (regex_search(name, wregex(handleList[i], regex_constants::icase)))
			{
				return true;
			}
		}
	}

	if (filterParent)
	{
		PROCESSENTRY32W PE;
		memset(&PE, 0, sizeof(PROCESSENTRY32W));
		PE.dwSize = sizeof(PROCESSENTRY32W);

		auto hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (hSnapshot == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		if (!Process32FirstW(hSnapshot, &PE))
		{
			CloseHandle(hSnapshot);
			return false;
		}

		do {
			if (PE.th32ProcessID == id)
			{
				auto name = GetProcessName(PE.th32ParentProcessID);

				for (size_t i = 0; i < handleList.size(); i++)
				{
					if (regex_search(name, wregex(handleList[i], regex_constants::icase)))
					{
						CloseHandle(hSnapshot);
						return true;
					}
				}
			}
		} while (Process32NextW(hSnapshot, &PE));

		CloseHandle(hSnapshot);
	}

	return false;
}

bool eh_init()
{
	CurrentID = GetCurrentProcessId();

	if (!DNSHandler::INIT())
		return false;

	if (!TCPHandler::INIT())
		return false;

	return true;
}

void eh_free()
{
	lock_guard<mutex> lg(udpContextLock);

	TCPHandler::FREE();

	for (auto i : udpContext)
		i.second->Close();
	udpContext.clear();
	udpProcessIds.clear();

	UP = 0;
	DL = 0;

	lock_guard<mutex> applicationsLock(proxyLogLock);
	proxiedApplications.clear();
	proxiedTcpEndpoints.clear();
}

void threadStart()
{

}

void threadEnd()
{

}

void tcpConnectRequest(ENDPOINT_ID id, PNF_TCP_CONN_INFO info)
{
	if (CurrentID == info->processId)
	{
		nf_tcpDisableFiltering(id);
		return;
	}

	if (!filterTCP)
	{
		nf_tcpDisableFiltering(id);

		return;
	}

	if (checkBypassName(info->processId))
	{
		nf_tcpDisableFiltering(id);

		return;
	}

	if (!checkHandleName(info->processId))
	{
		nf_tcpDisableFiltering(id);

		return;
	}

	if (info->ip_family != AF_INET && info->ip_family != AF_INET6)
	{
		nf_tcpDisableFiltering(id);

		return;
	}

	SOCKADDR_IN6 client;
	memcpy(&client, info->localAddress, sizeof(SOCKADDR_IN6));

	SOCKADDR_IN6 remote;
	memcpy(&remote, info->remoteAddress, sizeof(SOCKADDR_IN6));

	if (info->ip_family == AF_INET)
	{
		auto addr = (PSOCKADDR_IN)info->remoteAddress;
		addr->sin_family = AF_INET;
		addr->sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);
		addr->sin_port = tcpListen;
	}

	if (info->ip_family == AF_INET6)
	{
		auto addr = (PSOCKADDR_IN6)info->remoteAddress;
		IN6ADDR_SETLOOPBACK(addr);
		addr->sin6_port = tcpListen;
	}

	TCPHandler::CreateHandler(client, remote, info->processId);
	LogProxiedApplication(info->processId);
	{
		lock_guard<mutex> lock(proxyLogLock);
		proxiedTcpEndpoints.emplace(id);
	}
	LogProxyEvent(
		AIO_LOG_TCP_OPEN,
		info->processId,
		GetProcessName(info->processId) + L" -> " + ConvertIP((PSOCKADDR)&remote));
}

void tcpConnected(ENDPOINT_ID id, PNF_TCP_CONN_INFO info)
{
	UNREFERENCED_PARAMETER(id);
	UNREFERENCED_PARAMETER(info);
}

void tcpCanSend(ENDPOINT_ID id)
{
	UNREFERENCED_PARAMETER(id);
}

void tcpSend(ENDPOINT_ID id, const char* buffer, int length)
{
	UP += length;

	nf_tcpPostSend(id, buffer, length);
}

void tcpCanReceive(ENDPOINT_ID id)
{
	UNREFERENCED_PARAMETER(id);
}

void tcpReceive(ENDPOINT_ID id, const char* buffer, int length)
{
	DL += length;

	nf_tcpPostReceive(id, buffer, length);
}

void tcpClosed(ENDPOINT_ID id, PNF_TCP_CONN_INFO info)
{
	SOCKADDR_IN6 client;
	memcpy(&client, info->localAddress, sizeof(SOCKADDR_IN6));

	TCPHandler::DeleteHandler(client);

	bool wasProxied;
	{
		lock_guard<mutex> lock(proxyLogLock);
		wasProxied = proxiedTcpEndpoints.erase(id) != 0;
	}
	if (wasProxied)
	{
		LogProxyEvent(
			AIO_LOG_TCP_CLOSE,
			info->processId,
			GetProcessName(info->processId) + L" endpoint=" + to_wstring(id));
	}
}

void udpCreated(ENDPOINT_ID id, PNF_UDP_CONN_INFO info)
{
	if (CurrentID == info->processId)
	{
		nf_udpDisableFiltering(id);
		return;
	}

	if (!filterUDP && !filterDNS)
	{
		nf_udpDisableFiltering(id);
		return;
	}

	if (checkBypassName(info->processId))
	{
		nf_udpDisableFiltering(id);

		return;
	}

	bool handleProcess = checkHandleName(info->processId);
	bool handleSystemDns = filterDNS && dnsOnly;
	if (!handleProcess && !handleSystemDns)
	{
		nf_udpDisableFiltering(id);

		return;
	}

	lock_guard<mutex> lg(udpContextLock);
	udpProcessIds[id] = info->processId;
	if (handleProcess && filterUDP)
	{
		LogProxiedApplication(info->processId);
		udpContext[id] = make_shared<SocksHelper::UDP>();
		LogProxyEvent(AIO_LOG_UDP_OPEN, info->processId, GetProcessName(info->processId));
	}
}

void udpConnectRequest(ENDPOINT_ID id, PNF_UDP_CONN_REQUEST info)
{
	UNREFERENCED_PARAMETER(id);
	UNREFERENCED_PARAMETER(info);
}

void udpCanSend(ENDPOINT_ID id)
{
	UNREFERENCED_PARAMETER(id);
}

void udpSend(ENDPOINT_ID id, const unsigned char* target, const char* buffer, int length, PNF_UDP_OPTIONS options)
{
	if (DNSHandler::IsDNS((PSOCKADDR_IN6)target))
	{
		if (!filterDNS)
		{
			nf_udpPostSend(id, target, buffer, length, options);

			return;
		}
		else
		{
			UP += length;
			DNSHandler::CreateHandler(id, (PSOCKADDR_IN6)target, buffer, length, options);

			DWORD processId = 0;
			{
				lock_guard<mutex> lock(udpContextLock);
				auto process = udpProcessIds.find(id);
				if (process != udpProcessIds.end())
					processId = process->second;
			}
			if (processId != 0)
			{
				LogProxyEvent(
					AIO_LOG_DNS,
					processId,
					GetProcessName(processId) + L" -> " + ConvertIP((PSOCKADDR)target));
			}

			return;
		}
	}

	udpContextLock.lock();
	if (udpContext.find(id) == udpContext.end())
	{
		udpContextLock.unlock();

		nf_udpPostSend(id, target, buffer, length, options);
		return;
	}
	auto remote = udpContext[id];
	udpContextLock.unlock();

	if (remote->tcpSocket.load() == INVALID_SOCKET && !remote->Associate())
		return;

	if (remote->udpSocket.load() == INVALID_SOCKET)
	{
		if (!remote->CreateUDP())
			return;

		auto option = (PNF_UDP_OPTIONS)new char[sizeof(NF_UDP_OPTIONS) + options->optionsLength]();
		memcpy(option, options, sizeof(NF_UDP_OPTIONS) + options->optionsLength - 1);

		thread(udpReceiveHandler, id, remote, option).detach();
	}

	if (remote->Send((PSOCKADDR_IN6)target, buffer, length) == length)
		UP += length;
}

void udpCanReceive(ENDPOINT_ID id)
{
	UNREFERENCED_PARAMETER(id);
}

void udpReceive(ENDPOINT_ID id, const unsigned char* target, const char* buffer, int length, PNF_UDP_OPTIONS options)
{
	nf_udpPostReceive(id, target, buffer, length, options);
}

void udpClosed(ENDPOINT_ID id, PNF_UDP_CONN_INFO info)
{
	UNREFERENCED_PARAMETER(info);

	lock_guard<mutex> lg(udpContextLock);
	bool proxiedUdp = udpContext.find(id) != udpContext.end();
	auto process = udpProcessIds.find(id);
	if (process != udpProcessIds.end())
	{
		if (proxiedUdp)
			LogProxyEvent(AIO_LOG_UDP_CLOSE, process->second, GetProcessName(process->second));
		udpProcessIds.erase(process);
	}

	if (proxiedUdp)
	{
		udpContext[id]->Close();
		udpContext.erase(id);
	}
}

void udpReceiveHandler(ENDPOINT_ID id, shared_ptr<SocksHelper::UDP> remote, PNF_UDP_OPTIONS options)
{
	char buffer[1458];

	while (remote->tcpSocket.load() != INVALID_SOCKET && remote->udpSocket.load() != INVALID_SOCKET)
	{
		SOCKADDR_IN6 target;

		int length = remote->Read(&target, buffer, sizeof(buffer), NULL);
		if (length == 0 || length == SOCKET_ERROR)
			break;

		DL += length;

		nf_udpPostReceive(id, (unsigned char*)&target, buffer, length, options);
	}

	delete[] options;
}
