ESP32-DOOM - a port of PrBoom to the ESP32-S3
=============================================
This is a port of PrBoom, which is an extended port of the original 1993
*ID software* hit game Doom, to the ESP32-S3. It runs on an ESP32-S3 which
has 16MiB of flash (app + doom2.wad) and at least 4MiB of PSRAM connected.

.. image:: https://img.youtube.com/vi/N5STKhmB9lc/hqdefault.jpg
   :target: https://youtu.be/N5STKhmB9lc
   
The demo gameplay above runs on LilyGo's
`T-Display-S3 <https://www.lilygo.cc/products/t-display-s3>`_ hardware.

Compiling
---------
This code is an `esp-idf <https://github.com/espressif/esp-idf>`_ project::

	idf.py build

Run ``idf.py menuconfig`` to configure pins.

Display
-------
Currently, the display interface is hardwired for the i8080 parallel bus to
ST7789 LCD controller, as found in T-Display-S3.

More to come, ie. SPI display support. Or hack your own!

See ``components/prboom-esp32-compat/i80_lcd.c`` for details.

=========  ======
Pin        GPIO
=========  ======
Power      15
Backlight  38
Reset      5
Chip sel   6
Data/Cmd   7
Clock      8
Data 0-7   39..48
=========  ======

Sound
-----
Stereo sound at 48kHz via any cheap TI PCM510x or ES7148 / ES7134 **I2S** DAC
from `AliExpress <https://www.aliexpress.com/item/1005002898278583.html>`_ ($3).

=========  ======
Pin        GPIO
=========  ======
BCLK       TBD
LRCK       TBD
DOUT       TBD
=========  ======

Flashing
--------
The main Doom binary can be built and flashed using::

	idf.py flash

Doom also needs game data, and ESP32-Doom expects this data to be put on
separate flash partitions. See ``partitions.csv`` for details.
``spiffs`` is for savegames and demos (to be implemented).

Edit and run::

	./flashwad.sh

Plan
----
- Sound FX or music.
- USB mouse and keyboard.
- Savegames and demos.
- `Sunlust <https://www.moddb.com/mods/sunlust>`_!
  (need 64MiB flash and memory mapping hacks)

Credits
-------
Doom is released by ID software in 1999 under the GNU GPL.
PrBoom is a modification of this code; its authors are credited in 
the ``components/prboom/AUTHORS`` file.
The ESP32 modifications are done by Espressif and licensed under the
Apache license, version 2.0.
ESP32-S3 port and sound by Arkadi Shishlov, Apache license 2.0.
