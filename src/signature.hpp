#pragma once
#include <iostream>

#include <cstdint>
#include <array>
#include <queue>
#include <functional>

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/noncopyable.hpp>
#include <boost/system/error_code.hpp>


class async_condition_variable : 
	public boost::noncopyable
{
public:
	using signature = auto(boost::system::error_code) -> void;

	template <typename CompletionToken>
	using handler_type_t = typename boost::asio::handler_type<CompletionToken, signature>::type;

	template <typename CompletionToken>
	using async_result = typename boost::asio::async_result<handler_type_t<CompletionToken>>;

	template <typename CompletionToken>
	using async_result_t = typename async_result<CompletionToken>::type;
private:
	using handler_block = std::array<uint8_t, 128U>;

	class handler_holder_base :
		public boost::noncopyable
	{
	public:
		handler_holder_base() {}
		virtual ~handler_holder_base() {}

		virtual void exec(boost::system::error_code ec) = 0;
	};

	template <typename CompletionToken>
	class handler_holder :
		public handler_holder_base
	{
	private:
		boost::asio::io_service& _io;
	public:
		handler_type_t<CompletionToken> m_handler;

		handler_holder(boost::asio::io_service& iosv, handler_type_t<CompletionToken>&& handler)
			: _io(iosv), m_handler(std::forward<handler_type_t<CompletionToken>&&>(handler)) {}

		void exec(boost::system::error_code ec)override
		{
			handler_type_t<CompletionToken> handler = std::move(m_handler);
			_io.post([handler, ec]() { boost::asio::asio_handler_invoke(std::bind(handler, ec), &handler); });
		}
	};
private:
	boost::asio::io_service& _io;
	std::queue<size_t> _handlers;

	std::vector<handler_block> _storage;
	std::vector<size_t> _freelist;

public:
	async_condition_variable(boost::asio::io_service& iosv)
		: _io(iosv) {}
	~async_condition_variable()
	{
		while (!_handlers.empty())
		{
			size_t idx = _handlers.front();
			auto holder = reinterpret_cast<handler_holder_base*>(_storage[idx].data());
			_handlers.pop();
			try
			{
				holder->exec(boost::asio::error::make_error_code(boost::asio::error::interrupted));
			}
			catch (...)
			{
				free_handler(idx);
				continue;
			}
			free_handler(idx);
		}
		assert(_freelist.size() == _storage.size());
	}
private:
	template <typename CompletionToken>
	size_t alloc_handler(handler_type_t<CompletionToken>&& handler)
	{
		static_assert(sizeof(handler_holder<CompletionToken>) <= std::tuple_size<handler_block>::value, "handler is too bigger.");

		if (_freelist.empty())
		{
			_storage.emplace_back();
			auto& storage = _storage.back();
			new(storage.data()) handler_holder<CompletionToken>(_io, std::forward<handler_type_t<CompletionToken>&&>(handler));
			return _storage.size() - 1;
		}
		else
		{
			size_t idx = _freelist.back();
			auto& storage = _storage[idx];
			_freelist.pop_back();
			new(storage.data()) handler_holder<CompletionToken>(_io, std::forward<handler_type_t<CompletionToken>&&>(handler));
			return idx;
		}
	}

	void free_handler(size_t idx)
	{
		auto& storage = _storage[idx];
		auto holder = reinterpret_cast<handler_holder_base*>(_storage[idx].data());
		holder->~handler_holder_base();
		_freelist.push_back(idx);
	}
public:
	template <typename CompletionToken>
	async_result_t<CompletionToken> async_wait(CompletionToken&& token)
	{
		handler_type_t<CompletionToken> handler(std::forward<CompletionToken&&>(token));
		async_result<CompletionToken> result(handler);
		size_t idx = alloc_handler<CompletionToken>(std::move(handler));
		auto holder = reinterpret_cast<handler_holder<CompletionToken>*>(_storage[idx].data());
		_handlers.push(idx);
		return result.get();
	}

	void notify_one()
	{
		if (!_handlers.empty())
		{
			size_t idx = _handlers.front();
			auto holder = reinterpret_cast<handler_holder_base*>(_storage[idx].data());
			_handlers.pop();
			try
			{
				holder->exec(boost::system::error_code());
			}
			catch (...)
			{
				free_handler(idx);
				throw;
			}
			free_handler(idx);
		}
	}

	void notify_all()
	{
		while (!_handlers.empty())
			notify_one();
	}
};

