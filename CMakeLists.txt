cmake_minimum_required(VERSION 3.24) #privious version have a clunkier fetch function


project(exchange LANGUAGES CXX)

include(FetchContent) #modeny dependency mec to clone repo during config like take it from exyternal repo and make it a part of our project

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRE ON) #to make it fail clearly
set(CMAKE_CXX_EXTENSIONS OFF) #not have compiles features
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_library(exchange_project INTERFACE)

target_include_directories(
    exchange_project
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(exchange_project INTERFACE cxx_std_20)
target_compile_options(
    exchange_project
    INTERFACE
        -Wall #warnings all
        -Wextra #warnings extra
        -Wpedantic #warning on non standard cpp features
        -fno-exceptions #disable cpp exceptions
)

add_executable(
    exchange
    src/exchange_main.cpp
)
target_link_libraries(
    exchange
    PRIVATE
        exchange_project
)

set_target_properties(
    exchange
    PROPERTIES
        OUTPUT_NAME "exchange"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src"
)
add_executable(
    trader_terminal
    tools/trader_terminal.cpp
)

target_link_libraries(
    trader_terminal
    PRIVATE
        exchange_project
)

set_target_properties(
    trader_terminal
    PROPERTIES
        OUTPUT_NAME "trader_terminal"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
)

add_executable(
    liquidity_bot
    tools/liquidity_bot.cpp
)

target_link_libraries(
    liquidity_bot
    PRIVATE
        exchange_project
)

set_target_properties(
    liquidity_bot
    PROPERTIES
        OUTPUT_NAME "liquidity_bot"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
)

# --- Unit Testing with Google Test ---
enable_testing()
find_package(GTest REQUIRED)

add_executable(
    matching_engine_tests
    tests/test_price_time_priority.cpp
    tests/test_partial_fills_and_queue.cpp
    tests/test_crossing_orders.cpp
    tests/test_spsc_queue.cpp
    tests/test_memory_pool.cpp
    tests/test_matching_engine_pool_limits.cpp
)

target_link_libraries(
    matching_engine_tests
    PRIVATE
        exchange_project
        GTest::gtest
        GTest::gtest_main
)

target_compile_options(
    matching_engine_tests
    PRIVATE
        -fexceptions
)

set_target_properties(
    matching_engine_tests
    PROPERTIES
        OUTPUT_NAME "matching_engine_tests"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_test(
    NAME matching_engine_tests
    COMMAND matching_engine_tests
)



