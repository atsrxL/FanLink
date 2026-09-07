cmake_minimum_required(VERSION 3.16)
project(FanLinkCore LANGUAGES CXX)

enable_testing()

add_library(fanlink_core STATIC
  src/common/crc16.cpp
  src/common/protocol.cpp
  src/common/serial_protocol.cpp
  src/app/tx_controller.cpp
  src/app/rx_controller.cpp
)

target_include_directories(fanlink_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fanlink_core PUBLIC cxx_std_17)
target_compile_options(fanlink_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(fanlink_native_tests tests/native_tests.cpp)
target_link_libraries(fanlink_native_tests PRIVATE fanlink_core)
target_compile_features(fanlink_native_tests PRIVATE cxx_std_17)
target_compile_options(fanlink_native_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME fanlink_native_tests COMMAND fanlink_native_tests)
