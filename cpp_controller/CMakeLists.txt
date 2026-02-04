cmake_minimum_required(VERSION 3.16)
project(cpp_controller LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

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
