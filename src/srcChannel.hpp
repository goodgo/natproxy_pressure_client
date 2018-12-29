#pragma once

#include "channel.hpp"
#include "util.hpp"

class CSrcChannel : public CChannel<CSrcChannel>
{
public:
	typedef boost::shared_ptr<CSrcChannel> self_type;

	CSrcChannel(asio::io_context& io, asio::ip::udp::endpoint& ep, 
		uint32_t id, uint32_t src_id, uint32_t dst_id)
		: CChannel(io, ep, id, src_id, dst_id)
		, _timer(io)
		, _display_timer(io)
		, _send_packs(0)
		, _send_bytes(0)
		, _recv_packs(0)
		, _recv_bytes(0)
		, _mode(0)
	{
		sprintf(_handshake_byte, "%c", SRC_HANDSHAKE_BYTE);
	}

	virtual ~CSrcChannel()
	{
		std::cout << _channel_info << "src endpoint destroy.\n";
	}

	static self_type start(asio::io_context& io, asio::ip::udp::endpoint& ep, 
		uint32_t id, uint32_t src_id, uint32_t dst_id)
	{
		self_type this_(new CSrcChannel(io, ep, id, src_id, dst_id));
		this_->go();
		return this_;
	}
	
	void go()
	{
		asio::spawn(_strand, boost::bind(&CSrcChannel::sender, shared_from_this(), boost::placeholders::_1));
		asio::spawn(_strand, boost::bind(&CSrcChannel::displayer, shared_from_this(), boost::placeholders::_1));
	}

	bool channStart(asio::yield_context yield, boost::system::error_code& ec) 
	{
		if (!handShake(yield, ec)) {
			std::cout << _channel_info << "src endpoint handshake failed: " << ec.message() << "\n";
			stop();
			return false;
		}
		asio::spawn(_strand, boost::bind(&CSrcChannel::reader, shared_from_this(), boost::placeholders::_1));

		data_st data[1];
		size_t size = 10;
		memcpy(data[0].data, "1234567890", size);

		uint32_t seq = 1;
		while (_started && _err_cnt < ERR_ALLOW_CNT && seq < LOOP_CNT) {
			data[0].id = seq;
			boost::posix_time::ptime time_now = boost::posix_time::microsec_clock::universal_time();

			uint32_t nsend = _socket.async_send(asio::buffer(data, sizeof(data->id) + size), yield[ec]);
			if (ec || nsend <= 0) {
				_err_cnt++;
				std::cout << _channel_info << "src endpoint send failed: " << ec.message() << "\n";
				continue;
			}

			_send_packs++;
			_send_bytes += nsend;
			_send_seq.insert(SendSeqTimeMap::value_type(seq++, time_now));

			int rand_ms = rand() % 1000;
			_timer.expires_from_now(std::chrono::milliseconds(rand_ms));
			_timer.async_wait(yield[ec]);
			if (ec) {
				_err_cnt++;
				std::cout << _channel_info << "src endpoint timer error: " << ec.message() << "\n";
				continue;
			}
		}
		std::cout << _channel_info << "src send packet finish." << seq << "\n";

		_timer.expires_from_now(std::chrono::seconds(10));
		_timer.async_wait(yield[ec]);
		if (ec) {
			std::cout << _channel_info << "udp src endpoint timer error: " << ec.message() << "\n";
		}

		for (SendSeqTimeMap::iterator send_it = _send_seq.begin(); send_it != _send_seq.end(); ++send_it) {
			RecvSeqTimeMap::iterator recv_it = _recv_seq.find(send_it->first);
			if (recv_it != _recv_seq.end()) {
				uint64_t elapse = (recv_it->second - send_it->second).total_milliseconds();
				std::cout << _channel_info << "seq[" << send_it->first << "] const: " << elapse << "ms" << "\n";
			}
			else {
				std::cout << _channel_info << "seq[" << send_it->first << "] no found." << "\n";
			}
		}

		std::cout << _channel_info << "udp src endpoint sender finish.\n";
		return true;
	}

	bool handShake(asio::yield_context yield, boost::system::error_code& ec)
	{
		uint32_t bytes = 0;
		while (_started && _err_cnt < ERR_ALLOW_CNT) {
			_timer.expires_from_now(std::chrono::seconds(3));
			_timer.async_wait(yield[ec]);
			if (ec) {
				_err_cnt++;
				std::cout << _channel_info << "src endpoint timer error: " << ec.message() << "\n";
				continue;
			}

			bytes = _socket.async_send(asio::buffer(_handshake_byte), yield[ec]);
			if (ec || bytes <= 0) {
				_err_cnt++;
				std::cout << _channel_info << "src endpoint send handshake failed: " << ec.message() << "\n";
				continue;
			}

			bytes = _socket.async_receive(asio::buffer(_readbuf), yield[ec]);
			if (ec || bytes <= 0) {
				_err_cnt++;
				std::cout << _channel_info << "src endpoint read handshake failed: " << ec.message() << "\n";
				continue;
			}

			std::cout << _channel_info << "src endpoint auth read: " << util::to_hex(_readbuf, bytes) << "\n";
			if (_readbuf[0] != _handshake_byte[0]) {
				_err_cnt++;
				std::cout << _channel_info << "src endpoint handshake invalid: " << (int)_readbuf[0] << "\n";
				continue;
			}
			std::cout << _channel_info << "src endpoint openned.\n";
			return true;
		}
		stop();
		return false;
	}

	void reader(asio::yield_context yield) 
	{
		boost::system::error_code ec;
		uint32_t seq = 0;
		while (_started && _err_cnt < ERR_ALLOW_CNT) {
			uint32_t nread = _socket.async_receive(asio::buffer(_readbuf), yield[ec]);
			if (ec || nread <= 0) {
				_err_cnt++;
				std::cout << _channel_info << "src endpoint reader failed: " << ec.message() << "\n";
				continue;
			}

			_recv_packs++;
			_recv_bytes += nread;
			memcpy(&seq, _readbuf, 4);
			boost::posix_time::ptime time_now = boost::posix_time::microsec_clock::universal_time();
			_recv_seq.insert(RecvSeqTimeMap::value_type(seq, time_now));
			//std::cout << _channel_info << "seq[" << seq << "] const: " << e << "ms" << "\n";
		}
		stop();
	}

	void displayer(asio::yield_context yield)
	{
		boost::system::error_code ec;
		while (_started) {
			_display_timer.expires_from_now(std::chrono::seconds(10));
			_display_timer.async_wait(yield[ec]);
			std::cout << _channel_info 
				<< " RX packets(" << _send_packs << "): " << _send_bytes
				<< " Bytes | TX packages(" << _recv_packs << "): " << _recv_bytes << " Bytes.\n";
		}
		stop();
	}

private:
	enum { LOOP_CNT = 1001 };
	enum {
		RAND_TIME_SEND,
		ECHO_SEND
	};

	asio::steady_timer	_timer;
	asio::steady_timer	_display_timer;

	typedef boost::unordered_map<uint32_t, 
		boost::posix_time::ptime> SendSeqTimeMap;
	typedef boost::unordered_map<uint32_t, 
		boost::posix_time::ptime> RecvSeqTimeMap;
	
	SendSeqTimeMap _send_seq;
	RecvSeqTimeMap _recv_seq;

	uint64_t _send_packs;
	uint64_t _send_bytes;
	uint64_t _recv_packs;
	uint64_t _recv_bytes;

	uint8_t	_mode;
	typedef struct {
		uint32_t id;
		char	 data[BUFF_SIZE];
	}data_st;
};