cmake_minimum_required(VERSION 3.16)
project(cpp_controller LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(controller
  src/main.cpp
  src/udp_socket.cpp
  src/controller.cpp
  src/packets.cpp
  algorithm/algorithm2.c
)

target_include_directories(controller PRIVATE include algorithm)
target_compile_options(controller PRIVATE -O2)
