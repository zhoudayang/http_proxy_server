#pragma once

#include <boost/noncopyable.hpp>
#include <string>
#include <unordered_map>

namespace zy
{
// only for http_request, I ignore compatible with http response
class http_request : boost::noncopyable
{
public:

  http_request();

  // if not exist, return empty string
  std::string get_header(const std::string& key) const;

  bool init_request(const std::string& line);

  bool add_header(const std::string& line);

  bool initialized() { return !method_.empty(); }

  void set_content(const std::string& content) { content_ = content; }

  std::string proxy_request() const;

  std::string method() const { return method_; }

  std::string domain_name() const { return domain_name_; }

  uint16_t port() const { return port_; }

  bool valid() const { return !domain_name_.empty() && !method().empty(); }

private:
  std::string method_;
  std::string domain_name_;
  uint16_t port_;
  std::string url_;
  std::string version_;
  std::unordered_map<std::string, std::string> headers_;
  std::string proxy_request_;
  std::string content_;
};

}