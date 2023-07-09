Attempt to write a read-only disk controller for a CDC 9762 Storage Module
Drive running on a Raspberry Pi Pico (RP2040).

Reference documentation for this family of drives:
https://bitsavers.org/pdf/cdc/discs/smd/83322200M_CDC_BK4XX_BK5XX_Hardware_Reference_Manual_Jun1980.pdf

## Pin config
 - https://htmlpreview.github.io/?https://github.com/sqaxomonophonen/smd-pico-controller/blob/master/doc/pin_config.html
 - https://github.com/sqaxomonophonen/smd-pico-controller/blob/master/config.h

## Building
Run `build.sh` (requirements: `cmake`, `git`, `arm-none-eabi-gcc`).
