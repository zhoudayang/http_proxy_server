#include "dns_resolver.h"
#include <muduo/base/Logging.h>

#include <muduo/net/SocketsOps.h>
#include <muduo/net/EventLoop.h>
#include <boost/bind.hpp>
#include <muduo/net/Channel.h>

using namespace zy;

namespace impl
{
int createNonblockingUdpOrDie(sa_family_t family)
{
  int sockfd = ::socket(family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
  if(sockfd < 0)
  {
    LOG_FATAL << "zy::createNonblockingOrDie";
  }
  return sockfd;
}

bool convert_host(const std::string& host, muduo::net::Buffer* buf)
{
  std::string label;
  for(auto& ch : host)
  {
    if(std::isalpha(ch) || std::isdigit(ch))
    {
      label.push_back(ch);
    }
    else if( ch == '_')
    {
      if(label.empty())
      {
        LOG_ERROR << "label can't start with " << ch;
        return false;
      }
      else
        label.push_back(ch);
    }
    else if(ch == '.')
    {
      if(label.empty())
      {
        LOG_ERROR << "invalid . pos";
        return false;
      }
      else if(label.back() == '_')
      {
        LOG_ERROR << "label can't stop with " << ch;
        return false;
      }
      else if (label.size() > 63)
      {
        LOG_ERROR << "label can't be longer than 63 bytes";
        return false;
      }
      else
      {
        uint8_t length = static_cast<uint8_t>(label.size());
        buf->appendInt8(length);
        buf->append(label.c_str());
        label.clear();
      }
    }
    else
    {
      LOG_ERROR << "invalid character in label";
      return false;
    }
  }
  if(!label.empty())
  {
    if(label.back() == '_')
    {
      LOG_ERROR << "label can't stop with " << label.back();
      return false;
    }
    if(label.size() > 63)
    {
      LOG_ERROR << "label can't be longer than 63 bytes";
      return false;
    }
    else
    {
      // 长度是8bit
      uint8_t length = static_cast<uint8_t>(label.size());
      buf->appendInt8(length);
      buf->append(label.c_str());
    }
  }
  //末尾是0
  buf->appendInt8(0);
  return true;
}

// todo: check ptr or domain_name
bool retrieve_name(muduo::net::Buffer* buf)
{
  if(buf->readableBytes() <= 1)
  {
    return false;
  }

  uint8_t label_length;
  while (buf->readableBytes() >= 1 && (label_length = static_cast<uint8_t>(buf->readInt8())) != 0) {
    if((label_length >> 6 & 0x3) == 0x3)
    {
      buf->retrieve(1);
      break;
    }
    if (buf->readableBytes() < label_length) {
      LOG_ERROR << "error label length";
      return false;
    }
    buf->retrieve(label_length);
  }

  return true;
}

}

namespace packet
{

struct flag1
{
  uint8_t flag1 = 0x01;

  uint8_t QR () const { return flag1 >> 7 & 1; }
  uint8_t Opcode() const { return flag1 >> 3 & 0xf; }
  uint8_t AA() const { return flag1 >> 2 & 1; }
  uint8_t RD() const { return flag1 >> 1 & 1; }
  uint8_t RA() const { return flag1 & 1; }
}__attribute__((__packed__));

struct flag2
{
  uint8_t flag2 = 0x00;
  uint8_t RA() const{ return flag2 >> 7 & 1; }
  uint8_t Z() const { return flag2 >> 4 & 0x8; }
  uint8_t RCODE() const { return flag2 & 0xf; }
}__attribute__((__packed__));


struct flag {
  struct flag1 flag1;
  struct flag2 flag2;
}__attribute__((__packed__));


static_assert(sizeof(struct flag) == 2, "error flag size");
static_assert(sizeof(struct flag1) == 1, "errror flag1 size");
static_assert(sizeof(struct flag2) == 1, "error flag2 size");

struct count {
  uint16_t question_count = 0;
  uint16_t answer_count = 0;
  uint16_t ns_count = 0;
  uint16_t ar_count = 0;
}__attribute__((__packed__));

struct query_tail
{
  uint16_t query_type;
  uint16_t query_class;
}__attribute__((__packed__));

}

dns_resolver::dns_resolver(muduo::net::EventLoop *loop, double timeout)
    : sockfd_(impl::createNonblockingUdpOrDie(AF_INET)),
      loop_(loop),
      channel_(new muduo::net::Channel(loop_, sockfd_)),
      dns_datas_(),
      inputBuffer_(),
      outputBuffer_(),
      timeout_(timeout),
      mutex_(),
      v4_buffers_(TTL),
      v6_buffers_(TTL),
      v4_datas_(),
      v6_datas_()
{
  // 系统内置dns在127.0.1.1上面监听
  muduo::net::InetAddress local_dns("127.0.1.1", 53, false);

  // connect error, fatal
  // bind to local dns server
  if(muduo::net::sockets::connect(sockfd_, local_dns.getSockAddr()) == -1)
  {
    LOG_FATAL << "connect to local dns server error! " << ::strerror(errno);
  }

  v4_buffers_.resize(TTL);
  v6_buffers_.resize(TTL);

  channel_->setReadCallback(boost::bind(&dns_resolver::handleRead, this, _1));
  channel_->setWriteCallback(boost::bind(&dns_resolver::handleWrite, this));
  channel_->setWriteCallback(boost::bind(&dns_resolver::handleError, this));
  // enable reading from socket
  channel_->enableReading();
  loop_->runEvery(1.0, boost::bind(&dns_resolver::onTimer, this));
}

bool dns_resolver::resolve(const std::string &host, const dns_resolver::ResolveCallback& cb, bool ipv6)
{
  if(host.size() > 255)
  {
    LOG_ERROR << "domain length is over " << 255;
    return false;
  }
  if(dns_datas_.size() + 1 >= UINT16_MAX)
  {
    LOG_ERROR << "dns_datas_ is full!"; // fixme: fatal instead ?
    return false;
  }
  {
    muduo::MutexLockGuard lock(mutex_);
    if(ipv6)
    {
      auto it = v6_datas_.find(host);
      if(it != v6_datas_.end())
      {
        auto entry = (it->second).lock();
        if(entry){
          cb(muduo::net::InetAddress(entry->addr6));
          return true;
        }
        else
        {
          v6_datas_.erase(it);
        }
      }
    }
    else
    {
      auto it = v4_datas_.find(host);
      if(it != v4_datas_.end())
      {
        auto entry = (it->second).lock();
        if(entry) {
          cb(muduo::net::InetAddress(entry->addr4));
          return true;
        }
        else
        {
          v4_datas_.erase(it);
        }
      }
    }
  }
  muduo::net::Buffer buf;
  uint16_t transaction_id = static_cast<uint16_t>(dns_datas_.size() + 1);
  buf.appendInt16(transaction_id);
  struct packet::flag query;
  buf.append(&query, sizeof(query));
  struct packet::count query_count;
  query_count.question_count = muduo::net::sockets::hostToNetwork16(1);
  buf.append(&query_count, sizeof(query_count));
  if(!impl::convert_host(host, &buf))
    return false;
  uint16_t query_class = 1;
  u_int16_t query_type = (ipv6 ? 28 : 1);
  buf.appendInt16(query_type);
  buf.appendInt16(query_class);
  auto timer_id = loop_->runAfter(timeout_, boost::bind(&dns_resolver::handleTimeout, this, transaction_id));
  dns_datas_[transaction_id] =  std::make_shared<Entry>(cb, host, ipv6, 1, timer_id);
  send(&buf);
  return true;
}

// never come here ?
void dns_resolver::handleWrite()
{
  loop_->assertInLoopThread();
  if(channel_->isWriting())
  {
    ssize_t n = muduo::net::sockets::write(channel_->fd(), outputBuffer_.peek(), outputBuffer_.readableBytes());
    if(n > 0)
    {
      outputBuffer_.retrieve(n);
      // remain no data, disable writing
      if(outputBuffer_.readableBytes() == 0)
      {
        channel_->disableWriting();
      }
    }
    else
    {
      LOG_SYSERR << "dns_channel::handleWrit";
    }
  }
}

// send function, must call in loop thread
// fixme: change here ?
void dns_resolver::sendInLoop(const void *data, size_t len)
{
  loop_->assertInLoopThread();
  ssize_t nwrote = 0;
  size_t remain = len;
  bool faultError = false;
  if(!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
  {
    nwrote = muduo::net::sockets::write(channel_->fd(), data, len);
    if(nwrote >= 0)
    {
      remain = len - nwrote;
    }
    else
    {
      nwrote = 0;
      if(errno != EWOULDBLOCK)
      {
        LOG_SYSERR << "TcpConnection::sendInLoop " << errno << " : " << muduo::strerror_tl(errno) ;
        // msg size if too big -> EMSGSIZE
        if(errno == EPIPE || errno == ECONNRESET || errno == EMSGSIZE)
          faultError = true;
      }
    }
  }
  assert(remain <= len);
  if(!faultError && remain > 0)
  {
    outputBuffer_.append(static_cast<const char*>(data) + nwrote, remain);
    if(!channel_->isWriting())
    {
      channel_->enableWriting();
    }
  }
}

void dns_resolver::send(muduo::net::Buffer *buf)
{
  if(loop_->isInLoopThread())
  {
    sendInLoop(buf->peek(), buf->readableBytes());
    buf->retrieveAll();
  }
  else
    loop_->runInLoop(boost::bind(&dns_resolver::sendInLoop, this, buf->peek(), buf->readableBytes()));
}

void dns_resolver::handleRead(muduo::Timestamp receiveTime)
{
  loop_->assertInLoopThread();
  int savedErrno = 0;
  // buffer size of 65536, big enough for udp packet
  ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
  if(n > 0)
  {
    MessageCallback(receiveTime);
  }
  else if(n == 0)
  {
    LOG_SYSERR << "empty udp packet";
  }
  else if (n < 0)
  {
    errno = savedErrno;
    LOG_SYSERR << "dns_channel::handleRead";
  }
  // only store one packet
  inputBuffer_.retrieveAll();
}

//fixme: right way ?
void dns_resolver::MessageCallback(muduo::Timestamp receiveTime) {
  // never happen
  if (inputBuffer_.readableBytes() < 12) {
    LOG_ERROR << "not a valid dns response packet!";
    return;
  }
  uint16_t transaction_id = static_cast<uint16_t > (inputBuffer_.readInt16());
  if (!dns_datas_.count(transaction_id)) {
    LOG_ERROR << "can't find specified transaction id " << transaction_id;
    return;
  }
  auto entry = dns_datas_[transaction_id];
  loop_->cancel(entry->timerId());
  dns_datas_.erase(transaction_id);
  struct packet::flag flag;
  memcpy(&flag, inputBuffer_.peek(), sizeof(flag));
  inputBuffer_.retrieveInt16();
  // fixme: truncated use tcp to query dns ?
  if (flag.flag1.QR() != 0x01 || flag.flag2.RCODE() != 0 || flag.flag1.RD() != 0) {
    entry->resolveCb(muduo::net::InetAddress());
    return;
  }
  uint16_t question_count = static_cast<uint16_t>(inputBuffer_.readInt16());
  uint16_t answer_count = static_cast<uint16_t>(inputBuffer_.readInt16());
  uint16_t ns_count = static_cast<uint16_t>(inputBuffer_.readInt16());
  uint16_t ar_count = static_cast<uint16_t>(inputBuffer_.readInt16());
  // todo: what if ns_count and ar_count != 0
  if (question_count != 1 && ns_count != 0 && ar_count != 0) {
    entry->resolveCb(muduo::net::InetAddress());
    return;
  }
  uint8_t label_length;
  // process with question
  while (inputBuffer_.readableBytes() >= 1 && (label_length = static_cast<uint8_t>(inputBuffer_.readInt8())) != 0) {
    if (inputBuffer_.readableBytes() < label_length) {
      LOG_ERROR << "error label length";
      entry->resolveCb(muduo::net::InetAddress());
      return;
    }
    inputBuffer_.retrieve(label_length);
  }
  if (inputBuffer_.readableBytes() < 2 * 2)
  {
    LOG_ERROR << "error question packet!";
    entry->resolveCb(muduo::net::InetAddress());
    return;
  }
  uint16_t query_type = static_cast<uint16_t>(inputBuffer_.readInt16());
  uint16_t query_class = static_cast<uint16_t>(inputBuffer_.readInt16());
  (void)query_class;
  uint16_t valid_query_type = entry->ipv6() ? 28 : 1;
  if(valid_query_type != query_type)
  {
    LOG_ERROR << "query type not equal!";
    entry->resolveCb(muduo::net::InetAddress());
    return;
  }

  for(int i = 0; i < answer_count; ++i)
  {
    if(!impl::retrieve_name(&inputBuffer_))
    {
      entry->resolveCb(muduo::net::InetAddress());
      return;
    }
    if(inputBuffer_.readableBytes() < 10)
    {
      LOG_ERROR << "invalid answer packet!";
      entry->resolveCb(muduo::net::InetAddress());
      return;
    }
    uint8_t answer_type = static_cast<uint16_t>(inputBuffer_.readInt16());
    uint8_t answer_class = static_cast<uint16_t>(inputBuffer_.readInt16());
    (void)answer_class;
    uint32_t ttl = static_cast<uint32_t>(inputBuffer_.readInt32());
    uint16_t data_length = static_cast<uint16_t>(inputBuffer_.readInt16());
    if(data_length > inputBuffer_.readableBytes())
    {
      LOG_ERROR << "can't get entire data!";
      entry->resolveCb(muduo::net::InetAddress());
      return;
    }
    ttl = (ttl >= TTL ? TTL - 1 : ttl);
    if(answer_type == valid_query_type)
    {
      // ipv6
      if(entry->ipv6() && data_length == 16)
      {
        struct sockaddr_in6 data;
        ::bzero(&data, sizeof(data));
        data.sin6_family = AF_INET6;
        memcpy(&data.sin6_addr, inputBuffer_.peek(), 16);

        {
          muduo::MutexLockGuard lock(mutex_);
          V6EntryPtr ptr = std::make_shared<AF_INET6_Entry>(data);
          WkV6EntryPtr wk_ptr(ptr);
          v6_datas_[entry->domain()] = wk_ptr;
          v6_buffers_.at(ttl).insert(ptr);
        }

        entry->resolveCb(muduo::net::InetAddress(data));
        return;
      }// ipv4
      else if (!entry->ipv6() && data_length == 4)
      {
        struct sockaddr_in data;
        ::bzero(&data, sizeof(data));
        data.sin_family = AF_INET;
        memcpy(&data.sin_addr.s_addr, inputBuffer_.peek(), 4);

        {
          muduo::MutexLockGuard lock(mutex_);
          V4EntryPtr ptr = std::make_shared<AF_INET_Entry>(data);
          WkV4EntryPtr wk_ptr(ptr);
          v4_datas_[entry->domain()] = wk_ptr;
          v4_buffers_.at(ttl).insert(ptr);
        }

        entry->resolveCb(muduo::net::InetAddress(data));
        return;
      }
    }
    else
      inputBuffer_.retrieve(data_length);
  }
  entry->resolveCb(muduo::net::InetAddress());
}

void dns_resolver::handleError()
{
  int err = muduo::net::sockets::getSocketError(channel_->fd());
  // strerror_tl is a wrapper function for strerror_r
  LOG_ERROR << "dns_channel::handleError [" << sockfd_ << "] - SO_ERROR = " << err << muduo::strerror_tl(err);
}

// call by timeout function
void dns_resolver::resolve(uint16_t transaction_id)
{
  assert(dns_datas_.count(transaction_id));
  auto& entry = dns_datas_[transaction_id];
  muduo::net::Buffer buf;
  buf.appendInt16(transaction_id);
  struct packet::flag query;
  buf.append(&query, sizeof(query));
  struct packet::count query_count;
  query_count.question_count = muduo::net::sockets::hostToNetwork16(1);
  buf.append(&query_count, sizeof(query_count));
  if(!impl::convert_host(entry->domain(), &buf))
  {
    LOG_FATAL << "convert_host error !";
  }
  uint16_t query_class = 1;
  u_int16_t query_type = (entry->ipv6() ? 28 : 1);
  buf.appendInt16(query_class);
  buf.appendInt16(query_type);
  auto timer_id = loop_->runAfter(timeout_, boost::bind(&dns_resolver::handleTimeout, this, transaction_id));
  entry->set_timer_id(timer_id);
  send(&buf);
}

void dns_resolver::onTimer()
{
  muduo::MutexLockGuard lock(mutex_);
  v4_buffers_.push_back(V4Bucket());
  v6_buffers_.push_back(V6Bucket());
}

// try to resolve again
void dns_resolver::handleTimeout(uint16_t transaction_id)
{
  LOG_ERROR <<"transaction_id "<< transaction_id << " timeout, try again!";
  if(!dns_datas_.count(transaction_id))
  {
    LOG_ERROR << "can't find transaction_id " << transaction_id << " at dns_datas_";
    return;
  }
  auto entry = dns_datas_[transaction_id];
  if(entry->add_count_and_get() > MAX_TIMEOUT)
  {
    // use 0.0.0.0 to call resolveCallback function, notify client that the resolve is error!
    entry->resolveCb(muduo::net::InetAddress());
    dns_datas_.erase(transaction_id);
    return;
  }
  else
    resolve(transaction_id);
}