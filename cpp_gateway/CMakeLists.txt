cmake_minimum_required(VERSION 3.16)
project(rosmaster_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(rosmaster
  src/rosmaster/rosmaster.cpp
  src/connection/serial_port.cpp
  src/connection/tcp_socket.cpp
  src/connection/packets.cpp
  src/utils/csv_recorder.cpp
  src/utils/logger.cpp
  src/utils/timestamp.cpp
  src/utils/binary_log.cpp
)

target_include_directories(rosmaster PUBLIC include)

add_executable(tcp_client app/tcp_client.cpp)
target_link_libraries(tcp_client PRIVATE rosmaster)

# add_executable(tcp_gateway app/tcp_gateway.cpp app/helpper.cpp)
# target_link_libraries(tcp_gateway PRIVATE rosmaster)

# New threaded gateway
add_executable(gateway_threaded
  app/gateway_main.cpp
  app/workers/usb_worker.cpp
  app/workers/tcp_worker.cpp
  app/workers/controller_worker.cpp
  app/workers/log_worker.cpp
)
target_link_libraries(gateway_threaded PRIVATE rosmaster)

add_executable(decode_gateway_log app/decode_gateway_log.cpp)
target_link_libraries(decode_gateway_log PRIVATE rosmaster)
