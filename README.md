# CMSIS-DAP programmer (Formerly Atmel EDBG programmer)

This is a simple command line utility for programming ARM-based MCUs
(currently only Atmel) though CMSIS-DAP SWD interface. It works on Linux,
Mac OS X and Windows. It was tested with Atmel mEDBG- and EDBG-based boards,
Atmel-ICE, LPC-Link2 and IBDAP.

## Installation

Simply run 'make all' and you will get a small binary.

## Dependencies

The dependencies are minimal. In addition to normal develplement tools (GCC, make, etc)
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
  -f, --file <file>          binary file to be programmed or verified
  -t, --target <name>        specify a traget type (use '-t list' for a list of supported target types)
  -l, --list                 list all available debuggers
  -s, --serial <number>      use a debugger with a specified serial number
  -o, --offset <number>      offset for the operation
  -z, --size <number>        size for the operation
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


