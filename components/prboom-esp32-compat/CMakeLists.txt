idf_component_register(SRCS i_main.c i_network.c i_sound.c i_system.c i_video.c
                       dbopl.c dbopl/opl.c dbopl/oplplayer.c dbopl/opl_queue.c
                       i80_lcd.c i2s_dac.c
                       INCLUDE_DIRS include
                       REQUIRES esp_lcd esp_driver_i2s esp_timer spiffs prboom)

# DOSBox's dbopl synthesizes directly at the output sample rate (unlike a
# cycle-accurate core such as Nuked OPL3, which always runs its internal
# state machine near the real chip's ~49.7kHz rate and decimates down --
# too slow to keep up in real time at the project's global debug
# optimization level). Keep this hot audio path fast regardless of that.
set_source_files_properties(
    dbopl.c  dbopl/opl.c dbopl/oplplayer.c dbopl/opl_queue.c i_sound.c
    PROPERTIES COMPILE_OPTIONS "-O2")
