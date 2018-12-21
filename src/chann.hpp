#pragma once

#include <boost/asio/spawn.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/io_context.hpp>

#include <boost/unordered/unordered_map.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/system/error_code.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/noncopyable.hpp>
#include <boost/bind.hpp>
#include <iostream>

namespace asio {
	using namespace boost::asio;
};

class CChannel : public boost::enable_shared_from_this<CChannel>, boost::noncopyable
{
public:
	typedef boost::shared_ptr<CChannel> self_type;

	CChannel(asio::io_context& io, asio::ip::udp::endpoint& ep, uint8_t dir)
		: _strand(io)
		, _socket(io)
		, _remoteEp(ep)
		, _localEp(asio::ip::udp::endpoint(asio::ip::udp::v4(), 0))
		, _timer(io)
		, _dir(dir)
		, _started(true)
	{
		sprintf(_handshake_byte, "%c", _dir == 0 ? SRC_HANDSHAKE_BYTE : DST_HANDSHAKE_BYTE);
	}

	~CChannel()
	{	
		switch (_dir) {
		case 0:
			std::cout << "[" << _remoteEp.port() << "] udp src endpoint destroy channel. " << std::endl;
			break;
		case 1:
			std::cout << "[" << _localEp.port() << "] udp dst endpoint destroy channel. " << std::endl;
			break;
		default:
			break;
		}
	}
	static self_type start(asio::io_context& io, asio::ip::udp::endpoint& ep, uint8_t dir) {
		self_type this_(new CChannel(io, ep, dir));
		this_->go();
		return this_;
	}

	void stop() {
		_started = false;
	}

	void go() {
		asio::spawn(_strand, boost::bind(&CChannel::sender, shared_from_this(), boost::placeholders::_1));
	}

	void sender(asio::yield_context yield) {
		boost::system::error_code ec;
		asio::socket_base::reuse_address option(true);
		_socket.open(asio::ip::udp::v4(), ec);
		if (ec) {
			std::cout << "[" << _remoteEp.port() << "] open local udp failed: " << ec.message() << std::endl;
			return;
		}
		_socket.set_option(option);
		_socket.bind(_localEp, ec);
		if (ec) {
			std::cout << "[" << _remoteEp.port() << "] udp bind local failed: " << ec.message() << std::endl;
			return;
		}

		_socket.connect(_remoteEp, ec);
		if (ec) {
			std::cout << "[" << _remoteEp.port() << "] udp connect remote["<< _remoteEp.address().to_string() << "] failed: " << ec.message() << std::endl;
			return;
		}

		std::cout << "connect udp [" << _socket.local_endpoint() << "] --> [" << _socket.remote_endpoint() << "] ok." << std::endl;

		switch (_dir) {
		case 0:
			SrcChannStart(yield, ec);
			break;
		case 1:
			DstChannStart(yield, ec);
			break;
		default:
			break;
		}
	}

	void hearbeat(asio::yield_context yield) {
	
	}

	void SrcChannStart(asio::yield_context yield, boost::system::error_code& ec) {
		if (!SrcChannHandShake(yield, ec)) {
			std::cout << "[" << _remoteEp.port() << "] udp src endpoint handshake failed: " << ec.message() << std::endl;
			return;
		}
		asio::spawn(_strand, boost::bind(&CChannel::reader, shared_from_this(), boost::placeholders::_1));

		data_st data[1];
		size_t size = 10;
		memcpy(data[0].data, "1234567890", size);

		uint32_t seq = 1;
		while (_started && seq < LOOP_CNT) {
			data[0].id = seq;
			boost::posix_time::ptime time_now = boost::posix_time::microsec_clock::universal_time();

			uint32_t nsend = _socket.async_send(asio::buffer(data, sizeof(data->id) + size), yield[ec]);
			if (ec || nsend <= 0) {
				std::cout << "[" << _remoteEp.port() << "] udp src endpoint send failed: " << ec.message() << std::endl;
				continue;
			}
			_sendSeq.insert(SendSeqTimeMap::value_type(seq, time_now));
			seq++;

			int rs = rand() % 10;
			_timer.expires_from_now(std::chrono::milliseconds(500 + (rs * 100)));
			_timer.async_wait(yield[ec]);
			if (ec) {
				std::cout << "[" << _remoteEp.port() << "] udp src endpoint timer error: " << ec.message() << std::endl;
			}
		}
		_timer.expires_from_now(std::chrono::seconds(20));
		_timer.async_wait(yield[ec]);
		if (ec) {
			std::cout << "[" << _remoteEp.port() << "] udp src endpoint timer error: " << ec.message() << std::endl;
		}

		for (SendSeqTimeMap::iterator send_it = _sendSeq.begin(); send_it != _sendSeq.end(); ++send_it) {
			RecvSeqTimeMap::iterator recv_it = _recvSeq.find(send_it->first);
			if (recv_it != _recvSeq.end()) {
				uint64_t elapse = (recv_it->second - send_it->second).total_milliseconds();
				std::cout << "[" << _remoteEp.port() << "] seq[" << send_it->first << "] const: " << elapse << "ms" << std::endl;
			}
			else {
				std::cout << "[" << _remoteEp.port() << "] seq[" << send_it->first << "] no found." << std::endl;
			}
		}

		std::cout << "[" << _remoteEp.port() << "] udp src endpoint sender finish." << std::endl;
	}

	bool SrcChannHandShake(asio::yield_context yield, boost::system::error_code& ec) {
		while (_started) {
			_timer.expires_from_now(std::chrono::seconds(3));
			_timer.async_wait(yield[ec]);
			if (ec) {
				std::cout << "[" << _remoteEp.port() << "] udp src endpoint timer error: " << ec.message() << std::endl;
				continue;
			}

			uint32_t nsend = _socket.async_send(asio::buffer(_handshake_byte), yield[ec]);
			if (ec || nsend <= 0) {
				std::cout << "[" << _remoteEp.port() << "] udp src endpoint send handshake failed: " << ec.message() << std::endl;
				continue;
			}

			uint32_t nread = _socket.async_receive(asio::buffer(_readbuf), yield[ec]);
			if (ec || nread <= 0) {
				std::cout << "[" << _remoteEp.port() << "] udp src endpoint read handshake failed: " << ec.message() << std::endl;
				continue;
			}

			std::cout << "[" << _remoteEp.port() << "] udp src endpoint auth read: " << util::to_hex(_readbuf, nread) << std::endl;
			if (_readbuf[0] != _handshake_byte[0]) {
				std::cout << "[" << _remoteEp.port() << "] udp src endpoint handshake no success: " << (int)_readbuf[0] << std::endl;
				continue;
			}
			std::cout << "[" << _remoteEp.port() << "] udp src endpoint openned." << std::endl;

			return true;
		}
		return false;
	}

	void reader(asio::yield_context yield) {
		std::cout << "[" << _remoteEp.port() << "] udp src endpoint start reader." << std::endl;

		boost::system::error_code ec;
		if (_dir == 0) {
			uint32_t seq = 0;
			while (_started && seq < LOOP_CNT - 10) {
				uint32_t nread = _socket.async_receive(asio::buffer(_readbuf), yield[ec]);
				if (ec || nread <= 0) {
					std::cout << "[" << _remoteEp.port() << "] udp src endpoint read handshake failed: " << ec.message() << std::endl;
					continue;
				}
				memcpy(&seq, _readbuf, 4);
				boost::posix_time::ptime time_now = boost::posix_time::microsec_clock::universal_time();
				_recvSeq.insert(RecvSeqTimeMap::value_type(seq, time_now));
				//std::cout << "[" << _remoteEp.port() << "] seq[" << seq << "] const: " << e << "ms" << std::endl;
			}
		}
		std::cout << "[" << _remoteEp.port() << "] udp src endpoint reader finish." << std::endl;
	}

	void DstChannStart(asio::yield_context yield, boost::system::error_code& ec) {
		if (!DstChannHandShake(yield, ec)) {
			std::cout << "[" << _remoteEp.port() << "] udp dest endpoint handshake failed: " << ec.message() << std::endl;
			return;
		}

		std::cout << "[" << _remoteEp.port() << "] udp dest endpoint openned." << std::endl;

		uint32_t cnt = 0;
		while (_started) {
			uint32_t nread = _socket.async_receive(asio::buffer(_readbuf, BUFF_SIZE), yield[ec]);
			if (ec || nread <= 0) {
				std::cout << "[" << _remoteEp.port() << "] udp dest endpoint read failed: " << ec.message() << std::endl;
				continue;
			}

			std::cout << "[" << _remoteEp.port() << "] udp dest endpoint echo: " << ++cnt << std::endl;

			uint32_t nsend = _socket.async_send(asio::buffer(_readbuf, nread), yield[ec]);
			if (ec || nread <= 0) {
				std::cout << "[" << _remoteEp.port() << "] udp dest endpoint echo failed: " << ec.message() << std::endl;
				continue;
			}
		}
	}

	bool DstChannHandShake(asio::yield_context yield, boost::system::error_code& ec) {
		uint32_t nsend = _socket.async_send(asio::buffer(_handshake_byte), yield[ec]);
		if (ec || nsend <= 0) {
			std::cout << "[" << _remoteEp.port() << "] udp dest endpoint send handshake failed: " << ec.message() << std::endl;
			return false;
		}

		uint32_t nread = _socket.async_receive(asio::buffer(_readbuf, BUFF_SIZE), yield[ec]);
		if (ec || nread <= 0) {
			std::cout << "[" << _remoteEp.port() << "] udp dest endpoint read handshake failed: " << ec.message() << std::endl;
			return false;
		}
		if (_readbuf[0] != _handshake_byte[0]) {
			std::cout << "[" << _remoteEp.port() << "] udp dest endpoint handshake no success: " << (int)_readbuf[0] << std::endl;
			return false;
		}
		return true;
	}

private:
	enum { BUFF_SIZE = 1024 };
	enum {
		SRC_HANDSHAKE_BYTE = 0x01,
		DST_HANDSHAKE_BYTE = 0x02,
	};
	enum { LOOP_CNT = 5001 };

	asio::io_context::strand _strand;
	asio::ip::udp::socket	_socket;
	asio::ip::udp::endpoint	_remoteEp;
	asio::ip::udp::endpoint	_localEp;
	asio::steady_timer		_timer;
	typedef boost::unordered_map<uint32_t, boost::posix_time::ptime> SendSeqTimeMap;
	typedef boost::unordered_map<uint32_t, boost::posix_time::ptime> RecvSeqTimeMap;
	SendSeqTimeMap _sendSeq;
	RecvSeqTimeMap _recvSeq;
	char _readbuf[BUFF_SIZE];
	char _handshake_byte[2];
	uint8_t _dir;
	bool	_started;

	typedef struct {
		uint32_t id;
		char	 data[BUFF_SIZE];
	}data_st;
};