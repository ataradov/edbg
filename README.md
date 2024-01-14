[![Build Status](https://github.com/ataradov/edbg/actions/workflows/build.yml/badge.svg)](https://github.com/ataradov/edbg/actions)

# CMSIS-DAP programmer

This is a simple command line utility for programming ARM-based MCUs
through CMSIS-DAP SWD interface. It works on Linux, Mac OS X and Windows.
It was tested with Atmel mEDBG- and EDBG-based boards, Atmel-ICE, LPC-Link2, IBDAP and
[Free-DAP](https://github.com/ataradov/free-dap)-based debuggers.

## Installation

Binary releases can be downloaded from [here](https://taradov.com/bin/edbg/).
Binary releases are not tested, they are a result of automated build process. Please report if you
see any issues with them.

If you want to build from the source code, simply run `make all` and you will get a small binary, called `edbg`.

## Dependencies

The dependencies are minimal. In addition to normal development tools (GCC, make, etc)
you will need:

 * Windows: none
 * Linux: libudev-dev
 * Mac OS X: none

## Usage
```
Usage: edbg [options]
Options:
  -h, --help                 print this help message and exit
  -b, --verbose              print verbose messages
  -d, --version <version>    use a specified CMSIS-DAP version (default is best available)
  -x, --reset <duration>     assert the reset pin before any other operation (duration in ms)
  -e, --erase                perform a chip erase before programming
  -p, --program              program the chip
  -v, --verify               verify memory
  -k, --lock                 lock the chip (set security bit)
  -u, --unlock               unlock the chip (forces chip erase in most cases)
  -r, --read                 read the whole content of the chip flash
  -f, --file <file>          binary file to be programmed or verified; also read output file name
  -t, --target <name>        specify a target type (use '-t list' for a list of supported target types)
  -l, --list                 list all available debuggers
  -s, --serial <number>      use a debugger with a specified serial number
  -c, --clock <freq>         interface clock frequency in kHz (default 16000)
  -o, --offset <offset>      offset for the operation
  -z, --size <size>          size for the operation
  -F, --fuse <options>       operations on the fuses (use '-F help' for details)
```

```
Fuse operations format: <actions><section>,<index/range>,<value>
  <actions>     - any combination of 'r' (read), 'w' (write), 'v' (verify)
  <section>     - index of the fuse section, may be omitted if device has only
                  one section; use '-h -t <target>' for more information
  <index/range> - index of the fuse, or a range of fuses (limits separated by ':')
                  specify ':' to read all fuses
                  specify '*' to read and write values from a file
  <value>       - fuses value or file name for write and verify operations
                  immediate values must be 32 bits or less
```
Multiple operations may be specified in the same command.
They must be separated with a ';'.

Exact fuse bits locations and values are target-dependent.

## Examples
```
>edbg -b -t samd11 -pv -f build/Demo.bin
Debugger: ATMEL EDBG CMSIS-DAP ATML2178031800000312 01.1A.00FB (S)
Clock frequency: 16.0 MHz
Target: SAM D11D14A (Rev B)
Programming............................................... done.
Verification............................................... done.

```

Fuse operations:
```
  -F w,1,1             -- set fuse bit 1
  -F w,8:7,0           -- clear fuse bits 8 and 7
  -F v,31:0,0x12345678 -- verify that fuse bits 31-0 are equal to 0x12345678
  -F wv,5,1            -- set and verify fuse bit 5
  -F r1,:,             -- read all fuses in a section 1
  -F wv,*,fuses.bin    -- write and verify all fuses from a file
  -F w0,1,1;w1,5,0     -- set fuse bit 1 in section 0 and clear fuse bit 5 in section 1
```

