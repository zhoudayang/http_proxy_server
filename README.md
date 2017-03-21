### A simple implement of http/https proxy server

#### detail
* support http and https proxy
* based on muduo network library
* optional non blocking dns query between cares and zy_dns 
* use HighWaterMark and LowWaterMark callback function to control network traffic

#### build dependency 
1. muduo
2. muduo_cdns
3. libc-ares-dev (optional)
4. boost-program-options

#### how to use

the default bind address is 0.0.0.0, and the default listen port is 8768. if you want to change to self-defined value. please run 

```
zy_https_proxy -iIP -pPORT
```

the IP and PORT above are the ip address and port that you want http_proxy_server bind to. for more information, please run 

```
zy_https_proxy -h
```

I recommend you to use the cares as the dns resolver because it's stability.
