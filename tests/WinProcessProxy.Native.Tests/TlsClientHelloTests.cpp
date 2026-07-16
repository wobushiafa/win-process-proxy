#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../../src/WinProcessProxy.Native/TlsClientHello.h"

namespace
{
	void AppendUInt16(std::vector<std::uint8_t>& data, size_t value)
	{
		data.push_back((std::uint8_t)(value >> 8));
		data.push_back((std::uint8_t)value);
	}

	void SetUInt16(std::vector<std::uint8_t>& data, size_t offset, size_t value)
	{
		data[offset] = (std::uint8_t)(value >> 8);
		data[offset + 1] = (std::uint8_t)value;
	}

	std::vector<std::uint8_t> CreateClientHello(const std::string& host, bool includeSni = true)
	{
		std::vector<std::uint8_t> data = { 0x16, 0x03, 0x03, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x03 };
		data.insert(data.end(), 32, 0x00);
		data.push_back(0x00);
		AppendUInt16(data, 2);
		AppendUInt16(data, 0x1301);
		data.push_back(1);
		data.push_back(0);

		size_t extensionsLengthOffset = data.size();
		AppendUInt16(data, 0);
		size_t extensionsStart = data.size();
		if (includeSni)
		{
			AppendUInt16(data, 0x0000);
			AppendUInt16(data, 5 + host.size());
			AppendUInt16(data, 3 + host.size());
			data.push_back(0x00);
			AppendUInt16(data, host.size());
			data.insert(data.end(), host.begin(), host.end());
		}

		SetUInt16(data, extensionsLengthOffset, data.size() - extensionsStart);
		size_t handshakeLength = data.size() - 9;
		data[6] = (std::uint8_t)(handshakeLength >> 16);
		data[7] = (std::uint8_t)(handshakeLength >> 8);
		data[8] = (std::uint8_t)handshakeLength;
		SetUInt16(data, 3, data.size() - 5);
		return data;
	}

	bool Expect(bool condition, const char* name)
	{
		if (condition)
			return true;
		std::cerr << "FAILED: " << name << std::endl;
		return false;
	}
}

int main()
{
	int failures = 0;
	auto hello = CreateClientHello("chatgpt.com");
	failures += !Expect(TlsClientHello::ParseServerName(hello) == "chatgpt.com", "parses valid SNI");

	auto partial = hello;
	partial.resize(partial.size() - 1);
	failures += !Expect(TlsClientHello::ParseServerName(partial).empty(), "rejects incomplete record");

	failures += !Expect(TlsClientHello::ParseServerName(CreateClientHello("", false)).empty(), "accepts ClientHello without SNI");
	failures += !Expect(TlsClientHello::ParseServerName(CreateClientHello("bad_host.example")).empty(), "rejects invalid hostname");

	auto malformed = hello;
	malformed[3] = 0x7f;
	malformed[4] = 0xff;
	failures += !Expect(TlsClientHello::ParseServerName(malformed).empty(), "rejects malformed record length");

	auto nonTls = hello;
	nonTls[0] = 0x17;
	failures += !Expect(TlsClientHello::ParseServerName(nonTls).empty(), "rejects non-handshake TLS record");

	if (failures == 0)
		std::cout << "All TLS ClientHello tests passed." << std::endl;
	return failures == 0 ? 0 : 1;
}
