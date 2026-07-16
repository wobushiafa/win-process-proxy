#pragma once
#ifndef TLSCLIENTHELLO_H
#define TLSCLIENTHELLO_H

#include <cstdint>
#include <string>
#include <vector>

namespace TlsClientHello
{
	std::string ParseServerName(const std::vector<std::uint8_t>& data);
}

#endif
