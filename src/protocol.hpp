#pragma once
#include <cstdint>
#include <boost/smart_ptr/shared_ptr.hpp>

struct EN_HEAD {
	enum { H1 = 0xDD, H2 = 0x05 };
};

struct EN_SVR_VERSION {
	enum { ENCRYP = 0x02, NOENCRYP = 0x04 };
};

struct EN_FUNC {
	enum {
		HEARTBEAT = 0x00,
		LOGIN = 0x01,
		ACCELATE = 0x02,
		GET_CONSOLES = 0x03,
		REQ_ACCESS = 0x04,
		STOP_ACCELATE = 0x05
	};
};

typedef struct
{
	uint8_t ucHead1;
	uint8_t ucHead2;
	uint8_t ucPrtVersion;
	uint8_t ucSvrVersion;
	uint8_t ucFunc;
	uint8_t ucKeyIndex;
	uint16_t usBodyLen;
}SHeaderPkg;

class SSessionInfo
{
public:
	SSessionInfo() : uiId(0), uiAddr(0) {}
	SSessionInfo(uint32_t id, uint32_t addr) : uiId(id), uiAddr(addr) {}
	SSessionInfo& operator=(const SSessionInfo& info)
	{
		uiId = info.uiId;
		uiAddr = info.uiAddr;
		return *this;
	}
	static uint8_t size() {
		return static_cast<uint8_t>(
			sizeof(uiId) +
			sizeof(uiAddr));
	}
	uint32_t uiId;
	uint32_t uiAddr;
};

class CRespPkgBase
{
public:
	CRespPkgBase() : ucErr(0) {}
	virtual ~CRespPkgBase() {}
	virtual bool deserialize(const char* p, const size_t n) = 0;

public:
	SHeaderPkg header;
	uint8_t ucErr;
};

class CRespLogin : public CRespPkgBase
{
public:
	CRespLogin()
		: CRespPkgBase()
		, uiId(0)
	{}
	virtual ~CRespLogin() {}

	virtual bool deserialize(const char* p, const size_t n)
	{
		if (n < 5)
			return false;

		memcpy(&header, p, sizeof(SHeaderPkg));
		p += sizeof(SHeaderPkg);

		size_t len = p[0];
		if (n < len + 5)
		{
			return false;
		}

		//szGuid.assign(p + 1, len);
		//memcpy(&uiPrivateAddr, p + 1 + len, 4);
	}

public:
	uint32_t uiId;
};

class CRespAccelate : public CRespPkgBase
{
public:
	CRespAccelate()
		: CRespPkgBase()
		, uiUdpAddr(0)
		, usUdpPort(0)
	{}
	virtual ~CRespAccelate() {}
	virtual bool deserialize(const char* p, const size_t n)
	{
		const char* body = p + 9;
		memcpy(&uiUdpAddr, body, 4);
		memcpy(&usUdpPort, body + 4, 2);

		return true;
	}

public:
	uint32_t uiUdpAddr;
	uint16_t usUdpPort;
};

class CRespAccess : public CRespPkgBase
{
public:
	CRespAccess()
		: CRespPkgBase()
		, uiSrcId(0)
		, uiUdpAddr(0)
		, usUdpPort(0)
		, uiPrivateAddr(0)
	{}
	virtual ~CRespAccess() {}
	virtual bool deserialize(const char* p, const size_t n)
	{
		const char* body = p + 8;
		memcpy(&uiSrcId, body, 4);
		memcpy(&uiUdpAddr, body + 4, 4);
		memcpy(&usUdpPort, body + 4 + 4, 2);
		memcpy(&uiPrivateAddr, body + 4 + 4 + 2, 4);

		return true;
	}

public:
	uint32_t uiSrcId;
	uint32_t uiUdpAddr;
	uint16_t usUdpPort;
	uint32_t uiPrivateAddr;
};

class CRespGetSessions : public CRespPkgBase
{
public:
	CRespGetSessions() : CRespPkgBase() {}
	virtual ~CRespGetSessions() {}
	virtual bool deserialize(const char* p, const size_t n)
	{
		const char* body = p + 8;
		uint8_t num = body[0];
		body++;
		std::cout << "num: " << (int)num << std::endl;
		for (uint8_t i = 0; i < num; i++) {
			SSessionInfo info;

			memcpy(&info.uiId, body, 4);
			memcpy(&info.uiAddr, body + 4, 4);

			_sessions.push_back(info);

			//std::cout << "body: " << body << std::endl;
			body += 8;
		}
		return true;
	}
public:
	std::vector<SSessionInfo> _sessions;
};

class CRespStopAccelate : public CRespPkgBase
{
public:
	CRespStopAccelate()
		: CRespPkgBase()
		, uiUdpAddr(0)
		, usUdpPort(0)
	{}
	virtual ~CRespStopAccelate() {}
	virtual bool deserialize(const char* p, const size_t n)
	{
		const char* body = p + 8;
		memcpy(&uiUdpAddr, body, 4);
		memcpy(&usUdpPort, body + 4, 2);

		return true;
	}

public:
	uint32_t uiUdpAddr;
	uint16_t usUdpPort;
};