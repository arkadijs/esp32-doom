cmake_minimum_required(VERSION 3.16)

option(DOOM_USB_HOST "Build USB HID host (OFF selects USB console)" ON)

# Shared board/memory defaults, followed only by variant-specific USB settings.
# Generated configurations stay build-local so USB variants remain isolated.
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig")
if(DOOM_USB_HOST)
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.usb-host.defaults")
    set(DOOM_PROJECT_NAME "esp32-doom-usb-host")
    add_compile_definitions(DOOM_USB_HOST=1)
else()
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.usb-console.defaults")
    set(DOOM_PROJECT_NAME "esp32-doom")
endif()

set(COMPONENTS esptool_py main prboom-esp32-compat prboom)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project("${DOOM_PROJECT_NAME}")

if(DOOM_USB_HOST)
    # The default build is the USB host variant. The variant with the USB
    # console (no peripherals) lives in its own build directory.
    foreach(target IN ITEMS build clean app-flash)
        if(target STREQUAL "build")
            set(console_name build-console)
            set(console_command build)
            set(console_comment "Building esp32-doom.bin")
        elseif(target STREQUAL "clean")
            set(console_name clean-console)
            set(console_command fullclean)
            set(console_comment "Cleaning esp32-doom build")
        else()
            set(console_name app-flash-console)
            set(console_command app-flash)
            set(console_comment "Flashing esp32-doom.bin")
        endif()
        add_custom_target(${console_name}
            COMMAND ${CMAKE_COMMAND} -E env
                    "IDF_PATH=$ENV{IDF_PATH}"
                    ${PYTHON} "$ENV{IDF_PATH}/tools/idf.py"
                    -B "${CMAKE_SOURCE_DIR}/build-console"
                    -DDOOM_USB_HOST=OFF ${console_command}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            USES_TERMINAL
            COMMENT "${console_comment}")
    endforeach()

    add_custom_target(build-all-variants DEPENDS app build-console)
endif()
