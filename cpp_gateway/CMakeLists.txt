cmake_minimum_required(VERSION 3.16)
project(rosmaster_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(rosmaster
  src/rosmaster/rosmaster.cpp
  src/rosmaster/basic.cpp
  src/connection/serial_port.cpp
)

target_include_directories(rosmaster PUBLIC include)

add_executable(demo app/demo.cpp)
target_link_libraries(demo PRIVATE rosmaster)
