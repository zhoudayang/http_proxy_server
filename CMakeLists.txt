cmake_minimum_required(VERSION 2.8)
project(https_proxy)

add_definitions(-std=c++11)

add_compile_options(-Wall -Werror -g -O2)

find_package(Boost COMPONENTS program_options REQUIRED)

# to use zy_dns, please run cmake with the command cmake . -DWITH_ZY_DNS=ON
OPTION(WITH_ZY_DNS "zy dns" ON)

if(WITH_ZY_DNS)
    message("with zy dns")
    add_definitions(-DZY_DNS)
    set(SOURCE_FILES
            proxy_server.cc
            dns_resolver.cc
            tunnel.cc
            http_header.cc
            server_main.cc
            )
    link_libraries(muduo_net_cpp11 muduo_base_cpp11 pthread ${Boost_LIBRARIES})
else()
    message("without zy dns")
    set(SOURCE_FILES
            proxy_server.cc
            tunnel.cc
            http_header.cc
            server_main.cc
            )
    link_libraries(muduo_net_cpp11 muduo_base_cpp11 muduo_cdns pthread cares ${Boost_LIBRARIES})
endif()

add_executable(zy_https_proxy ${SOURCE_FILES})