This is a C project - Doom game ported to an embedded ESP32-S3 microcontroller board with ESP IDF and CMake.
To build: idf.py build
To flash the image and run the game: idf.py app-flash
Flashing resets the device.

There are two binary targets: the USB host app that can handle peripherals (default build target, build dir `build`) and the regular app with USB console.
To build the USB console app: idf.py build-console
To flash it: idf.py app-flash-console

The build is rather verbose, so make sure to filter output for errors (if required) before feeding as input to save tokens.

The OS environment of the parent shell is already setup via `source ../esp-idf/export.sh`.
If a tool need to be launched and it cannot inherit current shell environment to access IDF facilities then re-source the file with a shell.

The game standard output, error streams, assertions can be captured via serial over /dev/ttyACM0 when USB console is enabled.
Use /dev/ttyS1 to monitor and flash USB host-enabled build, attach monitor first so that all messages are captured after reset.
You have access to /dev/ttyACM0 and /dev/ttyS1 devices with native tools. picocom is available if required.
When you need to reflash USB host firmware (over ttyS1) - tell me and I'll press the boot + reset buttons.

After building the image, flash it. Monitor output for errors. Insert debug messages and asserts if necessary.
