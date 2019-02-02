#pragma once

#include <vector>
#include "session.hpp"
#include "ioContextPool.hpp"

class CSessionMgr
{
public:
	CSessionMgr(std::string addr, uint16_t port, size_t threads, size_t sessions, int dir, bool manual_mode = true)
		: _io_context_pool(threads)
		, _timer(_io_context_pool.getIoContext())
		, _ep(boost::asio::ip::address::from_string(addr), port)
		, _thread_cnt(threads)
		, _session_cnt(sessions)
		, _id(0)
		, _dir(dir)
		, _manual_mode(manual_mode)
	{
	
	}

	bool start()
	{
		std::cout << "session manager start: [" << _ep << "] thread count: " << _thread_cnt
			<< " | sessions count: " << _session_cnt << " | dir: " << _dir << std::endl;
		
		boost::asio::spawn(_io_context_pool.getIoContext(),
			boost::bind(&CSessionMgr::startSession, this, boost::placeholders::_1));
		
		_io_context_pool.start();
		return true;
	}

	void startSession(boost::asio::yield_context yield)
	{
		boost::system::error_code ec;
		for (size_t i = 0; i < _session_cnt; i++)
		{
			_timer.expires_after(std::chrono::milliseconds(300));
			_timer.async_wait(yield[ec]);
			CSession::self_type s = CSession::NewSession(_io_context_pool.getIoContext(), _ep, i, _dir, _manual_mode);
			_sessions.push_back(s);
		}
	}
private:
	CIoContextPool _io_context_pool;
	boost::shared_ptr<boost::thread> _thread;
	boost::asio::steady_timer _timer;
	std::vector<CSession::self_type> _sessions;
	boost::asio::ip::tcp::endpoint _ep;
	size_t _thread_cnt;
	size_t _session_cnt;
	uint32_t _id;
	int _dir;
	bool _manual_mode;
};