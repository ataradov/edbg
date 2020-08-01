/*
 * Copyright (c) 2013-2019, Alex Taradov <alex@taradov.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*- Includes ----------------------------------------------------------------*/
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "target.h"
#include "edbg.h"
#include "dap.h"
#include "dbg.h"

/*- Definitions -------------------------------------------------------------*/
#define VERSION           "v0.10"

#define MAX_DEBUGGERS     20

#ifndef O_BINARY
#define O_BINARY 0
#endif

/*- Constants ---------------------------------------------------------------*/
static const struct option long_options[] =
{
  { "help",      no_argument,        0, 'h' },
  { "verbose",   no_argument,        0, 'b' },
  { "erase",     no_argument,        0, 'e' },
  { "program",   no_argument,        0, 'p' },
  { "verify",    no_argument,        0, 'v' },
  { "lock",      no_argument,        0, 'k' },
  { "unlock",    no_argument,        0, 'u' },
  { "read",      no_argument,        0, 'r' },
  { "file",      required_argument,  0, 'f' },
  { "target",    required_argument,  0, 't' },
  { "list",      no_argument,        0, 'l' },
  { "serial",    required_argument,  0, 's' },
  { "clock",     required_argument,  0, 'c' },
  { "offset",    required_argument,  0, 'o' },
  { "size",      required_argument,  0, 'z' },
  { "fuse",      required_argument,  0, 'F' },
  { 0, 0, 0, 0 }
};

static const char *short_options = "hbepvkurf:t:ls:c:o:z:F:";

/*- Variables ---------------------------------------------------------------*/
static char *g_serial = NULL;
static bool g_list = false;
static char *g_target = NULL;
static bool g_verbose = false;
static long g_clock = 16000000;

static target_options_t g_target_options =
{
  .erase        = false,
  .program      = false,
  .verify       = false,
  .lock         = false,
  .unlock       = false,
  .read         = false,
  .name         = NULL,
  .offset       = -1,
  .size         = -1,
  .fuse_cmd     = NULL,
};

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
void verbose(char *fmt, ...)
{
  va_list args;

  if (g_verbose)
  {
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    fflush(stdout);
  }
}

//-----------------------------------------------------------------------------
void message(char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);

  fflush(stdout);
}

//-----------------------------------------------------------------------------
void warning(char *fmt, ...)
{
  va_list args;
 
  va_start(args, fmt);
  fprintf(stderr, "Warning: ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
}

//-----------------------------------------------------------------------------
void check(bool cond, char *fmt, ...)
{
  if (!cond)
  {
    va_list args;

    dbg_close();

    va_start(args, fmt);
    fprintf(stderr, "Error: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);

    exit(1);
  }
}

//-----------------------------------------------------------------------------
void error_exit(char *fmt, ...)
{
  va_list args;

  dbg_close();

  va_start(args, fmt);
  fprintf(stderr, "Error: ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);

  exit(1);
}

//-----------------------------------------------------------------------------
void perror_exit(char *text)
{
  dbg_close();
  perror(text);
  exit(1);
}

//-----------------------------------------------------------------------------
int round_up(int value, int multiple)
{
  return ((value + multiple - 1) / multiple) * multiple;
}

//-----------------------------------------------------------------------------
void sleep_ms(int ms)
{
#ifdef _WIN32
  Sleep(ms);
#else
  struct timespec ts;

  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000;

  nanosleep(&ts, NULL);
#endif
}

//-----------------------------------------------------------------------------
void *buf_alloc(int size)
{
  void *buf;

  if (NULL == (buf = malloc(size)))
    error_exit("out of memory");

  memset(buf, 0, size);

  return buf;
}

//-----------------------------------------------------------------------------
void buf_free(void *buf)
{
  free(buf);
}

//-----------------------------------------------------------------------------
int load_file(char *name, uint8_t *data, int size)
{
  struct stat stat;
  int fd, rsize;

  check(NULL != name, "input file name is not specified");

  fd = open(name, O_RDONLY | O_BINARY);

  if (fd < 0)
    perror_exit("open()");

  fstat(fd, &stat);

  if (stat.st_size < size)
    size = stat.st_size;

  rsize = read(fd, data, size);

  if (rsize < 0)
    perror_exit("read()");

  check(rsize == size, "cannot fully read file");

  close(fd);

  return rsize;
}

//-----------------------------------------------------------------------------
void save_file(char *name, uint8_t *data, int size)
{
  int fd, rsize;

  check(NULL != name, "output file name is not specified");

  fd = open(name, O_WRONLY | O_TRUNC | O_CREAT | O_BINARY, 0644);

  if (fd < 0)
    perror_exit("open()");

  rsize = write(fd, data, size);

  if (rsize < 0)
    perror_exit("write()");

  check(rsize == size, "error writing the file");

  close(fd);
}

//-----------------------------------------------------------------------------
static void print_debugger_info(debugger_t *debugger)
{
  uint8_t buf[256];
  char str[512] = "Debugger: ";
  int size;

  size = dap_info(DAP_INFO_VENDOR, buf, sizeof(buf));
  strcat(str, size ? (char *)buf : debugger->manufacturer);
  strcat(str, " ");

  size = dap_info(DAP_INFO_PRODUCT, buf, sizeof(buf));
  strcat(str, size ? (char *)buf : debugger->product);
  strcat(str, " ");

  size = dap_info(DAP_INFO_SER_NUM, buf, sizeof(buf));
  strcat(str, size ? (char *)buf : debugger->serial);
  strcat(str, " ");

  size = dap_info(DAP_INFO_FW_VER, buf, sizeof(buf));
  strcat(str, (char *)buf);
  strcat(str, " ");

  size = dap_info(DAP_INFO_CAPABILITIES, buf, sizeof(buf));
  check(size == 1, "incorrect DAP_INFO_CAPABILITIES size");

  strcat(str, "(");

  if (buf[0] & DAP_CAP_SWD)
    strcat(str, "S");

  if (buf[0] & DAP_CAP_JTAG)
    strcat(str, "J");

  strcat(str, ")\n");

  verbose(str);

  check(buf[0] & DAP_CAP_SWD, "SWD support required");
}

//-----------------------------------------------------------------------------
static void print_clock_freq(int freq)
{
  float value = freq;
  char *unit;

  if (value < 1.0e6)
  {
    value /= 1.0e3;
    unit = "kHz";
  }
  else
  {
    value /= 1.0e6;
    unit = "MHz";
  }

  verbose("Clock frequency: %.1f %s\n", value, unit);
}

//-----------------------------------------------------------------------------
void reconnect_debugger(void)
{
  dap_disconnect();
  dap_connect();
  dap_transfer_configure(0, 128, 128);
  dap_swd_configure(0);
  dap_reset_link();
  dap_swj_clock(g_clock);
  dap_led(0, 1);
}

//-----------------------------------------------------------------------------
static void print_help(char *name)
{
  message("CMSIS-DAP SWD programmer " VERSION ", built " __DATE__ " " __TIME__ " \n\n");

  if (g_target)
  {
    target_ops_t *target_ops = target_get_ops(g_target);

    if (target_ops->help)
      message(target_ops->help);
    else
      message("Specified target does not have a help text.\n");
  }
  else
  {
    message("Usage: %s [options]\n", name);
    message(
      "Options:\n"
      "  -h, --help                 print this help message and exit\n"
      "  -b, --verbose              print verbose messages\n"
      "  -e, --erase                perform a chip erase before programming\n"
      "  -p, --program              program the chip\n"
      "  -v, --verify               verify memory\n"
      "  -k, --lock                 lock the chip (set security bit)\n"
      "  -u, --unlock               unlock the chip (forces chip erase in most cases)\n"
      "  -r, --read                 read the whole content of the chip flash\n"
      "  -f, --file <file>          binary file to be programmed or verified; also read output file name\n"
      "  -t, --target <name>        specify a target type (use '-t list' for a list of supported target types)\n"
      "  -l, --list                 list all available debuggers\n"
      "  -s, --serial <number>      use a debugger with a specified serial number\n"
      "  -c, --clock <freq>         interface clock frequency in kHz (default 16000)\n"
      "  -o, --offset <offset>      offset for the operation\n"
      "  -z, --size <size>          size for the operation\n"
      "  -F, --fuse <options>       operations on the fuses (use '-F help' for details)\n"
    );
  }

  exit(0);
}

//-----------------------------------------------------------------------------
static void print_fuse_help(void)
{
  message(
    "Fuse operations format: <actions><section>,<index/range>,<value>\n"
    "  <actions>     - any combination of 'r' (read), 'w' (write), 'v' (verify)\n"
    "  <section>     - index of the fuse section, may be omitted if device has only\n"
    "                  one section; use '-h -t <target>' for more information\n"
    "  <index/range> - index of the fuse, or a range of fuses (limits separated by ':')\n"
    "                  specify ':' to read all fuses\n"
    "                  specify '*' to read and write values from a file\n"
    "  <value>       - fuses value or file name for write and verify operations\n"
    "                  immediate values must be 32 bits or less\n"
    "\n"
    "Multiple operations may be specified in the same command.\n"
    "They must be separated with a ';'.\n"
    "\n"
    "Exact fuse bits locations and values are target-dependent.\n"
    "\n"
    "Examples:\n"
    "  -F w,1,1             -- set fuse bit 1\n"
    "  -F w,8:7,0           -- clear fuse bits 8 and 7\n"
    "  -F v,31:0,0x12345678 -- verify that fuse bits 31-0 are equal to 0x12345678\n"
    "  -F wv,5,1            -- set and verify fuse bit 5\n"
    "  -F r1,:,             -- read all fuses in a section 1\n"
    "  -F wv,*,fuses.bin    -- write and verify all fuses from a file\n"
    "  -F w0,1,1;w1,5,0     -- set fuse bit 1 in section 0 and\n"
    "                          clear fuse bit 5 in section 1\n"
  );

  exit(0);
}

//-----------------------------------------------------------------------------
static void parse_command_line(int argc, char **argv)
{
  int option_index = 0;
  int c;
  bool help = false;

  while ((c = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1)
  {
    switch (c)
    {
      case 'h': help = true; break;
      case 'e': g_target_options.erase = true; break;
      case 'p': g_target_options.program = true; break;
      case 'v': g_target_options.verify = true; break;
      case 'k': g_target_options.lock = true; break;
      case 'u': g_target_options.unlock = true; break;
      case 'r': g_target_options.read = true; break;
      case 'f': g_target_options.name = optarg; break;
      case 't': g_target = optarg; break;
      case 'l': g_list = true; break;
      case 's': g_serial = optarg; break;
      case 'c': g_clock = strtoul(optarg, NULL, 0) * 1000; break;
      case 'b': g_verbose = true; break;
      case 'o': g_target_options.offset = (uint32_t)strtoul(optarg, NULL, 0); break;
      case 'z': g_target_options.size = (uint32_t)strtoul(optarg, NULL, 0); break;
      case 'F': g_target_options.fuse_cmd = optarg; break;
      default: exit(1); break;
    }
  }

  if (help)
    print_help(argv[0]);

  if (g_target_options.fuse_cmd && 0 == strcmp(g_target_options.fuse_cmd, "help"))
    print_fuse_help();

  check(optind >= argc, "malformed command line, use '-h' for more information");
}

//-----------------------------------------------------------------------------
int main(int argc, char **argv)
{
  debugger_t debuggers[MAX_DEBUGGERS];
  int n_debuggers = 0;
  int debugger = -1;
  target_ops_t *target_ops;

  parse_command_line(argc, argv);

  if (!(g_target_options.erase || g_target_options.program || g_target_options.verify ||
      g_target_options.lock || g_target_options.read || g_target_options.fuse_cmd ||
      g_list || g_target))
    error_exit("no actions specified");

  if (g_target_options.read && (g_target_options.erase || g_target_options.program ||
      g_target_options.verify || g_target_options.lock))
    error_exit("mutually exclusive actions specified");

  n_debuggers = dbg_enumerate(debuggers, MAX_DEBUGGERS);

  if (g_list)
  {
    message("Attached debuggers:\n");
    for (int i = 0; i < n_debuggers; i++)
      message("  %s - %s %s\n", debuggers[i].serial, debuggers[i].manufacturer, debuggers[i].product);
    return 0;
  }

  if (NULL == g_target)
    error_exit("no target type specified (use '-t' option)");

  if (0 == strcmp(g_target, "list"))
  {
    target_list();
    return 0;
  }

  target_ops = target_get_ops(g_target);

  if (g_serial)
  {
    for (int i = 0; i < n_debuggers; i++)
    {
      if (0 == strcmp(debuggers[i].serial, g_serial))
      {
        debugger = i;
        break;
      }
    }

    if (-1 == debugger)
      error_exit("unable to find a debugger with a specified serial number");
  }

  if (0 == n_debuggers)
    error_exit("no debuggers found");
  else if (1 == n_debuggers)
    debugger = 0;
  else if (n_debuggers > 1 && -1 == debugger)
    error_exit("more than one debugger found, please specify a serial number");

  dbg_open(&debuggers[debugger]);

  print_debugger_info(&debuggers[debugger]);
  print_clock_freq(g_clock);

  reconnect_debugger();

  target_ops->select(&g_target_options);

  if (g_target_options.unlock)
  {
    verbose("Unlocking... ");
    target_ops->unlock();
    verbose(" done.\n");
  }

  if (g_target_options.erase)
  {
    verbose("Erasing... ");
    target_ops->erase();
    verbose(" done.\n");
  }

  if (g_target_options.program)
  {
    verbose("Programming...");
    target_ops->program();
    verbose(" done.\n");
  }

  if (g_target_options.verify)
  {
    verbose("Verification...");
    target_ops->verify();
    verbose(" done.\n");
  }

  if (g_target_options.lock)
  {
    verbose("Locking... ");
    target_ops->lock();
    verbose(" done.\n");
  }

  if (g_target_options.read)
  {
    verbose("Reading...");
    target_ops->read();
    verbose(" done.\n");
  }

  if (g_target_options.fuse_cmd)
  {
    verbose("Fuses:\n");
    target_fuse_commands(target_ops, g_target_options.fuse_cmd);
    verbose("done.\n");
  }

  target_ops->deselect();

  dap_reset_target_hw(1);

  dap_disconnect();
  dap_led(0, 0);

  dbg_close();

  return 0;
}

