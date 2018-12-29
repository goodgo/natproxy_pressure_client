#include "pch.h"
#include "session.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/random.hpp>
#include "sessionMgr.hpp"

int main(char argc, char* argv[]) {
	
	if (argc < 5)
		return 0;

	std::string addr = argv[1];
	uint16_t port = (uint16_t)atoi(argv[2]);
	uint32_t threads = (uint32_t)atoi(argv[3]);
	uint32_t sessions = (uint32_t)atoi(argv[4]);
	std::string directory = argv[5];
	int dir = (directory == "src" ? 0 : (directory == "dst" ? 1 : 2));

	srand(time(NULL));


	CSessionMgr mgr(addr, port, threads, sessions, dir);
	mgr.start();
	std::cerr << "main exit..........." << "\n";

	return 0;
}