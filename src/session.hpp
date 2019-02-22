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
#include "srcChannel.hpp"
#include "dstChannel.hpp"
#include "protocol.hpp"
#include "config.hpp"

namespace asio {
	using namespace boost::asio;
};


class CSession : public boost::enable_shared_from_this<CSession>, boost::noncopyable
{
public:
	typedef boost::shared_ptr<CSession> self_type;

	CSession(asio::io_context& io, asio::ip::tcp::endpoint& ep, uint32_t id, int dir, bool auto_mode)
		: _strand(io)
		, _cond(boost::make_shared<async_condition_variable>(io))
		, _timer(io)
		, _get_clients_timer(io)
		, _socket(io)
		, _ep(ep)
		, _dir(dir)
		, _guid("")
		, _id(id)
		, _loginid(0)
		, _chann_cnt(0)
		, _started(true)
		, _auto_mode(auto_mode)
	{
		_guid = util::randGetGuid();
		_gameid = "game_" + _guid;
		_private_addr = util::randGetAddr();
		_chann_cnt = 1;//rand() % 10;

		//std::cout << "guid:" << _guid << "_gameid:" << _gameid << "_vip:" << _private_addr << "_channnum: " << _chann_cnt << std::endl;
	}

	static self_type NewSession(asio::io_context& io, asio::ip::tcp::endpoint& ep, uint32_t id, int dir, bool auto_mode)
	{
		self_type ss(new CSession(io, ep, id, dir, auto_mode));
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

		_socket.async_connect(_ep, yield[ec]);
		if (ec) {
			std::cout << "connect failed: " << ec.message() << std::endl;
			return;
		}
		
		if (!reqLogin(yield, ec)) {
			Close();
			return;
		}

		if (_dir != 1 && !reqGetClients(yield, ec)) {
			Close();
			return;
		}
		
		asio::spawn(_strand, boost::bind(&CSession::reader, shared_from_this(), boost::placeholders::_1));

		if (_dir != 1) {
			reqAccelate(yield, ec);
		}

		while (_started) {
			_get_clients_timer.expires_from_now(std::chrono::seconds(100));
			_get_clients_timer.async_wait(yield[ec]);
			if (ec)
				break;
		}
	}

	void reader(asio::yield_context yield)
	{
		boost::system::error_code ec;
		while (_started) {
			if (!doRead(yield, ec, _header)) {
				continue;
			}

			const char* p = asio::buffer_cast<const char*>(_rbuf.data());
			const size_t n = _rbuf.size();

			switch (_header.ucFunc) {
			case EN_FUNC::ACCELATE: {
				boost::shared_ptr<CRespAccelate> pkg = boost::make_shared<CRespAccelate>();
				pkg->deserialize(p, n);
				onAccelate(pkg);
				return;
			}break;
			case EN_FUNC::GET_CONSOLES: {

			}break;
			case EN_FUNC::REQ_ACCESS: {
				boost::shared_ptr<CRespAccess> pkg = boost::make_shared<CRespAccess>();
				pkg->deserialize(p, n);
				onAccess(pkg);
				return;
			}break;
			case EN_FUNC::STOP_ACCELATE: {
				boost::shared_ptr<CRespStopAccelate> pkg = boost::make_shared<CRespStopAccelate>();
				pkg->deserialize(p, n);
				onStopAccelate(pkg);
			}break;
			default:
				break;
			}
			_rbuf.consume(8 + _header.usBodyLen);
		}
		
		std::cout << "[" << _loginid << "] exit reader." << std::endl;
	}

	bool doRead(asio::yield_context yield, boost::system::error_code& ec, SHeaderPkg& header)
	{
		size_t nread = asio::async_read(_socket, _rbuf, asio::transfer_exactly(8), yield[ec]);
		if (ec || nread <= 0) {
			std::cout << "[" << _loginid << "] read head failed: " << ec.message() << std::endl;
			Close();
			return false;
		}
		if (!checkHead())
			return false;

		const char* pbuf = asio::buffer_cast<const char*>(_rbuf.data());
		memcpy(&header, pbuf, 8);

		nread = asio::async_read(_socket, _rbuf, asio::transfer_exactly(header.usBodyLen), yield[ec]);
		if (ec) {
			std::cout << "[" << _loginid << "] read head failed: " << ec.message() << std::endl;
			return false;
		}

		//std::cout << "[" << _loginid << "] read(" << 8 + header.usBodyLen  << "): "
		//	<< util::to_hex(pbuf, 8 + header.usBodyLen) << std::endl;
		
		return true;
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

	bool reqAccelate(asio::yield_context yield, boost::system::error_code& ec)
	{
		for (uint32_t i = 0; i < _chann_cnt;) {
			if (!accelate(yield, ec))
				std::cout << "[" << _loginid << "] wait accelate error: " << ec.message() << std::endl;
			else
				i++;

			int rand_sec = rand() % 100;
			_get_clients_timer.expires_from_now(std::chrono::seconds(rand_sec));
			_get_clients_timer.async_wait(yield[ec]);
			if (!_started || ec) {
				std::cout << "[" << _loginid << "] wait accelate error: " << ec.message() << std::endl;
				break;
			}
		}
		return true;
	}

	bool accelate(asio::yield_context yield, boost::system::error_code& ec)
	{
		std::cout << "[" << _loginid << "] select accelate to " << _dst_info.uiId << std::endl;

		if (_dst_info.uiId == 0 || _dst_info.uiId == _loginid)
			return false;

		SHeaderPkg h{ 0xDD, 0x05, 0x01, 0x02, 0x02, 0x00, };

		uint16_t bodylen = 9 + _gameid.length();
		char package[256];
		memset(package, 0, 128);
		memcpy(package, &h, 6);
		memcpy(package + 6, &bodylen, 2);

		memcpy(package + 8, &_loginid, 4);
		memcpy(package + 8 + 4, &_dst_info.uiId, 4);
		package[8 + 4 + 4] = static_cast<uint8_t>(_gameid.length());
		memcpy(package + 8 + 4 + 4 + 1, _gameid.c_str(), _gameid.length());

		int nwrite = asio::async_write(_socket, asio::buffer(package, 8 + bodylen), yield[ec]);
		if (ec || nwrite != 8 + bodylen) {
			std::cout << "[" << _loginid << "] send get clients faield: " << ec.message() << std::endl;
			Close();
			return false;
		}
		std::cout << "[" << _loginid << "] send accelate: " << util::to_hex(package, 8 + bodylen) << std::endl;
		return true;
	}

	bool reqLogin(asio::yield_context yield, boost::system::error_code& ec)
	{
		SHeaderPkg h{ 0xDD, 0x05, 0x01, 0x02, 0x01, 0x00, };

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
			return false;
		}
		///////////////////////////////////////////////////
		if (!doRead(yield, ec, _header)) {
			std::cout << "[" << _guid << "] login error: " << ec.message() << std::endl;
			return false;
		}

		CRespLogin login_pkg;
		const char* pbuf = asio::buffer_cast<const char*>(_rbuf.data());
		if (!login_pkg.deserialize(pbuf, _rbuf.size())) {
			std::cout << "[" << _guid << "] login packet parse error: " << ec.message() << std::endl;
			return false;
		}

		_loginid = login_pkg.uiId;

		std::cout << "[" << _guid << "] login id: " << _loginid << std::endl;
		_rbuf.consume(8 + _header.usBodyLen);
		return true;
	}

	bool reqGetClients(asio::yield_context yield, boost::system::error_code& ec) 
	{
		SHeaderPkg h { 0xDD, 0x05, 0x01, 0x02, 0x03, 0x00, };

		uint16_t bodylen = 4;
		char package[128];
		memset(package, 0, 128);
		memcpy(package, &h, 6);
		memcpy(package + 6, &bodylen, 2);
		memcpy(package + 8, &_loginid, 4);

		while (_started) {
			int nwrite = asio::async_write(_socket, asio::buffer(package, 8 + bodylen), yield[ec]);
			if (ec || nwrite != 8 + bodylen) {
				std::cout << "[" << _loginid << "] send get clients faield: " << ec.message() << std::endl;
				Close();
				return false;
			}

			if (!doRead(yield, ec, _header) || ec) {
				std::cout << "[" << _loginid << "] get clients read error: " << ec.message() << std::endl;
				Close();
				return false;
			}

			const char* p = asio::buffer_cast<const char*>(_rbuf.data());
			const size_t n = _rbuf.size();
			boost::shared_ptr<CRespGetSessions> pkg = boost::make_shared<CRespGetSessions>();
			pkg->deserialize(p, n);
			
			if (!_auto_mode) {

				std::cout << "[" << _loginid << " get clients: [ ";
				int i = 0;
				for (auto it = pkg->_sessions.begin(); it != pkg->_sessions.end(); ++it, ++i) {
					std::cout << "(" << i << ": " << it->uiId << "), ";
				}
				std::cout << "\n[" << _loginid << "] input destination index: ";

				uint32_t dst_index;
				std::cin >> dst_index;
				if (dst_index >= pkg->_sessions.size()) {
					std::cout << "[" << _loginid << "] input index error! " << std::endl;
					continue;
				}
				_dst_info = pkg->_sessions[dst_index];
				std::cout << "[" << _loginid << "] you select destination index: " << dst_index << " --- " << _dst_info.uiId << std::endl;
			}
			else {
				if (pkg->_sessions.size() > 1) {
					while (_dst_info.uiId == 0 || _dst_info.uiId == _loginid) {
						int rd = rand() % pkg->_sessions.size();
						_dst_info = pkg->_sessions[rd];
					}
					std::cout << "[" << _loginid << "] automate select accelate to: " << _dst_info.uiId << std::endl;
				}
			}

			_rbuf.consume(8 + _header.usBodyLen);
			if (_dst_info.uiId != 0 && _dst_info.uiId != _loginid)
				return true;

			_get_clients_timer.expires_from_now(std::chrono::seconds(5));
			_get_clients_timer.async_wait(yield[ec]);
			if (ec) {
				Close();
				return false;
			}
		}

		std::cout << "[" << _loginid << "] send get clients: " << util::to_hex(package, 8 + bodylen) << std::endl;
		return true;
	}

	bool onAccelate(boost::shared_ptr<CRespAccelate> pkg) 
	{
		uint16_t port = asio::detail::socket_ops::host_to_network_short(pkg->usUdpPort);

		std::cout << "[" << _loginid << "] accelate get udp port: " << port << std::endl;

		asio::ip::udp::endpoint ep(_ep.address(), port);
		CSrcChannel::self_type chann = 
			CSrcChannel::start(_strand.get_io_context(), ep, pkg->uiUdpId, _loginid, _dst_info.uiId);
		_src_channs.insert(std::make_pair(pkg->uiUdpId, chann));

		return true;
	}

	bool onAccess(boost::shared_ptr<CRespAccess> pkg) 
	{
		uint16_t port = asio::detail::socket_ops::host_to_network_short(pkg->usUdpPort);
		std::cout << "[" << _loginid << "] access udp port: " << port << std::endl;
		asio::ip::udp::endpoint ep(_ep.address(), port);
		
		CDstChannel::self_type chann = 
			CDstChannel::start(_strand.get_io_context(), ep, pkg->uiUdpId, pkg->uiSrcId, _loginid);
		_dst_channs.insert(std::make_pair(pkg->uiUdpId, chann));
		return true;
	}

	bool onStopAccelate(boost::shared_ptr<CRespStopAccelate> pkg)
	{
		uint16_t port = asio::detail::socket_ops::host_to_network_short(pkg->usUdpPort);
		std::cout << "[" << _loginid << "] stop accelate udp port: " << port << std::endl;

		std::map<uint32_t, boost::shared_ptr<CSrcChannel>>::iterator it = _src_channs.find(pkg->uiUdpId);
		if (it != _src_channs.end()) {
			std::cout << "[" << _loginid << "] stop accelate src udp port: " << port << std::endl;
			it->second->stop();
			_src_channs.erase(it);
		}

		std::map<uint32_t, boost::shared_ptr<CDstChannel>>::iterator it2 = _dst_channs.find(pkg->uiUdpId);
		if (it2 != _dst_channs.end()) {
			std::cout << "[" << _loginid << "] stop accelate dst udp port: " << port << std::endl;
			it2->second->stop();
			_dst_channs.erase(it2);
		}
		return true;
	}

private:
	asio::io_context::strand _strand;
	boost::shared_ptr<async_condition_variable> _cond;
	asio::steady_timer _timer;
	asio::steady_timer _get_clients_timer;
	asio::ip::tcp::socket _socket;
	std::map<uint32_t, boost::shared_ptr<CSrcChannel>> _src_channs;
	std::map<uint32_t, boost::shared_ptr<CDstChannel>> _dst_channs;
	
	asio::ip::tcp::endpoint _ep;
	asio::streambuf _rbuf;
	asio::streambuf _sbuf;
	int _dir;
	SHeaderPkg _header;
	std::string _gameid;
	std::string _guid;
	uint32_t	_private_addr;
	uint32_t	_id;
	uint32_t	_loginid;
	std::string _select_mode;
	SSessionInfo _dst_info;
	uint32_t	_chann_cnt;
	bool		_started;
	bool		_auto_mode;
};
