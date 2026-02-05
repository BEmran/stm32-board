cmake_minimum_required(VERSION 3.16)
project(rosmaster_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(rosmaster
  src/core/basic.cpp
  src/rosmaster/rosmaster.cpp
  src/connection/serial_port.cpp
  src/connection/udp_socket.cpp
  src/utils/csv_recorder.cpp
  src/utils/logger.cpp
)

target_include_directories(rosmaster PUBLIC include)

add_executable(demo app/demo.cpp)
target_link_libraries(demo PRIVATE rosmaster)

add_executable(udp_gateway app/udp_gateway.cpp)
target_link_libraries(udp_gateway PRIVATE rosmaster)
