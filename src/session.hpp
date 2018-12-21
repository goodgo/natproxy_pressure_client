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
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
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
#include "signature.hpp"
#include "util.hpp"
#include "chann.hpp"
#include "protocol.hpp"

namespace asio {
	using namespace boost::asio;
};


class CSession : public boost::enable_shared_from_this<CSession>, boost::noncopyable
{
public:
	typedef boost::shared_ptr<CSession> self_type;

	CSession(asio::io_context& io, asio::ip::tcp::endpoint& ep, uint32_t id)
		: _strand(io)
		, _cond(boost::make_shared<async_condition_variable>(io))
		, _timer(io)
		, _get_clients_timer(io)
		, _socket(io)
		, _sessions(std::vector<SSessionInfo>(0))
		, _ep(ep)
		, _guid("")
		, _id(id)
		, _loginid(0)
		, _dstid(0)
		, _chann_cnt(0)
		, _started(true)
	{
		_guid = util::randGetGuid();
		_gameid = "game_" + _guid;
		_private_addr = util::randGetAddr();
		_chann_cnt = 1;//rand() % 10;

		std::cout << "guid:" << _guid << "_gameid:" << _gameid << "_vip:" << _private_addr << "_channnum: " << _chann_cnt << std::endl;
	}

	static self_type NewSession(asio::io_context& io, asio::ip::tcp::endpoint& ep, uint32_t id) 
	{
		self_type ss(new CSession(io, ep, id));
		ss->Go();
		return ss;
	}

	void Close() 
	{
		if (_started) {
			_started = false;
			_socket.close();
		}
	}

	void Go() 
	{
		asio::spawn(_strand, boost::bind(&CSession::start, shared_from_this(), boost::placeholders::_1));
	}

	void start(asio::yield_context yield) 
	{
		boost::system::error_code ec;
		if (_socket.is_open())
			return;

		_socket.async_connect(_ep, yield[ec]);
		if (ec) {
			std::cout << "connect failed: " << ec.message() << std::endl;
			return;
		}
		std::cout << "login: " << _guid << std::endl;
		SHeaderPkg h { 0xDD, 0x05, 0x01, 0x02, 0x01, 0x00, };

		uint8_t guidlen = (uint8_t)_guid.length();
		uint16_t bodylen = 1 + guidlen + 4;

		char package[128];
		memset(package, 0, 128);
		memcpy(package, &h, 6);
		memcpy(package + 6, &bodylen, 2);
		package[8] = guidlen;
		memcpy(package + 9, _guid.c_str(), guidlen);
		memcpy(package + 9 + guidlen, &_private_addr, 4);

		int len = 8 + bodylen;

		int nwrite = asio::async_write(_socket, asio::buffer(package, len), yield[ec]);
		if (ec || nwrite != len) {
			std::cout << "[" << _guid << "] send login falied: " << ec.message() << std::endl;
			return;
		}

		memset(package, 0, 128);
		size_t nread = asio::async_read(_socket, _rbuf, asio::transfer_exactly(8), yield[ec]);
		if (ec || nread <= 0) {
			std::cout << "[" << _guid << "] read login response failed: " << ec.message() << std::endl;
			Close();
			return;
		}
		const char* pbuf = asio::buffer_cast<const char*>(_rbuf.data());
		if (pbuf[4] != 0x01)
		{
			std::cout << "[" << _guid << "] get login response header error: " << (int)pbuf[5] << std::endl;
			return;
		}

		std::cout << "[" << _loginid << "] login head: " << util::to_hex(pbuf, nread) << std::endl;

		memcpy(&bodylen, pbuf + 6, 2);
		nread = asio::async_read(_socket, _rbuf, asio::transfer_exactly(bodylen), yield[ec]);
		if (ec) {
			std::cout << "[" << _guid << "] login failed: " << ec.message() << std::endl;
			_rbuf.consume(8 + bodylen);
			return;
		}
		std::cout << "[" << _loginid << "] login package: " << util::to_hex(pbuf, 8 + bodylen) << std::endl;
		pbuf = asio::buffer_cast<const char*>(_rbuf.data());
		if (pbuf[8] != 0) {
			std::cout << "[" << _guid << "] login faile: " << (uint32_t)pbuf[0] << std::endl;
			_rbuf.consume(8 + bodylen);
			return;
		}
		memcpy(&_loginid, pbuf + 9, 4);
		std::cout << "[" << _guid << "] login id: " << _loginid << std::endl;

		_rbuf.consume(8 + bodylen);
		
		asio::spawn(_strand, boost::bind(&CSession::reader, shared_from_this(), boost::placeholders::_1));
	
		for (int i = 0; i < _chann_cnt;) {
			std::cout << _loginid << " start get clients." << std::endl;
			getClients(yield, ec);
			_cond->async_wait(yield[ec]);
			std::cout << _loginid << " start accelate." << std::endl;

			if (!accelate(yield, ec)) {
				std::cout << _loginid << " wait accelate." << std::endl;
				_get_clients_timer.expires_from_now(std::chrono::seconds(15));
				_get_clients_timer.async_wait(yield[ec]);
			}
			else
				i++;
		}
	}

	void reader(asio::yield_context yield)
	{
		boost::system::error_code ec;
		while (_started) {
			
			std::cout << "reading header..." << std::endl;
			uint32_t bytes = asio::async_read(_socket, _rbuf, asio::transfer_exactly(8), yield[ec]);
			if (ec || bytes <= 0) {
				std::cout << "read error: " << ec.message() << std::endl;
				continue;
			}

			if (!checkHead())
				continue;

			const char* pbuf = asio::buffer_cast<const char*>(_rbuf.data());
			uint16_t bodylen;
			memcpy(&bodylen, pbuf + 6, 2);

			std::cout << "reader head: " << util::to_hex(pbuf, 8) << ", bodylen: " << bodylen << std::endl;

			bytes = asio::async_read(_socket, _rbuf, asio::transfer_exactly(bodylen), yield[ec]);
			if (ec || bytes <= 0) {
				std::cout << "read body error: " << ec.message() << std::endl;
				_rbuf.consume(8);
				continue;
			}

			const char* p = asio::buffer_cast<const char*>(_rbuf.data());
			const size_t n = _rbuf.size();

			std::cout << "session[" << _loginid << "] read package: "
				<< util::to_hex(p, sizeof(_header) + _header.usBodyLen) << std::endl;

			switch (_header.ucFunc) {
			case EN_FUNC::ACCELATE: {
				boost::shared_ptr<CRespAccelate> pkg = boost::make_shared<CRespAccelate>();
				pkg->deserialize(p, n);
				onAccelate(pkg);
			}break;
			case EN_FUNC::GET_CONSOLES: {
				boost::shared_ptr<CRespGetSessions> pkg = boost::make_shared<CRespGetSessions>();
				pkg->deserialize(p, n);
				if (pkg->_sessions.size() > 1)
					_sessions = pkg->_sessions;
				_cond->notify_all();
			}break;
			case EN_FUNC::REQ_ACCESS: {
				boost::shared_ptr<CRespAccess> pkg = boost::make_shared<CRespAccess>();
				pkg->deserialize(p, n);
				onAccess(pkg);
			}break;
			case EN_FUNC::STOP_ACCELATE: {
				boost::shared_ptr<CRespAccess> pkg = boost::make_shared<CRespAccess>();
				pkg->deserialize(p, n);
				onAccess(pkg);
			}break;
			default:
				break;
			}
			_rbuf.consume(8 + bodylen);
		}
		
		std::cout << "session[" << _loginid << "] exit reader." << std::endl;
	}

	bool checkHead()
	{
		const char* pbuf = asio::buffer_cast<const char*>(_rbuf.data());
		memcpy(&_header, pbuf, sizeof(SHeaderPkg));

		if ((_header.ucHead1 == EN_HEAD::H1 &&
			_header.ucHead2 == EN_HEAD::H2) &&
			(EN_SVR_VERSION::ENCRYP == _header.ucSvrVersion ||
				EN_SVR_VERSION::NOENCRYP == _header.ucSvrVersion))
		{
			return true;
		}

		std::cout << "session[" << _id << "] read header error:  0x"
			<< std::hex << (int)_header.ucHead1 << " | 0x"
			<< std::hex << (int)_header.ucHead2 << " | 0x"
			<< std::hex << (int)_header.ucSvrVersion
			<< std::endl;

		memset(&_header, 0, sizeof(SHeaderPkg));
		_rbuf.consume(2);
		return false;
	}

	bool accelate(asio::yield_context yield, boost::system::error_code& ec)
	{
		if (_sessions.size() <= 1)
			return false;

		std::cout << _loginid << " sessions: " << _sessions.size() << std::endl;
		uint32_t  dst_id = 0;
		for (int i = 0; i < 4; i++) {
			int r = rand() % _sessions.size();
			if (_sessions[r].uiId != _loginid) {
				dst_id = _sessions[r].uiId;
				break;
			}
		}

		std::cout << _loginid << " select accelate to " << dst_id << std::endl;

		if (dst_id == 0)
			return false;

		SHeaderPkg h{ 0xDD, 0x05, 0x01, 0x02, 0x02, 0x00, };

		uint16_t bodylen = 5 + _gameid.length();
		char package[256];
		memset(package, 0, 128);
		memcpy(package, &h, 6);
		memcpy(package + 6, &bodylen, 2);

		memcpy(package + 8, &_loginid, 4);
		memcpy(package + 8 + 4, &dst_id, 4);
		package[8 + 4 + 4] = static_cast<uint8_t>(_gameid.length());
		memcpy(package + 8 + 4 + +4 + 1, _gameid.c_str(), _gameid.length());

		int nwrite = asio::async_write(_socket, asio::buffer(package, 8 + bodylen), yield[ec]);
		if (ec || nwrite != 8 + bodylen) {
			std::cout << "[" << _loginid << "] send get clients faield: " << ec.message() << std::endl;
			Close();
			return false;
		}
		std::cout << _loginid << " send accelate: " << util::to_hex(package, 8 + bodylen) << std::endl;
		return true;
	}

	bool getClients(asio::yield_context yield, boost::system::error_code& ec) 
	{
		SHeaderPkg h { 0xDD, 0x05, 0x01, 0x02, 0x03, 0x00, };

		uint16_t bodylen = 4;
		char package[128];
		memset(package, 0, 128);
		memcpy(package, &h, 6);
		memcpy(package + 6, &bodylen, 2);
		memcpy(package + 8, &_loginid, 4);

		int nwrite = asio::async_write(_socket, asio::buffer(package, 8 + bodylen), yield[ec]);
		if (ec || nwrite != 8 + bodylen) {
			std::cout << "[" << _loginid << "] send get clients faield: " << ec.message() << std::endl;
			Close();
			return false;
		}
		std::cout << _loginid << " send get clients: " << util::to_hex(package, 8 + bodylen) << std::endl;
		return true;
	}

	bool onAccelate(boost::shared_ptr<CRespAccelate> pkg) 
	{
		uint16_t port = asio::detail::socket_ops::host_to_network_short(pkg->usUdpPort);

		std::cout << "[" << _loginid << "] accelate get udp port: " << port << std::endl;

		asio::ip::udp::endpoint ep(asio::ip::address::from_string(SERVER_IP), port);
		CChannel::self_type chann = CChannel::start(_strand.get_io_context(), ep, 0);
		_src_channs.insert(std::make_pair(port, chann));

		return true;
	}

	bool onAccess(boost::shared_ptr<CRespAccess> pkg) 
	{
		uint16_t port = asio::detail::socket_ops::host_to_network_short(pkg->usUdpPort);
		std::cout << "[" << _loginid << "] access udp port: " << port << std::endl;
		asio::ip::udp::endpoint ep(asio::ip::address::from_string(SERVER_IP), port);
		
		CChannel::self_type chann = CChannel::start(_strand.get_io_context(), ep, 1);
		_dst_channs.insert(std::make_pair(port, chann));
		return true;
	}

	bool onStopAccelate(boost::shared_ptr<CRespStopAccelate> pkg)
	{
		uint16_t port = asio::detail::socket_ops::host_to_network_short(pkg->usUdpPort);
		std::cout << "[" << _loginid << "] stop accelate udp port: " << port << std::endl;

		std::map<uint16_t, boost::shared_ptr<CChannel>>::iterator it = _src_channs.find(port);
		if (it != _src_channs.end()) {
			std::cout << "[" << _loginid << "] stop accelate src udp port: " << port << std::endl;
			it->second->stop();
			_src_channs.erase(it);
		}

		it = _dst_channs.find(port);
		if (it != _dst_channs.end()) {
			std::cout << "[" << _loginid << "] stop accelate dst udp port: " << port << std::endl;
			it->second->stop();
			_dst_channs.erase(it);
		}
		return true;
	}

private:
	asio::io_context::strand _strand;
	boost::shared_ptr<async_condition_variable> _cond;
	asio::steady_timer _timer;
	asio::steady_timer _get_clients_timer;
	asio::ip::tcp::socket _socket;
	std::map<uint16_t, boost::shared_ptr<CChannel>> _src_channs;
	std::map<uint16_t, boost::shared_ptr<CChannel>> _dst_channs;
	std::vector<SSessionInfo> _sessions;
	
	asio::ip::tcp::endpoint _ep;
	asio::streambuf _rbuf;
	asio::streambuf _sbuf;

	SHeaderPkg _header;
	std::string _gameid;
	std::string _guid;
	uint32_t	_private_addr;
	uint32_t	_id;
	uint32_t	_loginid;
	uint32_t	_dstid;
	uint32_t	_chann_cnt;
	bool		_started;
};
