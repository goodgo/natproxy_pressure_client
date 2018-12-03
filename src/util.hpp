#pragma once

#include <string>

#define SERVER_IP "111.230.172.225" //¹ã¶«6
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
}
