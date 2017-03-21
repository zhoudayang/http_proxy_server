#include <muduo/net/EventLoop.h>
#include <muduo/base/Logging.h>
#include <muduo/base/LogFile.h>
#include <boost/program_options.hpp>
#include <iostream>
#include "proxy_server.h"

using namespace zy;

namespace po = boost::program_options;

std::unique_ptr<muduo::LogFile> g_logFile;

void outputFunc(const char* msg, int len)
{
  g_logFile->append(msg, len);
}

void flushFunc()
{
  g_logFile->flush();
}

void init_log()
{
  g_logFile.reset(new muduo::LogFile("/tmp/zy_https_proxy", 500 * 1024, false, 3, 100));
  muduo::Logger::setOutput(outputFunc);
  muduo::Logger::setLogLevel(muduo::Logger::WARN);
  muduo::Logger::setFlush(flushFunc);
}

int main(int argc, const char* argv[])
{
  po::options_description desc("proxy options");
  desc.add_options()
      ("help,h", "produce help message")
      ("host,h", po::value<muduo::string>(), "bind ip address")
      ("port,p", po::value<uint16_t>(), "listen port");
  po::variables_map value_map;
  po::store(po::parse_command_line(argc, argv, desc), value_map);

  // default bind address is 0.0.0.0
  muduo::string host = "0.0.0.0";
  // default listen port is 8768
  uint16_t port = 8768;

  if(value_map.count("help"))
  {
    std::cout << desc << std::endl;
    exit(0);
  }
  if(value_map.count("host"))
  {
    host = value_map["host"].as<muduo::string>();
  }
  if(value_map.count("port"))
  {
    port = value_map["port"].as<uint16_t>();
  }

  if(daemon(0, 0) == -1)
  {
    fprintf(stderr, "create daemon process error!\n");
    exit(-1);
  }

  init_log();

  LOG_INFO << "zy_https_proxy init complete! pid = " << ::getpid();

  muduo::net::EventLoop loop;
  proxy_server server(&loop, muduo::net::InetAddress(host, port));
  server.start();

  loop.loop();
}