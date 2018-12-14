#pragma once

#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/thread/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <chrono>

#include <boost/enable_shared_from_this.hpp>
#include <boost/system/error_code.hpp>
#include <boost/noncopyable.hpp>
#include <boost/bind.hpp>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#include "util.hpp"
#include "chann.hpp"

namespace asio {
	using namespace boost::asio;
};


class CSession : public boost::enable_shared_from_this<CSession>, boost::noncopyable
{
public:
	typedef boost::shared_ptr<CSession> self_type;

	CSession(asio::io_context& io, asio::ip::tcp::endpoint& ep, int mode, uint32_t id)
		: _strand(io)
		, _timer(io)
		, _socket(io)
		, _ep(ep)
		, _guid("")
		, _id(id)
		, _loginid(0)
		, _dstid(0)
		, _started(true)
		, _mode(mode)
	{
	}

	static self_type NewSession(asio::io_context& io, asio::ip::tcp::endpoint& ep, int mode, uint32_t id) {
		self_type ss(new CSession(io, ep, mode, id));
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
		asio::spawn(_strand, boost::bind(&CSession::CtrlRoutine, shared_from_this(), boost::placeholders::_1));
	}
	void CtrlRoutine(asio::yield_context yield) {
		boost::system::error_code ec;

		_socket.async_connect(_ep, yield[ec]);
		if (ec) {
			std::cout << "connect failed: " << ec.message() << std::endl;
			return;
		}
		if (!onLogin(yield, ec)) {
			return;
		}

		if (_mode == 0) {
			do {
				_timer.expires_from_now(std::chrono::seconds(3));
				_timer.async_wait(yield[ec]);
				onGetClients(yield, ec);
			} while (_dstid == 0 || _dstid == _loginid);

			if (!_started)
				return;

			onAccelate(yield, ec);
		}
		else {
			while (_started) {
				onAccess(yield, ec);
			}
		}
		std::cout << "[" << _id << "][" << _loginid << "] get clients ok." << std::endl;
		const char hearbeat[9] = {0xDD, 0x05, 0x01, 0x02, 0x00, 0x00, 0x00, '\0'};
		while (_started) {
			_timer.expires_from_now(std::chrono::seconds(50));
			_timer.async_wait(yield[ec]);

			uint32_t bytes = asio::async_write(_socket, asio::buffer(hearbeat, 8), yield[ec]);
			if (ec) {
				std::cout << "[" << _loginid << "] send heart beat falied: " << ec.message() << std::endl;
			}
		}

		std::cout << "contrl routine finish." << std::endl;
	}

	bool onLogin(asio::yield_context yield, boost::system::error_code& ec) {
		_guid = util::getGuid(_mode);

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

		memset(package, 0, 128);
		size_t nread = asio::async_read(_socket, _tcpReadbuf, asio::transfer_exactly(8), yield[ec]);
		if (ec || nread <= 0) {
			std::cout << "[" << _guid << "] read login response failed: " << ec.message() << std::endl;
			Close();
			return false;
		}
		const char* pbuf = asio::buffer_cast<const char*>(_tcpReadbuf.data());
		if (pbuf[4] != 0x01)
		{
			std::cout << "[" << _guid << "] get login response header error: " << (int)pbuf[5] << std::endl;
			return false;
		}

		memcpy(&bodylen, pbuf + 6, 2);
		asio::async_read(_socket, _tcpReadbuf, asio::transfer_exactly(bodylen), yield[ec]);
		if (ec) {
			std::cout << "[" << _guid << "] login failed: " << ec.message() << std::endl;
			_tcpReadbuf.consume(8 + bodylen);
			return false;
		}

		pbuf = asio::buffer_cast<const char*>(_tcpReadbuf.data());
		if (pbuf[8] != 0) {
			std::cout << "[" << _guid << "] login faile: " << (uint32_t)pbuf[0] << std::endl;
			_tcpReadbuf.consume(8 + bodylen);
			return false;
		}
		memcpy(&_loginid, pbuf + 9, 4);
		std::cout << "[" << _guid << "] login id: " << _loginid << std::endl;

		_tcpReadbuf.consume(8 + bodylen);
		return true;
	}

	bool onGetClients(asio::yield_context yield, boost::system::error_code& ec) {
		struct head h { 0xDD, 0x05, 0x01, 0x02, 0x03, 0x00, };

		uint16_t bodylen = 4;
		char package[128];
		memset(package, 0, 128);
		memcpy(package, &h, 6);
		memcpy(package + 6, &bodylen, 2);
		memcpy(package + 8, &_loginid, 4);

		int nwrite = asio::async_write(_socket, asio::buffer(package, 12), yield[ec]);
		if (ec || nwrite != 12) {
			std::cout << "[" << _loginid << "] send get clients faield: " << ec.message() << std::endl;
			Close();
			return false;
		}
		memset(package, 0, 128);
		int nread = asio::async_read(_socket, _tcpReadbuf, asio::transfer_exactly(8), yield[ec]);
		if (ec || nread <= 0) {
			std::cout << "[" << _loginid << "] read get clients failed: " << ec.message() << std::endl;
			Close();
			return false;
		}

		const char* pbuf = asio::buffer_cast<const char*>(_tcpReadbuf.data());
		if (pbuf[4] != 0x03)
		{
			std::cout << "[" << _guid << "] get clients response header error: " << (int)pbuf[4] << std::endl;
			return false;
		}

		memcpy(&bodylen, pbuf + 6, 2);
		std::cout << "[" << _loginid << "] read get clients bodylen: " << bodylen << std::endl;

		nread = asio::async_read(_socket, _tcpReadbuf, asio::transfer_exactly(bodylen), yield[ec]);
		if (ec || nread <= 0) {
			std::cout << "[" << _loginid << "] read get clients body failed: " << ec.message() << std::endl;
			Close();
			return false;
		}

		uint8_t clients_num = pbuf[8];
		if (clients_num == 0)
		{
			std::cout << "[" << _loginid << "] get clients[" << 0 << "]" << std::endl;
			_tcpReadbuf.consume(8 + bodylen);
			return false;
		}

		std::string clients;
		for (int i = 9; i < clients_num * 8; ) {
			uint32_t client;
			uint32_t ip;
			memcpy(&client, pbuf + i, 4);
			memcpy(&ip, pbuf + i + 4, 4);
			i += 8;
			clients += std::to_string(client) + ",(" + std::to_string(ip) + "), ";
		}
		std::cout << "[" << _loginid << "] get dest clients[" << (int)clients_num << "]: " << clients << std::endl;

		uint32_t select_client = rand() % clients_num;
		memcpy(&_dstid, pbuf + 9 + (8 * select_client), 4);

		std::cout << "[" << _id << "] [" << _loginid << "] select dest client: " << _dstid << std::endl;
		_tcpReadbuf.consume(8 + bodylen);
		return true;
	}

	bool onAccelate(asio::yield_context yield, boost::system::error_code& ec) {
		struct head h { 0xDD, 0x05, 0x01, 0x02, 0x02, 0x00, };

		std::string gameid = "ThisIsGameId_" + std::to_string(_loginid);
		uint8_t gameid_len = static_cast<uint8_t>(gameid.length());

		uint16_t bodylen = 9 + gameid_len;
		char package[128];
		memset(package, 0, 128);
		memcpy(package, &h, 6);
		memcpy(package + 6, &bodylen, 2);

		memcpy(package + 8, &_loginid, 4);
		memcpy(package + 12, &_dstid, 4);

		package[16] = gameid_len;
		memcpy(package + 17, gameid.c_str(), gameid_len);

		std::cout << "[" << _loginid << "] accelate write[" << 8 + bodylen << "]: " << std::endl;

		int nwrite = asio::async_write(_socket, asio::buffer(package, 8 + bodylen), yield[ec]);
		if (ec || nwrite != 8 + bodylen) {
			std::cout << "[" << _loginid << "] send accelate faield: " << ec.message() << std::endl;
			Close();
			return false;
		}

		int nread = asio::async_read(_socket, _tcpReadbuf, asio::transfer_exactly(8), yield[ec]);
		if (ec || nread <= 0) {
			std::cout << "[" << _loginid << "] read accelate header failed: " << ec.message() << std::endl;
			Close();
			return false;
		}

		const char* pbuf = asio::buffer_cast<const char*>(_tcpReadbuf.data());

		if (pbuf[4] != 0x02) {
			std::cout << "[" << _loginid << "] read accelate function error: " << (int)pbuf[4] << std::endl;
			return false;
		}

		memcpy(&bodylen, pbuf + 6, 2);

		nread = asio::async_read(_socket, _tcpReadbuf, asio::transfer_exactly(bodylen), yield[ec]);
		if (ec || nread <= 0) {
			std::cout << "[" << _loginid << "] read accelate body failed: " << ec.message() << std::endl;
			Close();
			return false;
		}

		if (pbuf[8] != 0) {
			std::cout << "[" << _loginid << "] parse accelate body error: " << (int)pbuf[8] << std::endl;
			return false;
		}

		std::cout << "[" << _loginid << "] accelate read[" << nread << "]: " << std::endl;

		uint16_t udp_port;
		memcpy(&udp_port, pbuf + 13, 2);
		udp_port = asio::detail::socket_ops::network_to_host_short(udp_port);

		std::cout << "[" << _loginid << "] get udp port " << udp_port << std::endl;

		asio::ip::udp::endpoint ep(asio::ip::address::from_string(SERVER_IP), udp_port);
		CChannel::start(_strand.get_io_context(), ep, 0);
		
		return true;
	}

	bool onAccess(asio::yield_context yield, boost::system::error_code& ec) {
		int nread = asio::async_read(_socket, _tcpReadbuf, asio::transfer_exactly(8), yield[ec]);
		if (ec || !_started) {
			std::cout << "[" << _loginid << "] endpoint read failed.: " << ec.message() << std::endl;
			return false;
		}

		const char* pbuf = asio::buffer_cast<const char*>(_tcpReadbuf.data());
		if (pbuf[4] != 0x04) {
			_tcpReadbuf.consume(8);
			std::cout << "[" << _loginid << "] endpoint read no access function: " << static_cast<int>(pbuf[4]) << std::endl;
			return false;
		}

		uint16_t bodylen;
		memcpy(&bodylen, pbuf + 6, 2);

		nread = asio::async_read(_socket, _tcpReadbuf, asio::transfer_exactly(bodylen), yield[ec]);
		if (ec || nread <= 0) {
			std::cout << "[" << _loginid << "] read accelate body failed: " << ec.message() << std::endl;
			Close();
			return false;
		}
		uint16_t udp_port;
		memcpy(&udp_port, pbuf + 12, 2);
		udp_port = asio::detail::socket_ops::network_to_host_short(udp_port);

		std::cout << "[" << _loginid << "] access udp port: " << udp_port << std::endl;
		asio::ip::udp::endpoint ep(asio::ip::address::from_string(SERVER_IP), udp_port);
		CChannel::self_type chann = CChannel::start(_strand.get_io_context(), ep, 1);
		_dstChanns.push_back(chann);
		_tcpReadbuf.consume(8+bodylen);
		return true;
	}

	void ChannSenderRoutine(asio::yield_context yield) {

	}

	void ChannReaderRoutine(asio::yield_context yield) {

	}

private:
	asio::io_context::strand _strand;
	asio::steady_timer _timer;
	asio::ip::tcp::socket _socket;
	boost::shared_ptr<CChannel> _srcChann;
	std::vector<boost::shared_ptr<CChannel>> _dstChanns;

	asio::ip::tcp::endpoint _ep;
	asio::streambuf _tcpReadbuf;
	asio::streambuf _tcpSendbuf;
	asio::streambuf _udpReadbuf;
	asio::streambuf _udpSendbuf;
	std::string _guid;
	uint32_t	_id;
	uint32_t	_loginid;
	uint32_t	_dstid;
	bool		_started;
	int			_mode;
};
