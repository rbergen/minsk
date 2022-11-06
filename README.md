# Minsk-2 emulator with Minsk-22 support

[![CI](https://github.com/rbergen/minsk/actions/workflows/CI.yml/badge.svg)](https://github.com/rbergen/minsk/actions/workflows/CI.yml)

## Description

The minsk emulator in this repository was originally developed by [Martin Mareš](https://github.com/gollux) in 2010.

This is a version modified by [Rutger van Bergen](https://github.com/rbergen). With the adoption of support for the Minsk-22 in [the oiginal emulator](https://mj.ucw.cz/minsk/readme.html), both are functionally identical.

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

## Use

The emulator reads its input from stdin. Loading and executing the ex-hello example program would therefore be done like this:

```text
./minsk < ex-hello
```

Any output written to the emulated printer will be directed to stdout, as is any tracing information.

The list of supported options can be acquired by running the emulator with any unsupported option:

```text
./minsk -h
```

## Input format

The input to the emulator follows a fairly simple format:

```text
; An example

@0050
-62 00 7000 1000
-62 00 7006 1001
-62 00 7400 0000
-00 00 0000 0000
@1000
+65 45 53 53 56 17
+42 56 60 53 44 16
```

Empty lines and lines starting with a semicolon are ignored. `@xxxx` sets the memory address (in octal), all other lines specify signed 36-bit octal values to be written to consecutive memory cells. Spaces inside numbers are purely decorative and the parser ignores them.

## Documentation

Introductory documentation for the emulator can be found in the [readme.html](https://htmlpreview.github.io/?https://github.com/rbergen/minsk/blob/master/readme.html) file.

The [INSTRUCTIONS](INSTRUCTIONS) file contains more technical information about the Minsk-2/Minsk-22, its instruction set and their implementation in the emulator.

## Original emulator

The original emulator can be [used on-line](https://mj.ucw.cz/minsk/).
