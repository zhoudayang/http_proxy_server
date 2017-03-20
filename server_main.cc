#include <muduo/net/EventLoop.h>
#include <muduo/base/Logging.h>
#include "proxy_server.h"

using namespace zy;

int main()
{

  LOG_INFO << "pid = " << ::getpid();

  muduo::net::EventLoop loop;
  proxy_server server(&loop, muduo::net::InetAddress("127.0.0.1", 8967));
  server.start();

  loop.loop();

  return 0;
}