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

// todo : 提升稳定性
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
  const static int MAX_TIMEOUT = 0;
  // max ttl
  const static int TTL = 500;

  class Entry
  {
   public:
    Entry(const ResolveCallback& cb, const std::string& domain_name, bool ipv6, uint8_t count, muduo::net::TimerId timerId)
        : resolveCallback_(cb), domain_(domain_name), ipv6_(ipv6), count_(count), timerId_(timerId)
    {
      assert(resolveCallback_);
    }

    bool ipv6() const { return  ipv6_; }
    uint8_t count() const { return count_; }
    muduo::net::TimerId timerId() { return timerId_; }
    void resolveCb(const muduo::net::InetAddress& addr) { assert(resolveCallback_); resolveCallback_(addr); }
    void set_timer_id(const muduo::net::TimerId& timerId) { timerId_ = timerId; }
    int add_count_and_get() { return ++ count_; }
    std::string domain() const { return domain_; }

   private:
    ResolveCallback resolveCallback_; // resolve callback function
    std::string domain_;              // domain name
    bool ipv6_;                      //  ipv6 ?
    uint8_t count_;                   // retry count
    muduo::net::TimerId timerId_;     // timeout id
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

  void resolve(uint16_t transaction_id);

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
  std::unordered_map<uint16_t, std::shared_ptr<Entry> > dns_datas_;
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