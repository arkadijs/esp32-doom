set(srcs i_main.c i_network.c i_sound.c pcm_mix.c i_system.c i_video.c
         dbopl.c dbopl/opl.c dbopl/oplplayer.c dbopl/opl_queue.c
         i80_lcd.c i2s_dac.c)
set(requires esp_lcd esp_driver_i2s esp_timer spiffs prboom)

if(DOOM_USB_HOST)
    list(APPEND srcs usb_host_hid.c i_mouse.c i_keyboard.c)
    list(APPEND requires usb_host_hid)
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS include
                       REQUIRES ${requires})

# These headers are part of the component's source set for CMake-aware IDEs.
file(GLOB component_headers CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_LIST_DIR}/*.h"
     "${CMAKE_CURRENT_LIST_DIR}/include/*.h"
     "${CMAKE_CURRENT_LIST_DIR}/dbopl/*.h")
target_sources(${COMPONENT_LIB} PRIVATE ${component_headers})

# Conversion, synthesis and PCM mixing share the release speed settings.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -fwrapv -fno-strict-aliasing)
