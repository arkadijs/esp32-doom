This is a C project - Doom game ported to an embedded ESP32-S3 microcontroller board with ESP IDF and CMake.
To build: idf.py build
To flash the image and run the game: idf.py app-flash
Flashing resets the device.

The OS environment of the parent shell is already setup via `source ../esp-idf/export.sh`.
If a tool need to be launched and it cannot inherit current shell environment to access IDF facilities then re-source the file with a shell.

The game standard output, error streams, assertions can be captured via serial over /dev/ttyACM0. picocom is available if required.
You have access to /dev/ttyACM0 device with native tools.
