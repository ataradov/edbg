[![Build Status](https://travis-ci.org/ataradov/edbg.svg?branch=master)](https://travis-ci.org/ataradov/edbg)

# CMSIS-DAP programmer (Formerly Atmel EDBG programmer)

This is a simple command line utility for programming ARM-based MCUs
(currently only Atmel) through CMSIS-DAP SWD interface. It works on Linux,
Mac OS X and Windows. It was tested with Atmel mEDBG- and EDBG-based boards,
Atmel-ICE, LPC-Link2 and IBDAP.

## Installation

Simply run `make all` and you will get a small binary, called `edbg`.

## Dependencies

The dependencies are minimal. In addition to normal development tools (GCC, make, etc)
you will need:

 * Windows: none
 * Linux: libudev-dev
 * Mac OS X: libhidapi (built automatically by a Makefile)

## Usage
```
Usage: edbg [options]
Options:
  -h, --help                 print this help message and exit
  -b, --verbose              print verbose messages
  -e, --erase                perform a chip erase before programming
  -p, --program              program the chip
  -v, --verify               verify memory
  -k, --lock                 lock the chip (set security bit)
  -r, --read                 read the contents of the chip
  -f, --file <file>          binary file to be programmed or verified; also read output file name
  -t, --target <name>        specify a target type (use '-t list' for a list of supported target types)
  -l, --list                 list all available debuggers
  -s, --serial <number>      use a debugger with a specified serial number
  -c, --clock <freq>         interface clock frequency in kHz (default 16000)
  -o, --offset <offset>      offset for the operation
  -z, --size <size>          size for the operation
  -F, --fuse <options>       operations on the fuses (use '-h fuse' for details)
```

```
Fuse operations format: <actions>,<index/range>,<value>
  <actions>     - any combination of 'r' (read), 'w' (write), 'v' (verify)
  <index/range> - index of the fuse, or a range of fuses (limits separated by ':')
                  specify ':' to read all fuses
                  specify '*' to read and write values from a file
  <value>       - fuses value or file name for write and verify operations
                  immediate values must be 32 bits or less

Exact fuse bits locations and values are target-dependent.
```

## Examples
```
> edbg -bpv -t atmel_cm7 -f build/Demo.bin
Debugger: ATMEL EDBG CMSIS-DAP ATML2407060200000332 02.01.0157 (S)
Target type: Cortex-M7
Target: SAM V71J21
Programming....,.. done.
Verification....... done.
```

Fuse operations:
```
  -F w,1,1                -- set fuse bit 1
  -F w,8:7,0              -- clear fuse bits 8 and 7
  -F v,31:0,0x12345678    -- verify that fuse bits 31-0 are equal to 0x12345678
  -F wv,5,1               -- set and verify fuse bit 5
  -F r,:,                 -- read all fuses
  -F wv,*,fuses.bin       -- write and verify all fuses from a file
```

