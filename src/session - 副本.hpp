#pragma once

#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/thread/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/make_shared.hpp>
#include <boost/shared_ptr.hpp>

#include <boost/enable_shared_from_this.hpp>
#include <boost/system/error_code.hpp>
#include <boost/noncopyable.hpp>
#include <boost/bind.hpp>
#include <chrono>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#include "util.hpp"
#include "signature.hpp"

namespace asio {
	using namespace boost::asio;
};

enum {
	FUNC_HB = 0x00,
	FUNC_LOGIN = 0x01,
	FUNC_ACCELATE = 0x02,
	FUNC_GETCLIENTS = 0x03,
	FUNC_ACCESS = 0x04,
	FUNC_STOP = 0x05,
};

class CSession : public boost::enable_shared_from_this<CSession>, boost::noncopyable
{
public:
	typedef boost::shared_ptr<CSession> self_type;

	CSession(asio::io_context& io, asio::ip::tcp::endpoint& ep)
		: _strand(io)
		, _timer(io)
		, _socket(io)
		, _ep(ep)
		, _cond(boost::make_shared<async_condition_variable>(io))
		, _started(true)
		, _state(NOLOGIN)
	{
	}

	static self_type NewSession(asio::io_context& io, asio::ip::tcp::endpoint& ep) {
		self_type ss(new CSession(io, ep));
		ss->Go();
		return ss;
	}

	void Close() {
		if (_started) {
			_started = false;
			_socket.close();
		}
	}

	void Go() {
		asio::spawn(_strand, boost::bind(&CSession::CtrlSender, shared_from_this(), boost::placeholders::_1));
		asio::spawn(_strand, boost::bind(&CSession::CtrlReader, shared_from_this(), boost::placeholders::_1));
	}

	void CtrlSender(asio::yield_context yield) {
		boost::system::error_code ec;

		_socket.async_connect(_ep, yield[ec]);
		if (ec) {
			std::cout << "connect failed: " << ec.message() << std::endl;
			Close();
			return;
		}

		if (!onReqLogin(yield, ec)) {
			_started = false;
			return;
		}

		_cond->async_wait(yield[ec]);
		if (ec || !_started || _state != LOGINED) {
			std::cout << "[" << _id << "] login req failed: " << ec.message() << std::endl;
			return;
		}

		onReqGetClients(yield, ec);
	}

	void CtrlReader(asio::yield_context yield) {
		boost::system::error_code ec;
		while (_started) {
			char package[128] = { 0 };
			memset(package, 0, 128);
			size_t bytes = asio::async_read(_socket, _tcpReadbuf, asio::transfer_exactly(8), yield[ec]);

			if (ec || bytes <= 0) {
				std::cout << "[" << _id << "] read login response failed: " << ec.message() << std::endl;
				Close();
				continue;
			}
			const char* pbuf = asio::buffer_cast<const char*>(_tcpReadbuf.data());
			uint16_t bodylen;
			memcpy(&bodylen, pbuf + 6, 2);

			std::cout << "[" << _id << "] read: " << bytes << " bytes, body len: " << bodylen << std::endl;

			bytes = asio::async_read(_socket, _tcpReadbuf, asio::transfer_exactly(bodylen), yield[ec]);
			if (ec || bytes <= 0) {
				std::cout << "[" << _id << "] read login response failed: " << ec.message() << std::endl;
				Close();
				continue;
			}

			switch (pbuf[4]) {
				case FUNC_HB:
					std::cout << "[" << _id << "] heart beat." << std::endl;
					break;
				case FUNC_LOGIN: {
					onRespLogin(pbuf);
				}break;
				case FUNC_ACCELATE: {
			
				}break;
				case FUNC_GETCLIENTS: {
					onRespClients(pbuf);
				}break;
				case FUNC_ACCESS: {
			
				}break;
				case FUNC_STOP: {

				}break;
			}
			_tcpReadbuf.consume(8 + bodylen);
		}
	}

	void ChannSender(asio::yield_context yield) {

	}

	bool onReqLogin(asio::yield_context yield, boost::system::error_code& ec) {
		_guid = util::getGuid();
		std::cout << "guid: " << _guid << std::endl;

		struct head h { 0xDD, 0x05, 0x01, 0x02, 0x01, 0x00, };

		uint8_t guidlen = (uint8_t)_guid.length();
		uint16_t bodylen = 1 + guidlen + 4;
		uint32_t vip = 0xA0A0A0A;

		char package[128];
		memset(package, 0, 128);
		memcpy(package, &h, 6);
		memcpy(package + 6, &bodylen, 2);
		package[8] = guidlen;
		memcpy(package + 9, _guid.c_str(), guidlen);
		memcpy(package + 9 + guidlen, &vip, 4);

		int len = 8 + bodylen;

		int nwrite = asio::async_write(_socket, asio::buffer(package, len), yield[ec]);
		if (ec || nwrite != len) {
			std::cout << "[" << _guid << "] send login falied: " << ec.message() << std::endl;
			return false;
		}
		_state = LOGINING;
		return true;
	}

	bool onRespLogin(const char* pbuf) {
		if (_state == LOGINED) {
			return false;
		}
		int err = pbuf[8];
		if (err != 0) {
			std::cout << "[" << _guid << "] login failed: " << err << std::endl;
		}
		memcpy(&_id, pbuf + 9, 4);
		std::cout << "[" << _guid << "] login id: " << _id << std::endl;
		_state = LOGINED;
		_cond->notify_one();
		return true;
	}

	bool onRespClients(const char* pbuf) {
		uint8_t clients_num = pbuf[8];
		if (clients_num == 0)
		{
			std::cout << "[" << _id << "] get clients[" << 0 << std::endl;
			return false;
		}

		std::string clients;
		for (int i = 0; i < clients_num; i++) {
			uint32_t client;
			memcpy(&client, pbuf + 9 + (4 * i), 4);
			clients += std::to_string(client) + ",";
		}
		std::cout << "[" << _id << "] get dest clients[" << (int)clients_num << "]: " << clients << std::endl;

		uint32_t select_client = rand() % clients_num;
		memcpy(&_dstid, pbuf + 9 + (4 * select_client), 4);
		std::cout << "[" << _id << "] select dest client: " << _dstid << std::endl;

		return true;
	}

	bool onReqGetClients(asio::yield_context yield, boost::system::error_code& ec) {
		struct head h { 0xDD, 0x05, 0x01, 0x02, 0x03, 0x00, };

		uint16_t bodylen = 4;
		char package[128];
		memset(package, 0, 128);
		memcpy(package, &h, 6);
		memcpy(package + 6, &bodylen, 2);
		memcpy(package + 8, &_id, 4);

		int nwrite = asio::async_write(_socket, asio::buffer(package, 12), yield[ec]);
		if (ec || nwrite != 12) {
			std::cout << "[" << _id << "] send get clients faield: " << ec.message() << std::endl;
			Close();
			return false;
		}
		return true;
	}

	bool onAccelate(asio::yield_context yield, boost::system::error_code& ec) {
	
	}

private:
	asio::io_context::strand _strand;
	asio::steady_timer _timer;
	asio::ip::tcp::socket _socket;
	asio::ip::tcp::endpoint _ep;
	asio::streambuf _tcpReadbuf;
	asio::streambuf _tcpSendbuf;
	asio::streambuf _udpReadbuf;
	asio::streambuf _udpSendbuf;

	boost::shared_ptr<async_condition_variable> _cond;
	std::string _guid;
	uint32_t	_id;
	uint32_t	_dstid;
	bool _started;
	typedef enum {
		NOLOGIN = 0,
		LOGINING = 1,
		LOGINED = 2,
	}STATE;
	STATE _state;
};
