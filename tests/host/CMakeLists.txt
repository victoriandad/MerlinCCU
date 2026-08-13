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

## C++20 (not 17): the firmware build compiles with `-std=gnu++17`, whose GNU
# extensions accept designated initializers (panel_config.h's kPanel, several
# ConsoleModel status structs) that strict C++17 doesn't. MSVC has no
# GNU-extensions mode, so this host project targets C++20 instead, where
# designated initializers are standard -- issue #71.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MERLIN_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(host_tests
    main.cpp
    test_alert_ordering.cpp
    test_keypad_matrix_decode.cpp
    test_config_persistence.cpp
    test_date_time_math.cpp
    test_pinter_scheduling.cpp
    test_pinter_persistence.cpp
    test_calendar_navigation.cpp
    test_http_response.cpp
    test_weather_json_scan.cpp
    test_weather_normalisation.cpp
    test_weather_forecast_parser.cpp
    test_home_assistant_weather_parser.cpp
    test_open_meteo_parser.cpp
    test_bounded_json.cpp
    test_share_feed_parser.cpp
    test_air_traffic_response_parser.cpp
    test_golden_screens.cpp
    golden_test_support.cpp
    stubs/time_manager_stub.cpp
    stubs/config_manager_stub.cpp
    stubs/environment_sensor_manager_stub.cpp
    ${MERLIN_ROOT}/src/core/alert_ordering.cpp
    ${MERLIN_ROOT}/src/config/config_persistence.cpp
    ${MERLIN_ROOT}/src/network/date_time_math.cpp
    ${MERLIN_ROOT}/src/core/pinter_scheduling.cpp
    ${MERLIN_ROOT}/src/core/pinter_persistence.cpp
    ${MERLIN_ROOT}/src/core/calendar_navigation.cpp
    ${MERLIN_ROOT}/src/network/http_response.cpp
    ${MERLIN_ROOT}/src/network/weather_json_scan.cpp
    ${MERLIN_ROOT}/src/network/weather_normalisation.cpp
    ${MERLIN_ROOT}/src/network/weather_forecast_parser.cpp
    ${MERLIN_ROOT}/src/network/home_assistant_weather_parser.cpp
    ${MERLIN_ROOT}/src/network/open_meteo_parser.cpp
    ${MERLIN_ROOT}/src/network/bounded_json.cpp
    ${MERLIN_ROOT}/src/network/share_feed_parser.cpp
    ${MERLIN_ROOT}/src/network/air_traffic_response_parser.cpp
    ${MERLIN_ROOT}/src/display/framebuffer.cpp
    ${MERLIN_ROOT}/src/display/screen_banners.cpp
    ${MERLIN_ROOT}/src/display/screens.cpp
    ${MERLIN_ROOT}/src/display/calendar_screens.cpp
    ${MERLIN_ROOT}/src/display/pinter_screens.cpp
    ${MERLIN_ROOT}/src/display/alert_screens.cpp
    ${MERLIN_ROOT}/src/display/shares_screens.cpp
    ${MERLIN_ROOT}/src/display/settings_screens.cpp
    ${MERLIN_ROOT}/src/display/status_screens.cpp
    ${MERLIN_ROOT}/src/display/weather_screens.cpp
    ${MERLIN_ROOT}/src/display/air_traffic_screens.cpp
)

target_include_directories(host_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MERLIN_ROOT}/include/core
    ${MERLIN_ROOT}/include/sensors
    ${MERLIN_ROOT}/include/config
    ${MERLIN_ROOT}/include/network
    ${MERLIN_ROOT}/include/display
    ${MERLIN_ROOT}/include/display/fonts
    ${MERLIN_ROOT}/config
)

# Golden-image (pixel snapshot) references live alongside the test sources,
# not under the build directory, so they survive a clean rebuild and are
# reviewable/diffable in version control -- see golden_test_support.h.
target_compile_definitions(host_tests PRIVATE
    MERLINCCU_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
)

# Issue #69: the only static-analysis net for this native build was previously
# .clang-tidy, which needs a manual/IDE invocation rather than running on
# every build. MSVC's /W4 and GCC/Clang's -Wall -Wextra are each the closest
# native equivalent for whichever compiler configures this project.
target_compile_options(host_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

enable_testing()
add_test(NAME host_tests COMMAND host_tests)
