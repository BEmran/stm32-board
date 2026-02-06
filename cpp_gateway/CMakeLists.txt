cmake_minimum_required(VERSION 3.16)
project(rosmaster_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(rosmaster
  src/core/basic.cpp
  src/rosmaster/rosmaster.cpp
  src/connection/serial_port.cpp
  src/connection/tcp_socket.cpp
  src/connection/udp_packets.cpp
  src/connection/udp_socket.cpp
  src/utils/csv_recorder.cpp
  src/utils/logger.cpp
  src/utils/timestamp.cpp
)

target_include_directories(rosmaster PUBLIC include)

add_executable(demo app/demo.cpp)
target_link_libraries(demo PRIVATE rosmaster)

add_executable(udp_gateway app/udp_gateway.cpp)
target_link_libraries(udp_gateway PRIVATE rosmaster)

add_executable(udp_client app/udp_client.cpp)
target_link_libraries(udp_client PRIVATE rosmaster)

add_executable(demo_recorder app/demo_recorder.cpp)
target_link_libraries(demo_recorder PRIVATE rosmaster)

add_executable(tcp_client app/tcp_client.cpp)
target_link_libraries(tcp_client PRIVATE rosmaster)

add_executable(tcp_gateway app/tcp_gateway.cpp)
target_link_libraries(tcp_gateway PRIVATE rosmaster)
