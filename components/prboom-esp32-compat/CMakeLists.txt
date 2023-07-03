idf_component_register(SRCS i_main.c i_network.c i_sound.c i_system.c i_video.c i80_lcd.c
                       INCLUDE_DIRS include
                       REQUIRES esp_lcd spiffs prboom)
