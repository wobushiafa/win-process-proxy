#include "TlsClientHello.h"

namespace
{
	bool ReadUInt16(const std::vector<std::uint8_t>& data, size_t offset, std::uint16_t& value)
	{
		if (offset + 2 > data.size())
			return false;
		value = ((std::uint16_t)data[offset] << 8) | data[offset + 1];
		return true;
	}
}

std::string TlsClientHello::ParseServerName(const std::vector<std::uint8_t>& data)
{
	std::uint16_t recordLength = 0;
	if (data.size() < 9 || data[0] != 0x16 || data[5] != 0x01 ||
		!ReadUInt16(data, 3, recordLength) || data.size() < 5 + recordLength)
		return {};
	size_t recordEnd = 5 + recordLength;

	size_t offset = 9 + 2 + 32;
	if (offset >= recordEnd)
		return {};
	offset += 1 + data[offset];

	std::uint16_t length = 0;
	if (offset + 2 > recordEnd || !ReadUInt16(data, offset, length))
		return {};
	offset += 2 + length;
	if (offset >= recordEnd)
		return {};
	offset += 1 + data[offset];

	std::uint16_t extensionsLength = 0;
	if (offset + 2 > recordEnd || !ReadUInt16(data, offset, extensionsLength))
		return {};
	offset += 2;
	size_t extensionsEnd = offset + extensionsLength;
	if (extensionsEnd > recordEnd)
		return {};

	while (offset + 4 <= extensionsEnd)
	{
		std::uint16_t type = 0;
		std::uint16_t extensionLength = 0;
		ReadUInt16(data, offset, type);
		ReadUInt16(data, offset + 2, extensionLength);
		offset += 4;
		if (offset + extensionLength > extensionsEnd)
			return {};

		if (type == 0x0000 && extensionLength >= 5)
		{
			std::uint16_t listLength = 0;
			if (!ReadUInt16(data, offset, listLength) || offset + 2 + listLength > extensionsEnd)
				return {};

			size_t nameOffset = offset + 2;
			size_t listEnd = nameOffset + listLength;
			while (nameOffset + 3 <= listEnd)
			{
				std::uint8_t nameType = data[nameOffset];
				std::uint16_t nameLength = 0;
				if (!ReadUInt16(data, nameOffset + 1, nameLength) || nameOffset + 3 + nameLength > listEnd)
					return {};
				if (nameType == 0 && nameLength != 0 && nameLength <= 253)
				{
					std::string host((const char*)data.data() + nameOffset + 3, nameLength);
					if (host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") == std::string::npos)
						return host;
				}
				nameOffset += 3 + nameLength;
			}
		}

		offset += extensionLength;
	}

	return {};
}
