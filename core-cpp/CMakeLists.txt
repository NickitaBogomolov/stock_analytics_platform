cmake_minimum_required(VERSION 3.16)
project(market_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(MARKET_BUILD_TESTS "Build Google Test suite" ON)

# --- Библиотека ядра (бизнес-логика) ---
add_library(market_core
  src/CsvLoader.cpp
  src/Indicators.cpp
  src/Analyzer.cpp
  src/Backtest.cpp
  src/MonteCarlo.cpp
  src/Portfolio.cpp)
target_include_directories(market_core PUBLIC include)
if(MSVC)
  target_compile_options(market_core PRIVATE /W4)
else()
  target_compile_options(market_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Консольное приложение ---
add_executable(market_cli app/main.cpp)
target_link_libraries(market_cli PRIVATE market_core)

# --- Тесты (Google Test) ---
if(MARKET_BUILD_TESTS)
  enable_testing()
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.14.0)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  add_executable(market_tests
    tests/IndicatorsTest.cpp
    tests/CsvLoaderTest.cpp
    tests/AnalyzerTest.cpp
    tests/QuantTest.cpp)
  target_link_libraries(market_tests PRIVATE market_core GTest::gtest_main)
  target_compile_definitions(market_tests PRIVATE
    MARKET_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data")
  include(GoogleTest)
  gtest_discover_tests(market_tests)
endif()
