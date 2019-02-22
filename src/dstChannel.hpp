#pragma once

#include "channel.hpp"

class CDstChannel : public CChannel<CDstChannel>
{
public:
	typedef boost::shared_ptr<CDstChannel> self_type;

	CDstChannel(asio::io_context& io, asio::ip::udp::endpoint& ep, 
		uint32_t id, uint32_t src_id, uint32_t dst_id)
		: CChannel(io, ep, id, src_id, dst_id)
		, _timer(io)
		, _display_timer(io)
		, _echo_packs(0)
		, _echo_bytes(0)
	{
		sprintf(_handshake_byte, "%c", DST_HANDSHAKE_BYTE);
	}

	virtual ~CDstChannel()
	{
		std::cout << _channel_info << "dst endpoint destroy.\n";
	}

	static self_type start(asio::io_context& io, asio::ip::udp::endpoint& ep, 
		uint32_t id, uint32_t src_id, uint32_t dst_id)
	{
		self_type this_(new CDstChannel(io, ep, id, src_id, dst_id));
		this_->go();
		return this_;
	}

	void go()
	{
		asio::spawn(_strand, boost::bind(&CDstChannel::sender, shared_from_this(), boost::placeholders::_1));
		asio::spawn(_strand, boost::bind(&CDstChannel::displayer, shared_from_this(), boost::placeholders::_1));
	}

	void hearbeat(asio::yield_context yield)
	{
	}

	virtual bool channStart(asio::yield_context yield, boost::system::error_code& ec)
	{
		if (!handShake(yield, ec)) {
			std::cout << _channel_info << "dst endpoint handshake failed: " << ec.message() << "\n";
			stop();
			return false;
		}

		std::cout << _channel_info << "dst endpoint openned." << "\n";

		while (_started) {
			uint32_t bytes = _socket.async_receive(asio::buffer(_readbuf, BUFF_SIZE), yield[ec]);
			if (ec || bytes <= 0) {
				_err_cnt++;
				std::cout << _channel_info << "dst endpoint read failed: " << ec.message() << "\n";
				continue;
			}
			
			if (_echo_packs % 10 == 0) {
				uint32_t send_bytes = _socket.async_send(asio::buffer(_readbuf, bytes), yield[ec]);
				if (ec || send_bytes != bytes) {
					_err_cnt++;
					std::cout << _channel_info << "dst endpoint echo failed: " << ec.message() << "\n";
					continue;
				}
			}
			
			_echo_packs++;
			_echo_bytes += bytes;
		}
		stop();
		return false;
	}

	virtual bool handShake(asio::yield_context yield, boost::system::error_code& ec)
	{
		uint32_t bytes = _socket.async_send(asio::buffer(_handshake_byte), yield[ec]);
		if (ec || bytes <= 0) {
			_err_cnt++;
			std::cout << _channel_info << "dst endpoint send handshake failed: " << ec.message() << "\n";
			return false;
		}

		bytes = _socket.async_receive(asio::buffer(_readbuf, BUFF_SIZE), yield[ec]);
		if (ec || bytes <= 0) {
			_err_cnt++;
			std::cout << _channel_info << "dst endpoint read handshake failed: " << ec.message() << "\n";
			return false;
		}

		if (_readbuf[0] != _handshake_byte[0]) {
			_err_cnt++;
			std::cout << _channel_info << "dst endpoint handshake no success: " << (int)_readbuf[0] << "\n";
			return false;
		}
		return true;
	}

	void displayer(asio::yield_context yield)
	{
		boost::system::error_code ec;
		uint64_t echo_packs_prev = 0;
		uint64_t echo_bytes_prev = 0;
		while (_started) {
			_display_timer.expires_from_now(std::chrono::seconds(10));
			_display_timer.async_wait(yield[ec]);
			std::cout << _channel_info
				<< " echo packets: " << _echo_packs << "(" << (_echo_packs - echo_packs_prev) / 10 << " P/S)"
				<< " Bytes:  " << util::formatBytes(_echo_bytes) << "("
				<< util::formatBytes((_echo_bytes - echo_bytes_prev)/10)
				<< "/S).\n"
				;
			echo_packs_prev = _echo_packs;
			echo_bytes_prev = _echo_bytes;
		}
		stop();
	}

private:
	asio::steady_timer _timer;
	char _handshake_byte[2];
	asio::steady_timer	_display_timer;

	uint64_t _echo_packs;
	uint64_t _echo_bytes;
};