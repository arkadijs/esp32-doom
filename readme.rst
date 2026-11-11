ESP32-DOOM - a port of PrBoom to the ESP32-S3
=============================================
This is a port of PrBoom, which is an extended port of the original 1993
*ID software* hit game Doom, to the ESP32-S3. It runs on an ESP32-S3 which
has 16MiB of flash and 8MiB of PSRAM connected.

.. image:: https://img.youtube.com/vi/0bvNRb1Mimw/hqdefault.jpg
   :target: https://youtu.be/0bvNRb1Mimw

The demo gameplay above runs on LilyGo's
`T-Display-S3 <https://www.lilygo.cc/products/t-display-s3>`_ hardware
with an external I2S DAC.

Compiling
---------
This code is an `esp-idf <https://github.com/espressif/esp-idf>`_ project::

	idf.py build

The default build is the USB keyboard/mouse host image. It uses the ESP32-S3
USB-OTG connector for the peripheral and keeps diagnostics on UART0; USB
Serial/JTAG output is therefore unavailable on that connector. To build the
variant that keeps USB available for the developer console instead, or both
variants, use::

	idf.py build-console
	idf.py build-all-variants

The binaries are written as ``build/esp32-doom-usb-host.bin`` and
``build-console/esp32-doom.bin``.

Both variants share board, memory and speed settings in ``sdkconfig``.
The ``sdkconfig.usb-*.defaults`` files contain USB-specific settings only.
Generated configurations live in each build directory.
The engine builds with ``-O2``, wrapping signed arithmetic and legacy aliasing
semantics. Simulation stays at 35 tics/s; rendering uses uncapped interpolation.
The firmware starts with the usual title/demo sequence and normal controls,
without injected input or cheats.

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
Stereo sound at 22kHz via any cheap TI PCM510x or ES7148 / ES7134 **I2S** DAC
from `AliExpress <https://www.aliexpress.com/w/wholesale-TI-PCM5102.html>`_ ($3).

=========  ======
Pin        GPIO
=========  ======
BCLK       16
LRCK       21
DOUT       17
=========  ======

See ``components/prboom-esp32-compat/i2s_dac.c`` for details.

Flashing
--------
The main Doom binary (USB host variant) can be built and flashed using::

	idf.py app-flash

When changing USB variants, install the matching bootloader too: use
``idf.py flash`` for host, or
``idf.py -B build-console -DDOOM_USB_HOST=OFF flash`` for console. The console
bootloader disables the default UART pins, so app-only variant switching can
leave the host image without visible UART logs.

To update only the USB console application, use::

	idf.py app-flash-console

Doom also needs game data, and ESP32-Doom expects this data to be put on
separate flash partitions. See ``partitions.csv`` for details.
``spiffs`` is for savegames and demos (to be implemented).

Set ``DOOMWADDIR`` to a directory containing ``doom2.wad`` and
``prboom-plus.wad``, then flash the assets while USB console/ROM download mode
is available::

	DOOMWADDIR=/path/to/wads ./flashwad.sh

Pass a serial port as ``./flashwad.sh PORT``, or use
``python -m esptool --port PORT write-flash`` with the
IWAD at ``0x100000`` and the support PWAD at ``0xF80000``. App-only flashing
preserves assets. A new board also needs the bootloader and partition table
from ``idf.py flash``. UART flashing may require holding BOOT during reset.
USB console firmware has no USB input; USB-host firmware takes a keyboard or
mouse through the USB-OTG connector and logs on UART0 at 115200 baud.

To switch from a running USB-console image to the host image automatically,
with a separate UART connection attached, use::

    bash tools/flash_host.sh /dev/ttyACM0 /dev/ttyS1 build/host-startup.log

This builds the host application, attaches the UART reader before flashing,
flashes its matching bootloader, partition table and application through USB
ROM download, performs the ESP32-S3 reset,
and checks 60 seconds of UART output for host/graphics startup and crashes.
It preserves game assets. After switching to USB-host mode, flashing again may require manually
entering ROM download with BOOT/reset.

Credits
-------
Doom is released by ID software in 1999 under the GNU GPL.

PrBoom is a modification of this code; its authors are credited in
the ``components/prboom/AUTHORS`` file.

The ESP32 modifications are done by Espressif and licensed under the
Apache license, version 2.0.

ESP32-S3 port, sound and USB support by Arkadi Shishlov, Apache license 2.0.

DOSBox OPL emulator by the DOSBox Team licensed under GNU GPL v2 or later.

MIDI OPL player interface by Simon Howard, Chocolate Doom licensed under
GNU GPL v2 or later.

Sunlust MAP01–MAP05
------------------

The first five maps of Sunlust fit alongside the program and PrBoom support
assets in 16 MiB flash. Supply your own legally obtained Doom II, Sunlust and
PrBoom support WADs. Generated Sunlust packs are not added to Git.

For the compact five-map image, first compress Doom II and Sunlust with
``wadptr`` 2.4 (a separate tool, not included here)::

    wadptr doom2.wad -o _doom2.wad -c
    wadptr sunlust.wad -o _sunlust.wad -c

Confirm wadptr's IWAD prompt if shown. Keep the originals. Then, from this
repository, generate and validate a flash-ready asset pair::

    bash tools/prepare_sunlust.sh /path/_doom2.wad /path/_sunlust.wad \
        /path/prboom-plus.wad build-assets/sunlust 5

Requirements: Python 3, Bash and a native C compiler named ``cc``. The last
argument may be 1–5. The packer removes unselected maps and unreachable wall
textures/flats, closes dependencies over switch pairs and animation sequences,
retains UI/finale backgrounds, all actor sprites and sounds, and includes each
selected map's music.
Map lumps remain byte-for-byte identical to the supplied Sunlust WAD.
It merges Sunlust's resource overrides into a normal IWAD and carries its
animation/switch overrides into the small support PWAD. This tool targets
Doom II + Sunlust; it is not a general PWAD merger.

The next stage runs PrBoom's C texture compositor on the host and writes a
versioned ``TXFLASH`` lump with deduplicated wall columns. Original resource
lumps remain available. The script checks map and texture references, compares
every compiled column with the C compositor, checks malformed format records,
and writes JSON manifests with sizes and SHA-256 hashes. It stops if either
asset exceeds its partition. Inputs without wadptr compression may fit fewer
maps or leave insufficient room for precompiled textures.

Flash ``build-assets/sunlust/doom2.wad`` and
``build-assets/sunlust/prboom-plus.wad`` using the asset offsets above, or::

    DOOMWADDIR=build-assets/sunlust ./flashwad.sh

The tested five-map IWAD is 14,644,856 bytes; its support PWAD is 348,451 bytes.
Exact sizes depend on the input WAD versions. MAP06 and later are absent;
finishing the subset returns to the title. Missing original-game demos are
skipped, so the title/credit loop remains usable.

Demo recordings placed in ``demos/*.lmp`` (up to eight, sorted by file name)
are stored as ``DEMO1``..``DEMO8`` in the support PWAD by ``prepare_sunlust.sh``
and play in the title loop. To add demos without regenerating the IWAD::

    python3 tools/add_demos.py build-assets/sunlust/prboom-plus.wad prboom-plus.wad demos
    python -m esptool --chip esp32s3 write-flash 0xF80000 prboom-plus.wad

Demos must match the packed maps (currently Sunlust MAP01..MAP05).

Flash layout
------------

================  ==========  ============
Partition         Offset      Capacity
================  ==========  ============
Application       0x010000    960 KiB
IWAD              0x100000    14,848 KiB
Support PWAD      0xF80000    448 KiB
SPIFFS            0xFF0000    64 KiB
================  ==========  ============

WADs are memory-mapped from flash. Level geometry, live actors, renderer
metadata and generated sprite patches use PSRAM. Precompiled texture pixels
and posts stay in flash, with a bounded, purgeable 128 KiB PSRAM pixel cache.
Raw WADs use the original runtime texture compositor; PWAD texture overrides
also fall back to it. Original Doom resources, Boom effects, special column
filters and legacy save-record layouts remain supported. Large mods are still
limited by flash capacity and available PSRAM.

The renderer uses packed visplanes, exact per-view geometry caches, early
visibility checks and direct opaque column drawing. Live actors occupy
192 bytes on ESP32-S3, with the legacy save layout kept separately. Empty
non-vanilla REJECT tables use implicit zeros. LCD DMA completion controls
framebuffer ownership; indexed framebuffers swap during eligible gameplay.
LCD conversion and the active-channel PCM/OPL audio pipeline run on core 1.
No benchmark harness, periodic performance output or scripted controls are
included in the application.

Packing checks run automatically from ``prepare_sunlust.sh`` and require your
WADs. Renderer/audio development fixtures are maintained locally and are not
part of the distributed preparation/flashing tools.
