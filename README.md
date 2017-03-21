### A simple implement of http/https proxy server

#### detail
* support http and https proxy
* based on muduo network library
* optional non blocking dns query between cares and zy_dns 
* use HighWaterMark and LowWaterMark callback function to control network traffic

#### dependency 
1. muduo
2. muduo_cdns
3. libc-ares-dev (optional)
4. boost-program-options