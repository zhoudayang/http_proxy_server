#include "proxy_server.h"
#include "http_header.h"

#include <muduo/base/Logging.h>
#include <boost/bind.hpp>

using namespace zy;

namespace impl
{

// find end of header
const char* findEOH(const muduo::net::Buffer* buf)
{
  static std::string eoh("\r\n\r\n");
  const char* crlf = std::search(buf->peek(), buf->beginWrite(), eoh.begin(), eoh.end());
  return crlf == buf->beginWrite() ? nullptr : crlf;
}

// on convert error, return -1
int get_content_length(const std::string& value)
{
  try {
    return std::stoi(value);
  }catch(...)
  {
    return -1;
  }
}
}

proxy_server::proxy_server(muduo::net::EventLoop *loop, const muduo::net::InetAddress &addr)
  : loop_(loop),
    server_(loop_, addr, "proxy_server"),
    resolver_(loop_),
    con_states_(),
    tunnels_()
{
  server_.setConnectionCallback(boost::bind(&proxy_server::onConnection, this, _1));
  server_.setMessageCallback(boost::bind(&proxy_server::onMessage, this, _1, _2, _3));
}

bool proxy_server::is_valid_addr(const muduo::net::InetAddress &addr) {
  return addr.ipNetEndian() != INADDR_ANY;
}

void proxy_server::onConnection(const muduo::net::TcpConnectionPtr &con)
{
  LOG_DEBUG << "connection from " << con->peerAddress().toIpPort() << " is " << (con->connected() ? "up" : "down");
  auto name = con->name();
  if(con->connected())
  {
    con_states_[name] = kStart;
    con->setTcpNoDelay(true);
  }
  else
  {
    clean_from_container(name);
  }
}

void proxy_server::clean_from_container(const muduo::string &con_name)
{
  auto it = con_states_.find(con_name);
  if(it != con_states_.end())
    con_states_.erase(it);
  auto iter = tunnels_.find(con_name);
  if(iter != tunnels_.end())
    tunnels_.erase(iter);
}

void proxy_server::onMessage(const muduo::net::TcpConnectionPtr &con, muduo::net::Buffer *buf, muduo::Timestamp)
{
  auto name = con->name();
  if(!con_states_.count(name))
  {
    LOG_FATAL << "can't find state of specified con_name " << name;
  }
  auto& state = con_states_[name];
  // 此处需要解析http头或者connect 头
  if(state == kStart)
  {
    if(impl::findEOH(buf) != nullptr)
    {
      muduo::net::Buffer buffer(*buf);
      const char * begin = buffer.peek();
      const char * end = nullptr;
      http_request request;
      while((end = buffer.findCRLF()) != nullptr)
      {
        std::string line(begin, end);
        if(!line.empty())
        {
          bool ret;
          if(request.initialized())
            ret = request.add_header(line);
          else
            ret = request.init_request(line);
          if(!ret)
          {
            LOG_ERROR << "error http header " << name;
            buf->retrieveAll();
            onHeaderError(con);
            return;
          }
        }
        buffer.retrieveUntil(end + 2);
        begin = buffer.peek();
      }
      if(!request.valid())
      {
        LOG_ERROR << "invalid http request " << name;
        buf->retrieveAll();
        onHeaderError(con);
        return;
      }
      std::string content_length = request.get_header("Content-Length");
      int length = 0;
      if(content_length.empty())
        content_length = "0";
      length = impl::get_content_length(content_length);
      if(length == -1)
      {
        LOG_ERROR << "invalid Content-Length " << name;
        buf->retrieveAll();
        onHeaderError(con);
        return;
      }
      if(static_cast<int>(buffer.readableBytes()) >= length)
      {
        // got all http request, stop read, forbid execute onMessage function again
        con->stopRead();
        request.set_content(std::string(buffer.peek(), buffer.peek() + length));
        buffer.retrieve(length);
        begin = buf->peek();
        end = nullptr;
        while((end = buf->findCRLF()) != nullptr)
        {
          buf->retrieveUntil(end + 2);
          begin = buf->peek();
        }
        buf->retrieve(length);
        state = kGotRequest;
        uint16_t port = request.port();
        std::string domain_name = request.domain_name();
        if(request.method() != "CONNECT")
        {
          std::string request_str = request.proxy_request();
          resolver_.resolve(domain_name,
                            boost::bind(&proxy_server::onResolve, this, boost::weak_ptr<muduo::net::TcpConnection>(con), port, request_str, _1), false);
        }
        else
        {
          resolver_.resolve(domain_name,
                            boost::bind(&proxy_server::onResolve, this, boost::weak_ptr<muduo::net::TcpConnection>(con), port, _1), true);
        }
      }
      else
      {
        LOG_INFO << "content data is not complete yet";
        return;
      }
    }
    else
    {
      LOG_INFO << name << " header not complete yet";
      return;
    }
  }
    // 此处需要解析出http头
  else if(state == kTransport_http)
  {
    while(impl::findEOH(buf) != nullptr)
    {
      muduo::net::Buffer buffer(*buf);
      const char* begin = buffer.peek();
      const char* end = nullptr;
      http_request request;
      while((end = buffer.findCRLF()) != nullptr)
      {
        std::string line(begin, end);
        if(!line.empty())
        {
          bool ret;
          if(request.initialized())
            ret = request.add_header(line);
          else
            ret = request.init_request(line);
          if(!ret)
          {
            LOG_ERROR << "error http header " << name;
            buf->retrieveAll();
            onHeaderError(con);
            return;
          }
        }
        buffer.retrieveUntil(end + 2);
        begin = buffer.peek();
      }
      if(!request.valid())
      {
        LOG_ERROR << "invalid http request " << name;
        buf->retrieveAll();
        onHeaderError(con);
        return;
      }
      std::string content_length = request.get_header("Content-Length");
      int length = 0;
      if(content_length.empty())
      {
        content_length = "0";
      }
      length = impl::get_content_length(content_length);
      if(length == -1)
      {
        LOG_ERROR << "invalid Content-Length " << name;
        buf->retrieveAll();
        onHeaderError(con);
        return;
      }
      if(static_cast<int>(buffer.readableBytes()) >= length)
      {
        request.set_content(std::string(buffer.peek(), buffer.peek() + length));
        buffer.retrieve(length);
        begin = buf->peek();
        end = nullptr;
        while((end = buf->findCRLF()) != nullptr)
        {
          buf->retrieveUntil(end + 2);
          begin = buf->peek();
        }
        buf->retrieve(length);
        std::string request_str = request.proxy_request();
        auto clientCon = boost::any_cast<const muduo::net::TcpConnectionPtr&>(con->getContext());
        clientCon->send(request_str.data(), request_str.size());
      }
      else
      {
        LOG_INFO << "content not complete!";
        return;
      }
    }
  }
    // forward all data to proxy server directly
  else if(state == kTransport_https)
  {
    auto clientCon = boost::any_cast<const muduo::net::TcpConnectionPtr&>(con->getContext());
    clientCon->send(buf);
    buf->retrieveAll();
  }
  else
  {
    LOG_ERROR << "unknown connection state!";
    con->shutdown();
  }
}

void proxy_server::onHeaderError(const muduo::net::TcpConnectionPtr &con)
{
  const static muduo::string response("HTTP/1.1 400 Bad Request\r\nProxy-Agent: zy_https/0.1\r\n\r\n");
  con->send(response.c_str());
  con->shutdown();
}

void proxy_server::set_con_state(const muduo::string &con_name, proxy_server::conState state) {
  if(con_states_.count(con_name))
    con_states_[con_name] = state;
}

// 超时统一使用此header进行回复
void proxy_server::onResolveError(const muduo::net::TcpConnectionPtr &con)
{
  const static muduo::string response("HTTP/1.1 504 Gateway Timeout\r\nProxy-Agent: zy_https/0.1\r\n\r\n");
  con->send(response.c_str());
  con->shutdown();
}

void proxy_server::onResolve(const boost::weak_ptr<muduo::net::TcpConnection> wkCon,
                             uint16_t port, const muduo::net::InetAddress &addr)
{
  auto con = wkCon.lock();
  if(!con)
  {
    LOG_DEBUG << "connection is no more exit!";
    return;
  }
  if(!is_valid_addr(addr))
  {
    LOG_INFO << "fail to resolve the address of " << con->name();
    onResolveError(con);
  }
  else
  {
    auto con_name = con->name();
    set_con_state(con_name, kResolved);
    muduo::net::InetAddress address (addr.toIp(), port);
    TunnelPtr tunnel(new Tunnel(loop_, address, con, true));
    tunnel->setTransportCallback(boost::bind(&proxy_server::set_con_state, this, con_name, kTransport_http));
    tunnel->setup();
    tunnel->connect();
    tunnels_[con_name] = tunnel;
  }
}

void proxy_server::onResolve(const boost::weak_ptr<muduo::net::TcpConnection> wkCon,
                             uint16_t port, const std::string &request,
                             const muduo::net::InetAddress &addr)
{
  auto con = wkCon.lock();
  if(!con)
  {
    LOG_DEBUG << "connection is no more exit!";
    return;
  }
  if(!is_valid_addr(addr))
  {
    LOG_INFO << "fail to resolve the address of " << con->name();
    onResolveError(con);
  }
  else {
    auto con_name = con->name();
    set_con_state(con_name, kResolved);
    muduo::net::InetAddress address(addr.toIp(), port);
    TunnelPtr tunnel(new Tunnel(loop_, address, con, false));
    tunnel->setTransportCallback(boost::bind(&proxy_server::set_con_state,this, con_name, kTransport_https));
    tunnel->set_request(request);
    tunnel->setup();
    tunnel->connect();
    tunnels_[con_name] = tunnel;
  }
}
