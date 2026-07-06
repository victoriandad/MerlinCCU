# Host-side regression tests (issue #14). This is a deliberately separate,
# non-cross-compiling CMake project: it must build and run with the
# machine's native compiler, no Pico SDK / arm-none-eabi toolchain involved.
#
# Configure and run from this directory, e.g.:
#   cmake -S . -B build
#   cmake --build build
#   ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.13)
project(MerlinCCUHostTests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MERLIN_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(host_tests
    main.cpp
    test_alert_ordering.cpp
    test_keypad_matrix_decode.cpp
    test_config_persistence.cpp
    test_date_time_math.cpp
    test_pinter_scheduling.cpp
    ${MERLIN_ROOT}/src/core/alert_ordering.cpp
    ${MERLIN_ROOT}/src/config/config_persistence.cpp
    ${MERLIN_ROOT}/src/network/date_time_math.cpp
    ${MERLIN_ROOT}/src/core/pinter_scheduling.cpp
)

target_include_directories(host_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MERLIN_ROOT}/include/core
    ${MERLIN_ROOT}/include/sensors
    ${MERLIN_ROOT}/include/config
    ${MERLIN_ROOT}/include/network
)

enable_testing()
add_test(NAME host_tests COMMAND host_tests)
