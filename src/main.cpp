#include "pch.h"
#include "session.hpp"


#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/random.hpp>
#include "sessionMgr.hpp"
#include "config.hpp"

int main(char argc, char* argv[]) {

	if (!gConfig.load(argc, argv))
		return 0;

	CSessionMgr mgr(
		gConfig.srvAddr(),
		gConfig.srvPort(),
		gConfig.threadNum(),
		gConfig.sessionNum(),
		gConfig.endpoint(),
		gConfig.automation()
	);

	mgr.start();
	std::cerr << "main exit..........." << "\n";

	return 0;
}