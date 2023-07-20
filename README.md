Attempt to write a read-only disk controller for a CDC 9762 Storage Module
Drive running on a Raspberry Pi Pico (RP2040).

Reference documentation for this family of drives:
https://bitsavers.org/pdf/cdc/discs/smd/83322200M_CDC_BK4XX_BK5XX_Hardware_Reference_Manual_Jun1980.pdf

## Hardware
Pico pin config:
 - https://htmlpreview.github.io/?https://github.com/sqaxomonophonen/smd-pico-controller/blob/master/doc/pin_config.html
 - https://github.com/sqaxomonophonen/smd-pico-controller/blob/master/pin_config.h

Drive signals are balanced (both inputs and outputs) and need differential line receivers/drivers between the Pico pins and the drive. (TODO schematics/parts list)

## Building

#### Controller
Run `build.sh`. Requirements: `cmake`, `git`, `arm-none-eabi-gcc`.

#### Frontend
Run `make` in `frontend_graphical/` directory. Requirements: `termios.h`, SDL2, OpenGL2. (SDL2/OpenGL2 are replacable, see: https://github.com/ocornut/imgui/tree/master/examples)

## Running
 - If the Pico runs the controller code it will flash the LED briefly on startup, and it should announce itself as a TTY-over-USB device (typically `/dev/ttyACM0` on Linux)
 - Run the "frontend"; pass the path to the TTY as argument (it can't run without a Pico, but you can test a lot of functionality without a drive).
