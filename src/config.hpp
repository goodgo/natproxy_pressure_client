#pragma once

#include <iostream>
#include <string>
#include <boost/noncopyable.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

class CConfig : boost::noncopyable
{
public:
	static CConfig& getInstance()
	{
		static CConfig c;
		return c;
	}

	bool load(char argc, char* argv[])
	{
		boost::program_options::options_description desc("allow option");
		desc.add_options()
			("help,h", "help message")
			("addr,a", boost::program_options::value<std::string>(&_srv_addr), "service addr")
			("port,p", boost::program_options::value<uint16_t>(&_srv_port), "service port")
			("endpoint,e", boost::program_options::value<uint32_t>(&_endpoint), "endpoint option(0: source ep, 1: destination ep)")
			("automation,A", boost::program_options::value<bool>(&_automation), "select mode")
			("sessions,S", boost::program_options::value<uint32_t>(&_sessions), "sessions num")
			("threads,T", boost::program_options::value<uint32_t>(&_threads), "threads num")
			("log,l", boost::program_options::value<std::string>(&_log_path), "log path")
			("content,c", boost::program_options::value<std::string>(&_chann_data_content), "channel test content or file path")
			("block,b", boost::program_options::value<uint32_t>(&_chann_data_block), "channel send data block bytes")
			("times,t", boost::program_options::value<uint32_t>(&_chann_test_times), "channel test times")
			("mode,m", boost::program_options::value<uint32_t>(&_chann_mode), "channel test mode(0: interval, 1: random, 2: response)")
			("interval,i", boost::program_options::value<uint32_t>(&_chann_send_interval), "channel test interval(sec)")
			("heartbeat,H", boost::program_options::value<uint32_t>(&_chann_heartbeat_interval), "channel heartbeat interval(sec)")
			;

		boost::program_options::variables_map vm;
		boost::program_options::store(
			boost::program_options::parse_command_line(argc, argv, desc), vm);
		boost::program_options::notify(vm);

		if (vm.count("help")) {
			std::cerr << "help: " << desc << std::endl;
			return false;
		}
		if (vm.count("content")) {
			boost::filesystem::path p(_chann_data_content);
			if (!boost::filesystem::exists(p)) {
				if (boost::filesystem::is_directory(p.parent_path())) {
					std::cerr << "test file: " << _chann_data_content << " not found.\n";
					return false;
				}
			}
			else {
				std::cerr << "channel use string data." << std::endl;
			}
		}

		std::cout << "#############################################" << std::endl;
		std::cout << "addr: " << _srv_addr << std::endl;
		std::cout << "port: " << _srv_port << std::endl;
		std::cout << "automation: " << std::boolalpha << _automation << std::endl;
		std::cout << "endpoint: " << (_endpoint == 0 ? "source" : "destination") << std::endl;
		std::cout << "sessions: " << _sessions << std::endl;
		std::cout << "threads: " << _threads << std::endl;
		std::cout << "log path: " << _log_path << std::endl;
		std::cout << "channel test times: " << _chann_test_times << std::endl;
		std::cout << "channel mode: " << (_chann_mode == 0 ? "Interval" : "Response") << std::endl;
		std::cout << "channel send interval(msec): " << _chann_send_interval << std::endl;
		std::cout << "channel heartbeat interval(sec): " << _chann_heartbeat_interval << std::endl;
		std::cout << "channel data block: " << _chann_data_block << std::endl;
		std::cout << "channel data content: " << _chann_data_content << std::endl;
		std::cout << "#############################################" << std::endl;
		return true;
	}

	std::string srvAddr() { return _srv_addr; }
	uint16_t srvPort() { return _srv_port; }
	bool automation() { return _automation; }
	uint32_t threadNum() { return _threads; }
	uint32_t sessionNum() { return _sessions; }
	uint32_t endpoint() { return _endpoint; }
	std::string logPath() { return _log_path; }
	uint32_t channTestTimes() { return _chann_test_times; }
	uint32_t channMode() { return _chann_mode; }
	uint32_t channSendInterval() { return _chann_send_interval; }
	uint32_t channHeartBeatInterval() { return _chann_heartbeat_interval; }
	uint32_t channDataBlock() { return _chann_data_block; }
	std::string channDataContent() { return _chann_data_content; }

private:
	CConfig()
		: _srv_addr("")
		, _srv_port(0)
		, _automation(false)
		, _threads(1)
		, _sessions(1)
		, _endpoint(0)
		, _log_path("")
		, _chann_test_times(10000)
		, _chann_mode(0)
		, _chann_send_interval(0)
		, _chann_heartbeat_interval(30)
		, _chann_data_block(1400)
		, _chann_data_content("./log.log")
	{}

private:
	std::string _srv_addr;
	uint16_t _srv_port;
	bool _automation; // 自动化
	uint32_t _threads;
	uint32_t _sessions;	
	uint32_t _endpoint;					// 端点(0: 发起端, 1: 目的端)
	std::string _log_path;
	uint32_t _chann_test_times;			// 通道测试次数
	uint32_t _chann_mode;				// 通道发送模式 (0: 间隔发送，1: 收到应答发送)
	uint32_t _chann_send_interval;		// 通道发送间隔(msec) (0 连续发送)
	uint32_t _chann_heartbeat_interval; // 通道心跳间隔(sec)
	uint32_t _chann_data_block;			// 通道数据块大小(Byte)
	std::string _chann_data_content;	// 通道数据内容/文件路径
};

#define gConfig (CConfig::getInstance())