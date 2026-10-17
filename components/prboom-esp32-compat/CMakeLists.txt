set(srcs i_main.c i_network.c i_sound.c i_system.c i_video.c
         dbopl.c dbopl/opl.c dbopl/oplplayer.c dbopl/opl_queue.c
         i80_lcd.c i2s_dac.c)
set(requires esp_lcd esp_driver_i2s esp_timer spiffs prboom)

if(DOOM_USB_HOST)
    list(APPEND srcs usb_host_hid.c i_mouse.c i_keyboard.c)
    list(APPEND requires usb_host_hid)
    if(DOOM_USB_SIZE_OPT)
        set_source_files_properties(
            usb_host_hid.c i_mouse.c i_keyboard.c
            PROPERTIES COMPILE_OPTIONS "-Os")
    endif()
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS include
                       REQUIRES ${requires})

# These headers are part of the component's source set for CMake-aware IDEs.
file(GLOB component_headers CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_LIST_DIR}/include/*.h"
     "${CMAKE_CURRENT_LIST_DIR}/dbopl/*.h")
target_sources(${COMPONENT_LIB} PRIVATE ${component_headers})

# DOSBox's dbopl synthesizes directly at the output sample rate (unlike a
# cycle-accurate core such as Nuked OPL3, which always runs its internal
# state machine near the real chip's ~49.7kHz rate and decimates down --
# too slow to keep up in real time at the project's global debug
# optimization level). Keep this hot audio path fast regardless of that.
set_source_files_properties(
    dbopl.c  dbopl/opl.c dbopl/oplplayer.c dbopl/opl_queue.c i_sound.c
    PROPERTIES COMPILE_OPTIONS "-O2")
