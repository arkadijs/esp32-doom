cmake_minimum_required(VERSION 3.16)

option(DOOM_USB_HOST "Build the USB HID host variant (OFF: USB console variant)" ON)
option(DOOM_USB_SIZE_OPT "Optimize USB host variant components for size" OFF)

if(DOOM_USB_HOST)
    # Host mode owns the ESP32-S3 USB-OTG peripheral. Use a build-local
    # sdkconfig, seeded from the normal configuration, so variants never
    # overwrite each other's configuration or binaries.
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
    set(SDKCONFIG_DEFAULTS
        "${CMAKE_CURRENT_LIST_DIR}/sdkconfig;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.usb-host.defaults")
    set(DOOM_PROJECT_NAME "esp32-doom-usb-host")
    add_compile_definitions(DOOM_USB_HOST=1)
else()
    set(DOOM_PROJECT_NAME "esp32-doom")
endif()

set(COMPONENTS esptool_py main prboom-esp32-compat prboom)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project("${DOOM_PROJECT_NAME}")

if(DOOM_USB_HOST AND DOOM_USB_SIZE_OPT)
    # External-hub support is sizeable. Keep the USB stack compact without
    # changing the debug-oriented optimization used by the game itself.
    idf_component_get_property(usb_component espressif__usb COMPONENT_LIB)
    idf_component_get_property(hid_component espressif__usb_host_hid COMPONENT_LIB)
    idf_component_get_property(compat_component prboom-esp32-compat COMPONENT_LIB)
    target_compile_options(${usb_component} PRIVATE -Os)
    target_compile_options(${hid_component} PRIVATE -Os)
    target_compile_options(${compat_component} PRIVATE -Os)
    foreach(idf_component esp_stdio esp_hw_support esp_system spi_flash freertos)
        idf_component_get_property(idf_lib ${idf_component} COMPONENT_LIB)
        target_compile_options(${idf_lib} PRIVATE -Os)
    endforeach()
endif()

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
