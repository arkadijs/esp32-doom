#!/bin/sh -x
exec esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x100000 doom1-cut.wad
