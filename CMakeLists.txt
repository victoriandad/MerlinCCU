# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if(EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(MerlinCCU C CXX ASM)

# The core-0 stack's linker reservation: all of SCRATCH_Y (4 KB), the most
# the SDK linker script allows. At run time stack_guard extends it down into
# the unused SCRATCH_X bank for 8 KB in all, and sets the hardware stack limit
# at the bottom so an overflow faults instead of corrupting memory (#164).
# Must be set before pico_sdk_init() so it reaches pico_crt0's own
# stack-reservation section, not just this target's sources.
add_compile_definitions(PICO_STACK_SIZE=0x1000)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1
add_executable(MerlinCCU
    src/core/MerlinCCU.cpp
    src/core/alert_controller.cpp
    src/core/alert_ordering.cpp
    src/core/calendar_controller.cpp
    src/core/calendar_navigation.cpp
    src/core/console_controller.cpp
    src/core/cursor_navigation.cpp
    src/core/console_model.cpp
    src/core/ha_key_list.cpp
    src/core/home_controller.cpp
    src/core/input.cpp
    src/core/ota_manager.cpp
    src/core/page_table.cpp
    src/core/pinter_controller.cpp
    src/core/pinter_persistence.cpp
    src/core/pinter_scheduling.cpp
    src/core/pinter_store.cpp
    src/core/psram.cpp
    src/core/settings_controller.cpp
    src/core/softkey_builders.cpp
    src/core/reset_reason.cpp
    src/display/png_encoder.cpp
    src/display/weather_icons.cpp
    src/network/home_controls.cpp
    src/network/cert_pin.cpp
    src/core/stack_guard.cpp
    src/core/status_controller.cpp
    src/core/weather_extremes.cpp
    src/config/config_manager.cpp
    src/config/config_persistence.cpp
    src/config/config_fields.cpp
    src/config/geo_coordinates.cpp
    src/display/display.cpp
    src/display/framebuffer.cpp
    src/display/alert_screens.cpp
    src/display/calendar_screens.cpp
    src/display/pinter_screens.cpp
    src/display/screen_banners.cpp
    src/display/screens.cpp
    src/display/settings_screens.cpp
    src/display/local_conditions_screens.cpp
    src/display/shares_screens.cpp
    src/display/text_styles.cpp
    src/display/status_screens.cpp
    src/display/weather_screens.cpp
    src/display/screensavers/screensaver_clock.cpp
    src/display/screensavers/screensaver_life.cpp
    src/display/screensavers/screensaver_matrix.cpp
    src/display/screensavers/screensaver_radar.cpp
    src/display/screensavers/screensaver_rain.cpp
    src/display/screensavers/screensaver_starfield.cpp
    src/display/screensavers/screensaver_worms.cpp
    src/network/date_time_math.cpp
    src/network/bounded_json.cpp
    src/network/ha_request_plan.cpp
    src/network/house_weather.cpp
    src/network/home_assistant_manager.cpp
    src/network/home_assistant_weather_parser.cpp
    src/network/http_response.cpp
    src/network/mqtt_manager.cpp
    src/network/open_meteo_parser.cpp
    src/network/share_feed_parser.cpp
    src/network/share_price_manager.cpp
    src/network/time_manager.cpp
    src/network/weather_forecast_parser.cpp
    src/network/weather_json_scan.cpp
    src/network/weather_normalisation.cpp
    src/network/mqtt_commands.cpp
    src/network/ota_upload.cpp
    src/network/web_auth.cpp
    src/network/web_config_server.cpp
    src/network/wifi_manager.cpp
    src/sensors/bme_environmental_sensor.cpp
    src/sensors/environment_sensor_manager.cpp
    src/sensors/i2c_bus.cpp
    src/sensors/i2c_register_device.cpp
    src/sensors/local_conditions.cpp
    src/sensors/sgp40_air_quality_sensor.cpp
)

pico_set_program_name(MerlinCCU "MerlinCCU")
pico_set_program_version(MerlinCCU "0.1")

# Stamp the image with its git commit on every build (#154): shown on the
# Status page and the Firmware web page, and reported to Home Assistant.
set(MERLIN_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/merlin_generated)
add_custom_target(merlin_firmware_version ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}
            -DOUTPUT=${MERLIN_GENERATED_DIR}/firmware_version.h
            -P ${CMAKE_CURRENT_LIST_DIR}/tools/firmware_version.cmake
    BYPRODUCTS ${MERLIN_GENERATED_DIR}/firmware_version.h
    COMMENT "Stamping firmware version")
add_dependencies(MerlinCCU merlin_firmware_version)
target_include_directories(MerlinCCU PRIVATE ${MERLIN_GENERATED_DIR})

# Generate PIO header
pico_generate_pio_header(MerlinCCU ${CMAKE_CURRENT_LIST_DIR}/firmware/el320_raster.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(MerlinCCU 0)
pico_enable_stdio_usb(MerlinCCU 1)

# Add the standard library to the build
target_link_libraries(MerlinCCU
    pico_stdlib
)

# Boards with PSRAM get a .psram_bss section mapped onto it (#162). The extra
# script is merged into the SDK's memmap with INSERT rather than replacing it.
# psram.h tags objects for it only when the board header names a PSRAM chip
# select, so this list must match boards that do.
if(PICO_BOARD STREQUAL "pimoroni_pico_plus2_w_rp2350")
    target_link_options(MerlinCCU PRIVATE "LINKER:-T,${CMAKE_CURRENT_LIST_DIR}/firmware/psram.ld")
    set_property(TARGET MerlinCCU APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/firmware/psram.ld)
endif()

# -Wall -Wextra (issue #69) plus a hard failure specifically for an
# incomplete MenuPage switch (issue #63): the title/font/dispatch lookups in
# screens.cpp are already written exhaustively with no `default:` label, so a
# case missed when a new MenuPage value is added shows up as a build failure
# instead of a blank/stale page discovered by hand.
#
# -Wformat-truncation is suppressed: the remaining sites are softkey/status
# labels snprintf'd into fixed-size display buffers where truncating long
# input text is the deliberate, correct behaviour (there is no wider buffer to
# grow into on a 320-pixel-wide panel), not a bug to fix. GCC's static bound
# (based on the source type's range, e.g. any uint8_t up to 255) can't see
# that the real runtime values are always small.
#
# -Wstack-usage flags any one function frame over MERLIN_STACK_FRAME_WARN_BYTES. The
# core-0 stack is 8 KB in all (stack_guard.h), shared by the main loop and
# the lwIP callbacks that run on top of it, so a large local belongs in
# static storage instead (#164).
set(MERLIN_STACK_FRAME_WARN_BYTES 1600)
target_compile_options(MerlinCCU PRIVATE -Wall -Wextra -Werror=switch -Wno-format-truncation
    -Wstack-usage=${MERLIN_STACK_FRAME_WARN_BYTES})

# Add the standard include files to the build
target_include_directories(MerlinCCU PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/include/config
    ${CMAKE_CURRENT_LIST_DIR}/include/core
    ${CMAKE_CURRENT_LIST_DIR}/include/display
    ${CMAKE_CURRENT_LIST_DIR}/include/display/fonts
    ${CMAKE_CURRENT_LIST_DIR}/include/display/screensavers
    ${CMAKE_CURRENT_LIST_DIR}/include/network
    ${CMAKE_CURRENT_LIST_DIR}/include/sensors
    ${CMAKE_CURRENT_LIST_DIR}/config
)

set_source_files_properties(
    ${PICO_SDK_PATH}/src/rp2_common/pico_lwip/altcp_tls_mbedtls.c
    PROPERTIES
        COMPILE_DEFINITIONS MBEDTLS_ALLOW_PRIVATE_ACCESS
)

# Add any user requested libraries
target_link_libraries(MerlinCCU
pico_stdlib
    hardware_watchdog
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mqtt
    pico_lwip_mbedtls
    pico_lwip_netbios
    pico_lwip_sntp
    pico_mbedtls
    hardware_pio
    hardware_dma
    hardware_flash
    hardware_i2c
)

# Over-the-air updates (#154, docs/ota.md). Every image is built try-before-
# you-buy: after a flash-update boot the boot ROM runs it on trial, and
# ota_manager confirms it once the network is up. The embedded SHA-256 lets
# the boot ROM refuse a damaged image and keep the old one.
target_compile_definitions(MerlinCCU PRIVATE PICO_CRT0_IMAGE_TYPE_TBYB=1)
pico_hash_binary(MerlinCCU)

pico_add_extra_outputs(MerlinCCU)
