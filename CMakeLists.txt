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

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1
add_executable(MerlinCCU
    MerlinCCU.cpp
    console_controller.cpp
    console_model.cpp
    display.cpp
    framebuffer.cpp
    input.cpp
    screen_banners.cpp
    screens.cpp
    screensaver_life.cpp
    time_manager.cpp
    wifi_manager.cpp
)

pico_set_program_name(MerlinCCU "MerlinCCU")
pico_set_program_version(MerlinCCU "0.1")

# Generate PIO header
pico_generate_pio_header(MerlinCCU ${CMAKE_CURRENT_LIST_DIR}/el320_raster.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(MerlinCCU 0)
pico_enable_stdio_usb(MerlinCCU 1)

# Add the standard library to the build
target_link_libraries(MerlinCCU
    pico_stdlib
)

# Add the standard include files to the build
target_include_directories(MerlinCCU PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(MerlinCCU
pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_netbios
    pico_lwip_sntp
    hardware_pio
    hardware_dma
)

pico_add_extra_outputs(MerlinCCU)
