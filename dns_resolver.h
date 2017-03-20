#pragma once

#include <boost/noncopyable.hpp>
#include <muduo/net/Buffer.h>
#include <muduo/base/Timestamp.h>
#include <boost/function.hpp>
#include <string>
#include <unordered_map>
#include <muduo/net/TimerId.h>
#include <muduo/net/InetAddress.h>
#include <stdint.h>
#include <boost/circular_buffer.hpp>
#include <unordered_set>
#include <muduo/base/Mutex.h>

namespace muduo
{
namespace net
{
class Channel;
class EventLoop;
}
}

namespace zy
{
class dns_resolver : boost::noncopyable
{
 public:

  typedef boost::function<void(const muduo::net::InetAddress& addr)> ResolveCallback;

  explicit dns_resolver(muduo::net::EventLoop* loop, double timeout = 2);

  // 存在可能无法resolve, transaction ID 已经用完, 支持对ipv6地址的查找
  // may run the callback function during this function
  bool resolve(const std::string& host, const ResolveCallback&, bool ipv6 = false);

 private:
  // 最大重试次数
  const static int MAX_TIMEOUT = 3;
  // max ttl
  const static int TTL = 500;

  struct Entry
  {
    muduo::net::TimerId timerId;     // timeout id
    ResolveCallback resolveCallback; // resolve callback function
    std::string domain;              // domain name
    bool ipv6;                      //  ipv6 ?
    uint8_t count;                   // retry count
  };

  struct AF_INET_Entry
  {
    AF_INET_Entry(struct sockaddr_in addr)
        :addr4(addr)
    {}

    struct sockaddr_in addr4;
  };

  struct AF_INET6_Entry
  {
    AF_INET6_Entry(struct sockaddr_in6 addr)
        : addr6(addr)
    { }

    struct sockaddr_in6 addr6;
  };

  typedef std::weak_ptr<AF_INET_Entry> WkV4EntryPtr;
  typedef std::weak_ptr<AF_INET6_Entry> WkV6EntryPtr;

  typedef std::shared_ptr<AF_INET_Entry> V4EntryPtr;
  typedef std::shared_ptr<AF_INET6_Entry> V6EntryPtr;

  typedef std::unordered_set<V4EntryPtr> V4Bucket;
  typedef std::unordered_set<V6EntryPtr> V6Bucket;

  // function to process read from sockfd_
  void handleRead(muduo::Timestamp receiveTime);

  // call by handleRead function
  void MessageCallback(muduo::Timestamp receiveTime);

  void resolve(uint16_t transaction_id, const struct Entry& entry);

  // function to process write to sockfd_
  void handleWrite();

  void sendInLoop(const void* data, size_t len);

  void send(muduo::net::Buffer* buf);

  void handleTimeout(uint16_t transaction_id);

  void handleError();

  void onTimer();

  int sockfd_;

  muduo::net::EventLoop* loop_;
  muduo::net::Channel* channel_;
  std::unordered_map<uint16_t, struct Entry> dns_datas_;
  muduo::net::Buffer inputBuffer_;
  muduo::net::Buffer outputBuffer_;
  double timeout_;
  muduo::MutexLock mutex_;
  boost::circular_buffer<V4Bucket> v4_buffers_;
  boost::circular_buffer<V6Bucket> v6_buffers_;
  std::unordered_map<std::string,  WkV4EntryPtr> v4_datas_;
  std::unordered_map<std::string, WkV6EntryPtr> v6_datas_;
};
}