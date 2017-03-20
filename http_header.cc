#include "http_header.h"

#include <muduo/base/Logging.h>
#include <vector>

using namespace zy;

namespace impl
{

std::vector<std::string> split(const std::string& line, char ch)
{
  size_t pos0 = 0;
  size_t pos1;
  size_t length = line.size();
  std::vector<std::string> results;
  while((pos1 = line.find(ch, pos0)) != std::string::npos)
  {
    std::string str = line.substr(pos0, pos1 - pos0);
    results.emplace_back(str);
    pos0 = pos1 + 1;
  }
  if(pos0 < length)
  {
    results.push_back(line.substr(pos0));
  }
  return results;
}

// fixme: check if url is valid
bool init_url(const std::string& line, std::string &domain, std::string& url, uint16_t& port)
{
  size_t begin = 0;
  size_t length = line.size();
  auto pos = line.find("//");
  if(pos != std::string::npos)
  {
    if((begin = pos + 2) >= length)
      return false; // invalid url
  }
  auto slash_pos = line.find('/', begin);
  if(slash_pos == std::string::npos)
  {
    domain = line.substr(begin);
    url = "/";
  }
  else
  {
    domain = line.substr(begin, slash_pos - begin);
    url = line.substr(slash_pos);
  }
  port = 80;
  auto results = split(domain, ':');
  if(results.size() == 1)
    return true;
  else if (results.size() == 2)
  {
    try {
      port = static_cast<uint16_t>(std::stoi(results[1]));
      domain = results[0];
      return true;
    }catch(...)
    {
      LOG_ERROR << "port convert error because of " << results[1];
      return false;
    }
  }
  else
  {
    return false;
  }
}

std::string to_lower(const std::string& input)
{
  std::string result;
  for(auto ch : input)
    result.push_back(std::tolower(ch));
  return result;
}

}

http_request::http_request()
  : method_(),
    domain_name_(),
    port_(80),
    url_(),
    version_(),
    headers_(),
    proxy_request_(),
    content_()
{

}

std::string http_request::get_header(const std::string& key) const
{
  auto lower_key = impl::to_lower(key);
  auto it = headers_.find(lower_key);
  if(it != headers_.end())
    return it->second;
  return "";
}

bool http_request::init_request(const std::string &line)
{
  auto contents = impl::split(line, ' ');
  if(contents.size() != 3)
    return false;
  method_ = contents[0];
  version_ = contents[2];
  auto proxy_url = contents[1];
  bool ret = impl::init_url(proxy_url, domain_name_, url_, port_);
  if(ret)
  {
    proxy_request_ += method_ + " " + url_ + " " + version_ + "\r\n";
  }
  return ret;
}

bool http_request::add_header(const std::string &line)
{
  size_t colon = line.find(':');
  if(colon == line.npos || colon == line.size() - 1)
  {
    return false;
  }
  auto key = line.substr(0, colon);
  auto value = line.substr(colon + 1);
  while(!value.empty() && value.front() == ' ')
  {
    value.erase(value.begin());
  }
  if(value.empty())
    return false;
  std::string lower_key = impl::to_lower(key);
  headers_[lower_key] = value;
  // 对proxy-connection进行特殊处理
  if(lower_key == "proxy-connection")
  {
    proxy_request_ += "Connection: Keep-Alive\r\n";
  }
  else
  {
    proxy_request_ += line + "\r\n";
  }
  return true;
}

std::string http_request::proxy_request() const {
  std::string result = proxy_request_ + "\r\n";
  if(!result.empty())
    result += content_;
  return result;
}