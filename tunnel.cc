#include "tunnel.h"

#include <muduo/net/EventLoop.h>
#include <muduo/base/Logging.h>
#include <boost/bind.hpp>

using namespace zy;

Tunnel::Tunnel(muduo::net::EventLoop *loop,
               const muduo::net::InetAddress &addr,
               const Tunnel::TcpConnectionPtr &serverCon,
               bool https)
  : loop_(loop),
    client_(loop_, addr, "proxy_client"),
    serverCon_(serverCon),
    timerId_(),
    host_addr_(addr.toIpPort()),
    timeout_(3), // default timeout is 3 seconds
    https_(https),
    request_()
{

}

void Tunnel::onConnection(const Tunnel::TcpConnectionPtr &con) {
  LOG_DEBUG << con->name() << " " << (con->connected() ? "up" : "down");
  if(con->connected())
  {
    LOG_INFO << "proxy built ! " << serverCon_->peerAddress().toIp() << " <-> " << con->peerAddress().toIpPort();
    if(timerId_)
    {
      loop_->cancel(*timerId_);
      timerId_.reset();
    }
    con->setTcpNoDelay(true);
    con->setHighWaterMarkCallback(boost::bind(&Tunnel::onHighWaterMarkWeak, boost::weak_ptr<Tunnel>(shared_from_this()), kClient, _1, _2), 1024 * 1024);
    serverCon_->setContext(con);
    clientCon_ = con;
    serverCon_->startRead();
    // 是否是https代理
    if(https_)
    {
      onHttpsConnection();
    }
    else
    {
      if(!request_.empty())
        con->send(request_.c_str());
    }
    if(onTransportCallback_)
    {
      onTransportCallback();
    }
  }
  else
  {
    teardown();
  }
}

void Tunnel::setup()
{
  client_.setConnectionCallback(boost::bind(&Tunnel::onConnection, this, _1));
  client_.setMessageCallback(boost::bind(&Tunnel::onMessage, this, _1, _2, _3));
  serverCon_->setHighWaterMarkCallback(
      boost::bind(&Tunnel::onHighWaterMarkWeak, boost::weak_ptr<Tunnel>(shared_from_this()), kServer, _1, _2), 1024 * 1024);
  auto timer = loop_->runAfter(timeout_, boost::bind(&Tunnel::onTimeoutWeak, boost::weak_ptr<Tunnel>(shared_from_this())));
  timerId_.reset(new muduo::net::TimerId(timer));
}

void Tunnel::teardown()
{
  client_.setConnectionCallback(muduo::net::defaultConnectionCallback);
  client_.setMessageCallback(muduo::net::defaultMessageCallback);
  if(serverCon_)
  {
    serverCon_->setContext(boost::any());
    serverCon_->shutdown();
  }
  clientCon_.reset();
}

// forward to proxy client directly
void Tunnel::onMessage(const Tunnel::TcpConnectionPtr &con, muduo::net::Buffer *buf, muduo::Timestamp)
{
  LOG_DEBUG << "message from " << host_addr_ << " " << buf->readableBytes();
  if(serverCon_){
    serverCon_->send(buf);
    buf->retrieveAll();
  }
  else
  {
    teardown();
  }
}

void Tunnel::onHighWaterMark(Tunnel::ServerClient which, const Tunnel::TcpConnectionPtr &con, size_t bytes_to_sent)
{
  LOG_INFO << (which == kServer ? "server" : "client")
           << " onHighWaterMark " << con->name() << " bytes " << bytes_to_sent;
  if(which == kServer)
  {
    if(serverCon_->outputBuffer()->readableBytes() > 0)
    {
      clientCon_->stopRead();
      serverCon_->setWriteCompleteCallback(boost::bind(&Tunnel::onWriteCompleteWeak,
      boost::weak_ptr<Tunnel>(shared_from_this()), kServer, _1));
    }
  }
  else
  {
    if(clientCon_->outputBuffer()->readableBytes() > 0)
    {
      serverCon_->stopRead();
      clientCon_->setWriteCompleteCallback(boost::bind(&Tunnel::onWriteCompleteWeak,
      boost::weak_ptr<Tunnel>(shared_from_this()), kClient, _1));
    }
  }
}

void Tunnel::onHttpsConnection()
{
  static muduo::string response("HTTP/1.1 200 Connection established\r\nProxy-Agent: zy_https/0.1\r\n\r\n");
  serverCon_->send(response.c_str());
}

void Tunnel::onWriteComplete(Tunnel::ServerClient which, const Tunnel::TcpConnectionPtr &con)
{
  LOG_INFO << (which == kServer ? "server" : "client")
           << " onWriteComplete " << con->name();
  if(which == kServer)
  {
    clientCon_->startRead();
    serverCon_->setWriteCompleteCallback(muduo::net::WriteCompleteCallback());
  }
  else
  {
    serverCon_->startRead();
    clientCon_->setWriteCompleteCallback(muduo::net::WriteCompleteCallback());
  }
}

void Tunnel::onTimeout()
{
  LOG_ERROR << "connect to " << host_addr_ << " timeout!";
  if(serverCon_)
  {
    static muduo::string response("HTTP/1.1 504 Gateway Timeout\r\nProxy-Agent: zy_https/0.1\r\n\r\n");
    serverCon_->send(response.c_str());
    client_.stop();
    teardown();
  }
}

void Tunnel::onHighWaterMarkWeak(const boost::weak_ptr<Tunnel> &wkTunnel,
                                 Tunnel::ServerClient which,
                                 const Tunnel::TcpConnectionPtr &con,
                                 size_t bytes_to_sent)
{
  auto tunnel = wkTunnel.lock();
  if(tunnel)
    tunnel->onHighWaterMarkWeak(tunnel, which, con, bytes_to_sent);
}

void Tunnel::onWriteCompleteWeak(const boost::weak_ptr<Tunnel> &wkTunnel,
                                 Tunnel::ServerClient which,
                                 const Tunnel::TcpConnectionPtr &con)
{
  auto tunnel = wkTunnel.lock();
  if(tunnel)
    tunnel->onWriteComplete(which, con);
}

void Tunnel::onTimeoutWeak(const boost::weak_ptr<Tunnel> &wkTunnel)
{
  auto tunnel = wkTunnel.lock();
  if(tunnel)
    tunnel->onTimeout();
}
