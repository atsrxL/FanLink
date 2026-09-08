cmake_minimum_required(VERSION 3.16)
project(FanLinkCore LANGUAGES CXX)

enable_testing()

add_library(fanlink_core STATIC
  src/common/ota_protocol.cpp
  src/app/ota_receiver.cpp
  src/app/ota_tx_bridge.cpp
  src/app/ota_tx_local.cpp
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

add_executable(fanlink_ota_tests tests/ota_receiver_tests.cpp)
target_link_libraries(fanlink_ota_tests PRIVATE fanlink_core)
add_test(NAME fanlink_ota_tests COMMAND fanlink_ota_tests)
add_executable(fanlink_ota_integration_tests tests/ota_integration_tests.cpp)
target_link_libraries(fanlink_ota_integration_tests PRIVATE fanlink_core)
add_test(NAME fanlink_ota_integration_tests COMMAND fanlink_ota_integration_tests)
add_executable(fanlink_tx_local_tests tests/tx_local_tests.cpp)
target_link_libraries(fanlink_tx_local_tests PRIVATE fanlink_core)
add_test(NAME fanlink_tx_local_tests COMMAND fanlink_tx_local_tests)
