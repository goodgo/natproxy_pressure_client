#pragma once

#include <boost/asio/spawn.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <boost/unordered/unordered_map.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/system/error_code.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/noncopyable.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include <sstream>

namespace asio {
	using namespace boost::asio;
};

template <typename T>
class CChannel : public boost::enable_shared_from_this<T>, boost::noncopyable
{
public:
	CChannel(asio::io_context& io, asio::ip::udp::endpoint& ep, 
		uint32_t id, uint32_t src_id, uint32_t dst_id)
		: _strand(io)
		, _socket(io)
		, _remote_ep(ep)
		, _local_ep(asio::ip::udp::endpoint(asio::ip::udp::v4(), 0))
		, _channel_info("")
		, _id(id)
		, _src_id(src_id)
		, _dst_id(dst_id)
		, _err_cnt(0)
		, _started(true)
	{
		std::stringstream os;
		os << "channel[" << id << "] (" << src_id << ") <--> (" << dst_id << ")";
		_channel_info = os.str();
	}

	virtual ~CChannel()
	{

	}

	virtual void stop() 
	{
		if (_started) {
			_started = false;		
			boost::system::error_code ec;
			_socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
			_socket.close(ec);

			std::cout << _channel_info << "stop.\n";
		}
	}

	virtual void sender(asio::yield_context yield) 
	{
		boost::system::error_code ec;
		_socket.open(asio::ip::udp::v4(), ec);
		if (ec) {
			std::cout << _channel_info << "open failed: " << ec.message() << "\n";
			stop();
			return;
		}
		_socket.set_option(asio::socket_base::reuse_address(true));
		_socket.bind(_local_ep, ec);
		if (ec) {
			std::cout << _channel_info << "bind failed: " << ec.message() << "\n";
			stop();
			return;
		}

		_socket.connect(_remote_ep, ec);
		if (ec) {
			std::cout << _channel_info << "connect [" << _remote_ep.address().to_string() 
				<< "] failed: " << ec.message() << "\n";
			stop();
			return;
		}

		std::cout << _channel_info << "[" << _socket.local_endpoint() << "] --> [" 
			<< _socket.remote_endpoint() << "] connected." << "\n";

		channStart(yield, ec);
	}

	virtual bool channStart(asio::yield_context yield, boost::system::error_code& ec) = 0;
	virtual bool handShake(asio::yield_context yield, boost::system::error_code& ec) = 0;

protected:
	enum { BUFF_SIZE = 1500 };
	enum {
		SRC_HANDSHAKE_BYTE = 0x01,
		DST_HANDSHAKE_BYTE = 0x02,
	};
	enum { ERR_ALLOW_CNT = 10 };

	asio::io_context::strand _strand;
	asio::ip::udp::socket	_socket;
	asio::ip::udp::endpoint	_remote_ep;
	asio::ip::udp::endpoint	_local_ep;

	std::string _channel_info;
	uint32_t _id;
	uint32_t _src_id;
	uint32_t _dst_id;
	char	_readbuf[BUFF_SIZE];
	char	_handshake_byte[2];
	int		_err_cnt;
	bool	_started;
};