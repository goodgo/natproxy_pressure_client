#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/make_shared.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/thread.hpp>
#include <vector>
#include <list>

class CIoContextPool : public boost::noncopyable
{
public:
	CIoContextPool(size_t pool_size)
		: _index(0)
	{
		for (size_t i = 0; i < pool_size; i++) {
			ContextPtr io = boost::make_shared<boost::asio::io_context>();
			_io_contexts.push_back(io);
			_works.push_back(boost::asio::make_work_guard(*io));
		}
	}

	~CIoContextPool()
	{
	
	}

	void start()
	{
		std::vector<boost::shared_ptr<boost::thread>> _threads;
		for (size_t i = 0; i < _io_contexts.size(); i++) {
			boost::shared_ptr<boost::thread> th(new boost::thread(
				boost::bind(&boost::asio::io_context::run, _io_contexts[i])));

			_threads.push_back(th);
		}

		std::cerr << "io_context pool start....\n";
		for (size_t i = 0; i < _threads.size(); i++) {
			_threads[i]->join();
		}
	}

	boost::asio::io_context& getIoContext()
	{
		boost::asio::io_context& io = *_io_contexts[_index];
		if (++_index >= _io_contexts.size())
			_index = 0;
		return io;
	}

private:
	typedef boost::shared_ptr<boost::asio::io_context> ContextPtr;
	typedef boost::asio::executor_work_guard<boost::asio::io_context::executor_type> WorkGuard;
	
	std::vector<ContextPtr> _io_contexts;
	std::list<WorkGuard> _works;

	size_t _index;
};
