# Minsk-2 emulator with Minsk-22 support

## Description

The minsk emulator in this repository was originally developed by [Martin Mareš](https://github.com/gollux) in 2010.

This is a version modified by [Rutger van Bergen](https://github.com/rbergen). The main difference from the original version is that this one includes support for the Minsk-22, which is basically a version of the Minsk-2 with double the memory and additional I/O devices. As I/O device support in the emulator is very limited, only the difference in memory is relevant in this context.

## Build

1. Make sure you have make, GCC and common libraries installed. On Debian-based Linux distributions, this can be arranged by executing the following command:

   ```text
   sudo apt-get update && sudo apt-get -y build-essential
   ```

2. Download (and, if applicable, extract) the emulator sources.

3. Issue the following command from the directory containing the emulator source files:

   ```text
   make
   ```

The emulator executable will be called `minsk` if the build succeeds.

## Documentation

Introductory documentation for the emulator can be found in the [readme.html](https://htmlpreview.github.io/?https://github.com/rbergen/minsk/blob/master/readme.html) file.

The [INSTRUCTIONS](INSTRUCTIONS) file contains more technical information about the Minsk-2/Minsk-22, its instruction set and their implementation in the emulator.

## Original version

The original version of the emulator can be [used on-line](https://mj.ucw.cz/minsk/).
