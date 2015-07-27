# Atmel EDBG prgammer

This is a simple command line utility for programming Atmel MCUs though EDBG interface.
It works on Linux, Mac OS X and Windows.

## Installation

Simply run 'make all' and you will get a small binary.

## Dependencies

The dependencies are minimal. In addition to normal develplement tools (GCC, make, etc)
you will need:

Windows: none
Linux: libudev-dev
Mac OS X: libhidapi (built automatically by a Makefile)

## Usage
```
Usage: edbg [options]
Options:
  -h, --help                 print this help message and exit
  -e, --erase                perform a chip erase before programming
  -p, --program              program the chip
  -v, --verify               verify memory
  -k, --lock                 lock the chip (set security bit)
  -r, --read                 read the contents of the chip
  -f, --file <file>          binary file to be programmed or verified
  -l, --list                 list all available debuggers
  -a, --all                  execute the commands against all found debuggers
  -s, --serial <number>      use a debugger with a specified serial number
  -b, --verbose              print verbose messages
```

## Examples
```
> edbg -bpvf build/Demo.bin
Debugger: ATMEL EDBG CMSIS-DAP ATML2407060200000332 02.01.0157 (S)
Target type: Cortex-M7
Target: SAM V71J21
Programming....,.. done.
Verification....... done.
```
