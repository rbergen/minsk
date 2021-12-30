# Minsk-2 emulator by [Martin Mareš](https://github.com/gollux)
![CI](https://github.com/rbergen/minsk/actions/workflows/CI.yml/badge.svg)

## Description
This is a version modified by [Rutger van Bergen](https://github.com/rbergen). Main differences from the original, 1.0 version are:
- It addresses a number of compiler warnings in recent versions of GCC
- It does not put a hidden password in memory, unless specifically told to do so using a command-line option
- It supports printing (error) messages in English, besides in Russian
- It supports short command-line options, alongside the long ones
- It emulates a Minsk-2 version with double the memory (see [Minsk-2R Emulation](#minsk-2r-emulation), below)

## Build
1. Make sure you have make, GCC and common libraries installed. On Debian-based Linux distributions, this can be arranged by executing the following command:
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

## Minsk-2R Emulation
The most popular computer in the Minsk family was the Minsk-22, an extended version of the Minsk-2. Amongst others, it featured twice the amount of memory (8192 words instead of Minsk-2's 4096) and came with an extended addressing capability to be able to access it. As far as I've been able to establish, no detailed information about how extended addressing works is available online. Books in which some technical information is available are practically impossible for me to get hold of. Furthermore, if I could get the books, I couldn't read them because I don't speak Russian. 

What I have labeled "Minsk-2R" emulation is my personal best guess at how extended addressing could have been implemented based on the information I do have, most of which is in the original source code and documentation of the Minsk-2 emulator. I have extended the [INSTRUCTIONS](INSTRUCTIONS) document to explain how my implementation works.

According to [a page on the website of the Russian Virtual Computer Museum](https://computer-museum.ru/english/minsk0.htm) there are other differences between Minsk-2 and Minsk-22, including their instruction sets. I have not been able to implement any of those, again due to lack of usable documentation.

If I do get access to digestible documentation on the Minsk-22 at a later time, it is my intention to replace the Minsk-2R emulation with proper emulation of the Minsk-22.