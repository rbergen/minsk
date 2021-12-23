# Minsk-2 emulator by [Martin Mareš](https://github.com/gollux)

## Description
This is a version modified by [Rutger van Bergen](https://github.com/rbergen). Main differences from the original, 1.0 version are:
- It addresses a number of compiler warnings in recent versions of GCC
- It does not put a hidden password in memory, unless specifically told to do so using a command-line option
- It supports printing (error) messages in English, besides in Russian
- It supports short command-line options, alongside the long ones

## Build
1. Make sure you have make, GCC and common libraries installed. On Debian-based Linux distributions, this can be arranged by executing the following commandL
   ```
   sudo apt-get update && sudo apt-get -y build-essential
   ```
2. Download (and, if applicable, extract) the emulator sources.
3. Issue the following command from the directory containing the emulator source files:
   ```
   make
   ```

The emulator executable will be called `minsk` if the build succeeds.

## Documentation
Original documentation for the emulator can be found in the [readme.html](https://htmlpreview.github.io/?https://github.com/rbergen/minsk/blob/master/readme.html) file. The original version of the emulator can be [used on-line](https://mj.ucw.cz/minsk/).

