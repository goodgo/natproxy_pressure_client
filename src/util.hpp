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
#define SERVER_IP "172.16.31.187"

//#define SERVER_IP "111.230.172.225" //¹ã¶«6
//#define SERVER_IP "45.126.120.220"//¸£½¨1
struct head
{
	uint8_t h1;
	uint8_t h2;
	uint8_t protoVersion;
	uint8_t serverVersion;
	uint8_t func;
	uint8_t key;
	uint16_t bodylen;
};

struct login
{
	uint8_t len;
	std::string guid;
	uint32_t vip;
};


namespace util {
	std::string getGuid(int mode)
	{
		static const std::string s("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
		std::string ret = (mode == 0 ? "sp" : "ep");
		for (int i = 0; i < 30; i++)
		{
			int k = rand() % s.size();
			ret += s[k];
		}
		return ret;
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
