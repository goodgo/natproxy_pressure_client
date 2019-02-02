#pragma once

#include <string>
/*
#include <iomanip>
#include <sstream>
#include <boost/spirit/include/karma.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/unordered/unordered_map.hpp>
#include <boost/unordered/unordered_set.hpp>
*/
//#define SERVER_IP "172.16.31.194"

//#define SERVER_IP "111.230.172.225" //¹ã¶«6
//#define SERVER_IP "45.126.120.220"//¸£½¨1



struct login
{
	uint8_t len;
	std::string guid;
	uint32_t vip;
};


namespace util {
	std::string randGetGuid()
	{
		srand(time(NULL));
		static const std::string s("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
		std::string ret;
		for (int i = 0; i < 32; i++)
		{
			int k = rand() % s.size();
			ret += s[k];
		}
		return ret;
	}

	uint32_t randGetAddr()
	{
		uint32_t addr = 0x0A000000;
		for (int i = 0; i < 3; i++)
		{
			uint8_t *p = (uint8_t*)&addr;
			p[i] = static_cast<uint8_t>(rand() % 255);
		}
		return addr;
	}
	std::string to_hex(const char* buf, size_t len)
	{
		std::string str;
		char value[5];
		for (size_t i = 0; i < len; i++)
		{
			memset(value, 0, sizeof(value));
			sprintf(value, "%02X-", buf[i] & 0xff);
			str += value;
		}
		return str;
	}

	std::string formatBytes(const uint64_t& bytes)
	{
		const uint64_t KB = 1024;
		const uint64_t MB = 1024 * KB;
		const uint64_t GB = 1024 * MB;
		const uint64_t TB = 1024 * GB;

		char buff[32] = "";
		if (bytes >= TB)
			sprintf(buff, "%.2f TB", (double)bytes / TB);
		else if (bytes >= GB)
			sprintf(buff, "%.2f GB", (double)bytes / GB);
		else if (bytes >= MB)
			sprintf(buff, "%.2f MB", (double)bytes / MB);
		else if (bytes >= KB)
			sprintf(buff, "%.2f KB", (double)bytes / KB);
		else
			sprintf(buff, "%llu B", bytes);

		return buff;
	}

	/*
	inline std::string to_hex(const char*pbuf, const size_t len)
	{
		std::ostringstream out;
		out << std::hex;
		for (size_t i = 0; i < len; i++)
			out << std::setfill('0') << std::setw(2) << (static_cast<short>(pbuf[i]) & 0xff) << " ";
		return out.str();
	}

	inline std::string to_hex(std::string const& s)
	{
		return to_hex(s.c_str(), s.length());
	}

	template<typename T, std::size_t N>
	inline std::string to_hex(T(&arr)[N])
	{
		return to_hex(arr, N);
	}
	*/
}
