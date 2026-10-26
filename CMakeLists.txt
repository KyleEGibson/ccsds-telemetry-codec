cmake_minimum_required(VERSION 3.20)
project(ccsds_telemetry_codec VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TLM_BUILD_TESTS "Build the GoogleTest suite" ON)
option(TLM_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# Strict warnings, treated as errors, on every compiler.
add_library(tlm_warnings INTERFACE)
if(MSVC)
  target_compile_options(tlm_warnings INTERFACE /W4 /WX /permissive-)
else()
  target_compile_options(tlm_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
                                                -Wshadow -Wold-style-cast -Werror)
endif()

if(TLM_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(tlm
  src/crc16.cpp
  src/space_packet.cpp
  src/payloads.cpp
  src/sequence.cpp
  src/stream.cpp)
target_include_directories(tlm PUBLIC include)
target_link_libraries(tlm PRIVATE tlm_warnings)

if(TLM_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(tlm_tests
    tests/test_crc16.cpp
    tests/test_space_packet.cpp
    tests/test_payloads.cpp
    tests/test_sequence.cpp
    tests/test_stream.cpp)
  target_link_libraries(tlm_tests PRIVATE tlm tlm_warnings GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(tlm_tests)
endif()
