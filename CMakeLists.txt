cmake_minimum_required(VERSION 3.6)
project(https_server)

add_definitions(-std=c++11)

add_compile_options(-Wall -Werror -g -O0)

set(SOURCE_FILES
        dns_resolver.cc
        proxy_server.cc
        tunnel.cc
        http_header.cc
        server_main.cc
        )

link_libraries(muduo_net_cpp11 muduo_base_cpp11 pthread)

add_executable(http_server ${SOURCE_FILES})

