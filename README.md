A work-in-progress read-only(/archival) disk controller for CDC BK4XX/BK5XX (9760/9762) Storage Module Drives. It requires a Raspberry Pi Pico (RP2040) and some custom differential line driver/receiver hardware, and allows a PC to control the drive and receive status and data from it. The PC software ("frontend") also doubles as a debugging/diagnostics tool since it allows you to send arbitrary drive commands, has graphical display of drive status, and it logs what it sees. It is part of an ongoing effort to archive disk packs for a CDC BK5XX/9762 80MB drive at [datamuseum.dk](https://datamuseum.dk)

## Hardware
Pico pin config:
 - [`pin_config.html`](https://htmlpreview.github.io/?https://github.com/Datamuseum-DK/pico-smd-controller/blob/master/doc/pin_config.html)
 - [`pin_config.h`](pin_config.h)

Drive documentation:
 - [General Description - Operation - Theory of Operation - Discrete Component Circuits - 8332200 - BK4xx - BK5xx - 9760 - 9762.pdf](doc/references/General%20Description%20-%20Operation%20-%20Theory%20of%20Operation%20-%20Discrete%20Component%20Circuits%20-%208332200%20-%20BK4xx%20-%20BK5xx%20-%209760%20-%209762.pdf)

Drive signals are balanced (both inputs and outputs) and need differential line receivers/drivers between the Pico pins and the drive. (TODO schematics/parts list)

## Building

#### Controller
Run `build.sh`. Requirements: `cmake`, `git`, `arm-none-eabi-gcc`.

#### Frontend
Run `make` in `frontend_graphical/` directory. Requirements: `termios.h`, SDL2, OpenGL2. (SDL2/OpenGL2 are replacable, see: https://github.com/ocornut/imgui/tree/master/examples)

## Running
 - If the Pico runs the controller code it will flash the LED briefly on startup, and it should announce itself as a TTY-over-USB device (typically `/dev/ttyACM0` on Linux)
 - Run the "frontend"; pass the path to the TTY as argument (it can't run without a Pico, but you can test a lot of functionality without a drive).

## License/Credit
 - Dear ImGui ([`frontend_graphical/im_*`](frontend_graphical/)) is [MIT licensed by Omar Cornut](LICENSE.imgui)
 - [`frontend_graphical/stb_ds.h` by Sean Barrett is MIT/unlicensed](frontend_graphical/stb_ds.h)
 - [`frontend_graphical/Inconsolata-Medium.ttf`](frontend_graphical/Inconsolata-Medium.ttf) by The Inconsolata Project Authors is [OFL licensed](LICENSE.Inconsolata)
 - The rest by me is [unlicensed](LICENSE.spc) though I may have peeked at the [Pico Examples](https://github.com/raspberrypi/pico-examples)
