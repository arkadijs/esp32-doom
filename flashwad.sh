#!/bin/sh -xe
esptool="esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB"

$esptool 0x100000 $DOOMWADDIR/doom2.wad
$esptool 0xF80000 $DOOMWADDIR/prboom-plus.wad
