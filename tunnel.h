#pragma once

#include <muduo/net/TcpClient.h>
#include <boost/noncopyable.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TimerId.h>

namespace zy
{

class Tunnel : boost::noncopyable, public boost::enable_shared_from_this<Tunnel>
{
 public:
  typedef muduo::net::TcpConnectionPtr TcpConnectionPtr;
  typedef boost::function<void()> onTransportCallback;

  Tunnel(muduo::net::EventLoop* loop, const muduo::net::InetAddress& addr,
         const TcpConnectionPtr& serverCon, bool https = false);

  void set_request(const std::string & request) { request_ = request; }

  void set_timeout(double timeout) { timeout_ = timeout; }

  void setTransportCallback(const onTransportCallback& cb)
  {
    onTransportCallback_ = cb;
  }

  void setup();

  void connect() { client_.connect(); }

  void onConnection(const TcpConnectionPtr& con);

  void onMessage(const TcpConnectionPtr& con, muduo::net::Buffer* buf, muduo::Timestamp);

 private:
  enum ServerClient
  {
    kServer,
    kClient
  };

  void teardown();

  void onHighWaterMark(ServerClient which, const TcpConnectionPtr& con, size_t bytes_to_sent);

  void onWriteComplete(ServerClient which, const TcpConnectionPtr& con);

  void onTimeout();

  static void onHighWaterMarkWeak(const boost::weak_ptr<Tunnel>& wkTunnel, ServerClient which,
                                  const TcpConnectionPtr& con, size_t bytes_to_sent);

  static void onWriteCompleteWeak(const boost::weak_ptr<Tunnel>& wkTunnel, ServerClient which,
                                   const TcpConnectionPtr& con);

  static void onTimeoutWeak(const boost::weak_ptr<Tunnel>& wkTunnel);

  void onHttpsConnection();

  muduo::net::EventLoop* loop_;
  muduo::net::TcpClient client_;
  TcpConnectionPtr serverCon_;
  TcpConnectionPtr clientCon_;
  onTransportCallback onTransportCallback_;
  std::unique_ptr<muduo::net::TimerId> timerId_;
  muduo::string host_addr_;
  double timeout_;
  bool https_;
  std::string request_;
};
typedef boost::shared_ptr<Tunnel> TunnelPtr;
}