# Generated Cmake Pico project file
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(EXECUTABLE_NAME "rgb_gen_pico")
set(PICO_BOARD pico CACHE STRING "Board type")

# Initialize pico_sdk from installed location
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
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(rgb_gen_pico C CXX ASM)

# Initialize the Raspberry Pi Pico SDK
pico_sdk_init()

function(set_up_pico_target TARGET_NAME)
    add_executable(${TARGET_NAME})

    target_sources(
        ${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/config.h
        ${CMAKE_CURRENT_LIST_DIR}/src/video_mode.h
        ${CMAKE_CURRENT_LIST_DIR}/src/vram.h
        ${CMAKE_CURRENT_LIST_DIR}/src/vram.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/agat7_picture.h
        ${CMAKE_CURRENT_LIST_DIR}/src/agat7_picture.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/agat7_renderer.h
        ${CMAKE_CURRENT_LIST_DIR}/src/agat7_renderer.cpp
    )

    pico_generate_pio_header(${TARGET_NAME}
        ${CMAKE_CURRENT_LIST_DIR}/src/programs.pio
    )

    # Modify the below lines to enable/disable output over UART/USB
    pico_enable_stdio_uart(${TARGET_NAME} 0)
    pico_enable_stdio_usb(${TARGET_NAME} 1)

    # Add the standard include files to the build
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
    )

    # Add any user requested libraries
    target_link_libraries(${TARGET_NAME}
        hardware_pio
        hardware_dma
        hardware_flash
        pico_multicore
        pico_unique_id
        pico_stdlib
    )

    target_compile_options(${TARGET_NAME} PRIVATE
        -O3
    )

    # Add firmware version definition if provided via CMake
    if(DEFINED FW_VERSION)
        target_compile_definitions(${TARGET_NAME} PRIVATE FW_VERSION="${FW_VERSION}")
    endif()

    # Add linker flag to print memory usage
    target_link_options(${TARGET_NAME} PRIVATE
        -Wl,--print-memory-usage
    )

    pico_add_extra_outputs(${TARGET_NAME})
endfunction()

# Add the primary development executable using the function
set_up_pico_target(${EXECUTABLE_NAME})

# Ensure the vendor/reset interface is compiled in.
set(PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE 1)

# Extend the clean target to remove all object files (*.dir subdirectories only)
set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES
    "${CMAKE_BINARY_DIR}/CMakeFiles/${EXECUTABLE_NAME}.dir"
)

if(BUILD_ALL_RELEASES)
    # Retrieve the Git tag.
    find_package(Git REQUIRED)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        ERROR_VARIABLE GIT_ERR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT GIT_TAG OR GIT_ERR)
        message(FATAL_ERROR "Git tag not found - expectee v<X>.<Y>.")
    endif()

    function(add_release BOARD SYNC)
        set(ARTIFACT_NAME "${EXECUTABLE_NAME}_${GIT_TAG}_${BOARD}_sync_${SYNC}")

        set_up_pico_target(${ARTIFACT_NAME})

        target_compile_definitions(${ARTIFACT_NAME} PRIVATE
            BOARD=${BOARD}
            SYNC=${SYNC}
            FW_VERSION="${GIT_TAG}"
        )
    endfunction()

    add_release("rgb2vga" "neg")
    add_release("rgb2vga" "pos")
    add_release("murmulator" "neg")
    add_release("murmulator" "pos")
endif()
