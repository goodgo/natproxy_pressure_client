#include "pch.h"
#include "session.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/random.hpp>

void StarSession(boost::asio::io_context& io, boost::asio::ip::tcp::endpoint ep, int mode, uint32_t id) {
	CSession::NewSession(io, ep, mode, id);
}

int main(char argc, char* argv[]) {
	srand(time(NULL));

	int mode = argc < 2 ? 0 : !!(int)atoi(argv[1]);
	std::cout << "use mode: " << mode << ", " << (mode == 0 ? "source endpoint." : "destination endpoint.") << std::endl;

	boost::asio::io_context io;
	boost::asio::steady_timer timer(io);

	boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address::from_string(SERVER_IP), 10001);
	for (uint32_t i = 1; i < 2; i++)
	{
		timer.expires_after(std::chrono::milliseconds(3));
		timer.async_wait(boost::bind(StarSession, boost::ref(io), ep, mode, i));
	}
		
	io.run();
	return 0;
}