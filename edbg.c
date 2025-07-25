// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2013-2024, Alex Taradov <alex@taradov.com>. All rights reserved.

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
#define MAX_DEBUGGERS     20

#ifndef O_BINARY
#define O_BINARY 0
#endif

/*- Constants ---------------------------------------------------------------*/
static const struct option long_options[] =
{
  { "help",      no_argument,        0, 'h' },
  { "verbose",   no_argument,        0, 'b' },
  { "version",   required_argument,  0, 'd' },
  { "reset",     required_argument,  0, 'x' },
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

static const char *short_options = "hbd:x:epvkurf:t:ls:c:o:z:F:";

/*- Variables ---------------------------------------------------------------*/
static char *g_serial = NULL;
static bool g_list    = false;
static char *g_target = NULL;
static bool g_verbose = false;
static int  g_version = -1;
static long g_clock   = 16000000;
static bool g_debugger_open = false;

static target_options_t g_target_options =
{
  .reset        = 0,
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
  if (cond)
    return;

  va_list args;

  va_start(args, fmt);
  fprintf(stderr, "Error: ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);

  exit(1);
}

//-----------------------------------------------------------------------------
void error_exit(char *fmt, ...)
{
  va_list args;

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
uint8_t *mem_find(uint8_t *haystack, int haystack_size, uint8_t *needle, int needle_size)
{
  if (haystack_size == 0 || needle_size == 0 || haystack_size < needle_size)
    return NULL;

  for (int i = 0; i < (haystack_size - needle_size); i++)
  {
    if (memcmp(haystack + i, needle, needle_size) == 0)
      return haystack + i;
  }

  return NULL;
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

  size = dap_info(DAP_INFO_CMSIS_DAP_VER, buf, sizeof(buf));
  strcat(str, (char *)buf);
  strcat(str, " ");

  size = dap_info(DAP_INFO_CAPABILITIES, buf, sizeof(buf));
  check(size == 1 || size == 2, "incorrect DAP_INFO_CAPABILITIES size");

  strcat(str, "(");

  if (buf[0] & DAP_CAP_SWD)
    strcat(str, "S");

  if (buf[0] & DAP_CAP_JTAG)
    strcat(str, "J");

  strcat(str, ", ");

  if (debugger->versions & DBG_CMSIS_DAP_V1)
    strcat(str, "1");

  if (debugger->versions & DBG_CMSIS_DAP_V2)
    strcat(str, "2");

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
static void disconnect_debugger(void)
{
  dap_led(0, 0);
  dap_disconnect();
  dbg_close();
}

//-----------------------------------------------------------------------------
static void reconnect_debugger(void)
{
  dap_disconnect();
  dap_connect(DAP_INTERFACE_SWD);
  dap_transfer_configure(0, 32768, 128);
  dap_swd_configure(0);
  dap_swj_clock(g_clock);
  dap_led(0, 1);
}

//-----------------------------------------------------------------------------
static void print_help(char *name)
{
  message("CMSIS-DAP SWD programmer. Built " __DATE__ " " __TIME__ ".\n\n");

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
      "  -d, --version <version>    use a specified CMSIS-DAP version (default is best available)\n"
      "  -x, --reset <duration>     assert the reset pin before any other operation (duration in ms)\n"
      "  -e, --erase                perform a chip erase before programming\n"
      "  -p, --program              program the chip\n"
      "  -v, --verify               verify memory\n"
      "  -k, --lock                 lock the chip (set security bit)\n"
      "  -u, --unlock               unlock the chip (forces chip erase in most cases)\n"
      "  -r, --read                 read the whole content of the chip flash\n"
      "  -f, --file <file>          binary file to be programmed or verified; also read output file name\n"
      "  -t, --target <name>        specify a target type (use '-t list' for a list of supported target types)\n"
      "  -l, --list                 list all available debuggers\n"
      "  -s, --serial <number>      use a debugger with a specified serial number or index in the list\n"
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
      case 'x': g_target_options.reset = strtoul(optarg, NULL, 0); break;
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
      case 'd': g_version = strtoul(optarg, NULL, 0); break;
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
  debugger_t debuggers[MAX_DEBUGGERS] = {0};
  int n_debuggers = 0;
  int debugger = -1;
  target_ops_t *target_ops;
  bool active_actions;

  parse_command_line(argc, argv);

  active_actions = g_target_options.unlock || g_target_options.erase ||
      g_target_options.program || g_target_options.verify || g_target_options.lock ||
      g_target_options.read || g_target_options.fuse_cmd;

  if (!(active_actions || g_list || g_target || (g_target_options.reset == 0)))
    error_exit("no actions specified");

  if (g_target_options.read && (g_target_options.erase || g_target_options.program ||
      g_target_options.verify || g_target_options.lock))
    error_exit("mutually exclusive actions specified");

  n_debuggers = dbg_enumerate(debuggers, MAX_DEBUGGERS);

  if (g_list)
  {
    message("Attached debuggers:\n");

    for (int i = 0; i < n_debuggers; i++)
    {
      char ver[8] = "";

      if (debuggers[i].versions & DBG_CMSIS_DAP_V1)
        strcat(ver, "1");

      if (debuggers[i].versions & DBG_CMSIS_DAP_V2)
        strcat(ver, "2");

      message("  %d: %s - %s %s (%s)\n", i, debuggers[i].serial, debuggers[i].manufacturer, debuggers[i].product, ver);
    }

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
    char *end = NULL;
    int index = strtoul(g_serial, &end, 10);

    if (index < n_debuggers && end[0] == 0)
    {
      debugger = index;
    }
    else
    {
      for (int i = 0; i < n_debuggers; i++)
      {
        if (0 == strcmp(debuggers[i].serial, g_serial))
        {
          debugger = i;
          break;
        }
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

  if (-1 == g_version)
    g_version = (debuggers[debugger].versions & DBG_CMSIS_DAP_V2) ? DBG_CMSIS_DAP_V2 : DBG_CMSIS_DAP_V1;
  else if (1 == g_version)
    g_version = DBG_CMSIS_DAP_V1;
  else if (2 == g_version)
    g_version = DBG_CMSIS_DAP_V2;
  else
    error_exit("unsupported CMSIS-DAP version: %d", g_version);

  if (0 == (g_version & debuggers[debugger].versions))
    error_exit("selected debugger does not support this CMSIS-DAP version");

  dbg_open(&debuggers[debugger], g_version);

  g_debugger_open = true;

  print_debugger_info(&debuggers[debugger]);
  verbose("Using CMSIS-DAP v%d\n", (DBG_CMSIS_DAP_V1 == g_version) ? 1 : 2);
  print_clock_freq(g_clock);

  reconnect_debugger();

  if (g_target_options.reset > 0)
  {
    verbose("Resetting...");
    dap_reset_pin(0);
    sleep_ms(g_target_options.reset);
    dap_reset_pin(1);
    sleep_ms(10);
    verbose(" done.\n");
  }

  if (!active_actions)
  {
    disconnect_debugger();
    return 0;
  }

  target_ops->select(&g_target_options);

  if (g_target_options.unlock)
  {
    verbose("Unlocking...");
    target_ops->unlock();
    verbose(" done.\n");
  }

  if (g_target_options.erase)
  {
    verbose("Erasing...");
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
    verbose("Locking...");
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

  disconnect_debugger();

  return 0;
}

