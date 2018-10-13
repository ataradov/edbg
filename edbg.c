/*
 * Copyright (c) 2013-2017, Alex Taradov <alex@taradov.com>
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
#define VERSION           "v0.7"

#define MAX_DEBUGGERS     20

#ifndef O_BINARY
#define O_BINARY 0
#endif

/*- Types -------------------------------------------------------------------*/

/*- Variables ---------------------------------------------------------------*/
static const struct option long_options[] =
{
  { "help",      no_argument,        0, 'h' },
  { "verbose",   no_argument,        0, 'b' },
  { "erase",     no_argument,        0, 'e' },
  { "program",   no_argument,        0, 'p' },
  { "verify",    no_argument,        0, 'v' },
  { "lock",      no_argument,        0, 'k' },
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

static const char *short_options = "hbepvkrf:t:ls:c:o:z:F:";

static char *g_serial = NULL;
static bool g_list = false;
static char *g_target = NULL;
static bool g_verbose = false;
static long g_clock = 16000000;

static target_options_t g_target_options =
{
  .erase       = false,
  .program     = false,
  .verify      = false,
  .lock        = false,
  .read        = false,
  .fuse        = false,
  .fuse_read   = false,
  .fuse_write  = false,
  .fuse_verify = false,
  .fuse_name   = NULL,
  .name        = NULL,
  .offset      = -1,
  .size        = -1,
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
uint32_t extract_value(uint8_t *buf, int start, int end)
{
  uint32_t value = 0;
  int bit = start;
  int index = 0;

  do
  {
    int by = bit / 8;
    int bt = bit % 8;

    if (buf[by] & (1 << bt))
      value |= (1 << index);

    bit++;
    index++;
  } while (bit <= end);

  return value;
}

//-----------------------------------------------------------------------------
void apply_value(uint8_t *buf, uint32_t value, int start, int end)
{
  int bit = start;
  int index = 0;

  do
  {
    int by = bit / 8;
    int bt = bit % 8;

    if (value & (1 << index))
      buf[by] |= (1 << bt);
    else
      buf[by] &= ~(1 << bt);

    bit++;
    index++;
  } while (bit <= end);
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
static void print_help(char *name, char *param)
{
  printf("CMSIS-DAP SWD programmer " VERSION ", built " __DATE__ " " __TIME__ " \n\n");

  if (0 == strcmp(param, "fuse"))
  {
    printf("Fuse operations format: <actions>,<index/range>,<value>\n");
    printf("  <actions>     - any combination of 'r' (read), 'w' (write), 'v' (verify)\n");
    printf("  <index/range> - index of the fuse, or a range of fuses (limits separated by ':')\n");
    printf("                  specify ':' to read all fuses\n");
    printf("                  specify '*' to read and write values from a file\n");
    printf("  <value>       - fuses value or file name for write and verify operations\n");
    printf("                  immediate values must be 32 bits or less\n");
    printf("\n");
    printf("Exact fuse bits locations and values are target-dependent.\n");
    printf("\n");
    printf("Examples:\n");
    printf("  -F w,1,1                -- set fuse bit 1\n");
    printf("  -F w,8:7,0              -- clear fuse bits 8 and 7\n");
    printf("  -F v,31:0,0x12345678    -- verify that fuse bits 31-0 are equal to 0x12345678\n");
    printf("  -F wv,5,1               -- set and verify fuse bit 5\n");
    printf("  -F r,:,                 -- read all fuses\n");
    printf("  -F wv,*,fuses.bin       -- write and verify all fuses from a file\n");
  }
  else
  {
    printf("Usage: %s [options]\n", name);
    printf("Options:\n");
    printf("  -h, --help                 print this help message and exit\n");
    printf("  -b, --verbose              print verbose messages\n");
    printf("  -e, --erase                perform a chip erase before programming\n");
    printf("  -p, --program              program the chip\n");
    printf("  -v, --verify               verify memory\n");
    printf("  -k, --lock                 lock the chip (set security bit)\n");
    printf("  -r, --read                 read the whole content of the chip flash\n");
    printf("  -f, --file <file>          binary file to be programmed or verified; also read output file name\n");
    printf("  -t, --target <name>        specify a target type (use '-t list' for a list of supported target types)\n");
    printf("  -l, --list                 list all available debuggers\n");
    printf("  -s, --serial <number>      use a debugger with a specified serial number\n");
    printf("  -c, --clock <freq>         interface clock frequency in kHz (default 16000)\n");
    printf("  -o, --offset <offset>      offset for the operation\n");
    printf("  -z, --size <size>          size for the operation\n");
    printf("  -F, --fuse <options>       operations on the fuses (use '-h fuse' for details)\n");
  }

  exit(0);
}

//-----------------------------------------------------------------------------
static void parse_fuse_options(char *str)
{
  bool expect_name = false;

  while (*str)
  {
    if ('r' == *str)
      g_target_options.fuse_read = true;
    else if ('w' == *str)
      g_target_options.fuse_write = true;
    else if ('v' == *str)
      g_target_options.fuse_verify = true;
    else
      break;

    g_target_options.fuse = true;

    str++;
  }

  check(g_target_options.fuse, "no fuse operations spefified");

  if (',' == *str)
  {
    str++;

    if ('*' == *str)
    {
      str++;
      expect_name = true;
    }
    else if (':' == *str)
    {
      str++;
      g_target_options.fuse_end = -1;
      g_target_options.fuse_start = -1;
    }
    else
    {
      g_target_options.fuse_end = (uint32_t)strtoul(str, &str, 0);

      if (':' == *str)
      {
        str++;
        g_target_options.fuse_start = (uint32_t)strtoul(str, &str, 0);
      }
      else
      {
        g_target_options.fuse_start = g_target_options.fuse_end;
      }
    }
  }
  else
  {
    error_exit("fuse index is required");
  }

  if (',' == *str)
  {
    str++;

    if (expect_name)
      g_target_options.fuse_name = strdup(str);
    else
      g_target_options.fuse_value = (uint32_t)strtoul(str, &str, 0);
  }
  else if (g_target_options.fuse_write || g_target_options.fuse_verify)
  {
    error_exit("value or name is required for fuse write and verify operations");
  }

  check(expect_name || 0 == *str, "junk at the end of fuse operations: '%s'", str);

  check(g_target_options.fuse_end >= g_target_options.fuse_start,
      "fuse bit range must be specified in a descending order");

  check((g_target_options.fuse_end - g_target_options.fuse_start) <= 32,
      "fuse bit range must be 32 bits or less");
}

//-----------------------------------------------------------------------------
static void parse_command_line(int argc, char **argv)
{
  int option_index = 0;
  int c;

  while ((c = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1)
  {
    switch (c)
    {
      case 'h': print_help(argv[0], (optind < argc) ? argv[optind] : ""); break;
      case 'e': g_target_options.erase = true; break;
      case 'p': g_target_options.program = true; break;
      case 'v': g_target_options.verify = true; break;
      case 'k': g_target_options.lock = true; break;
      case 'r': g_target_options.read = true; break;
      case 'f': g_target_options.name = optarg; break;
      case 't': g_target = optarg; break;
      case 'l': g_list = true; break;
      case 's': g_serial = optarg; break;
      case 'c': g_clock = strtoul(optarg, NULL, 0) * 1000; break;
      case 'b': g_verbose = true; break;
      case 'o': g_target_options.offset = (uint32_t)strtoul(optarg, NULL, 0); break;
      case 'z': g_target_options.size = (uint32_t)strtoul(optarg, NULL, 0); break;
      case 'F': parse_fuse_options(optarg); break;
      default: exit(1); break;
    }
  }

  check(optind >= argc, "malformed command line, use '-h' for more information");
}

//-----------------------------------------------------------------------------
int main(int argc, char **argv)
{
  debugger_t debuggers[MAX_DEBUGGERS];
  int n_debuggers = 0;
  int debugger = -1;
  target_t *target;

  parse_command_line(argc, argv);

  if (!(g_target_options.erase || g_target_options.program || g_target_options.verify ||
      g_target_options.lock || g_target_options.read || g_target_options.fuse ||
      g_list || g_target))
    error_exit("no actions specified");

  if (g_target_options.read && (g_target_options.erase || g_target_options.program ||
      g_target_options.verify || g_target_options.lock))
    error_exit("mutually exclusive actions specified");

  n_debuggers = dbg_enumerate(debuggers, MAX_DEBUGGERS);

  if (g_list)
  {
    printf("Attached debuggers:\n");
    for (int i = 0; i < n_debuggers; i++)
      printf("  %s - %s %s\n", debuggers[i].serial, debuggers[i].manufacturer, debuggers[i].product);
    return 0;
  }

  if (NULL == g_target)
    error_exit("no target type specified (use '-t' option)");

  if (0 == strcmp("list", g_target))
  {
    target_list();
    return 0;
  }

  target = target_get_ops(g_target);

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

  dap_reset_target_hw(1);

  dap_disconnect();
  dap_get_debugger_info();
  dap_connect();
  dap_transfer_configure(0, 128, 128);
  dap_swd_configure(0);
  dap_led(0, 1);
  dap_reset_link();
  dap_swj_clock(g_clock);
  dap_target_prepare();

  print_clock_freq(g_clock);

  target->ops->select(&g_target_options);

  if (g_target_options.erase)
  {
    verbose("Erasing... ");
    target->ops->erase();
    verbose(" done.\n");
  }

  if (g_target_options.program)
  {
    verbose("Programming...");
    target->ops->program();
    verbose(" done.\n");
  }

  if (g_target_options.verify)
  {
    verbose("Verification...");
    target->ops->verify();
    verbose(" done.\n");
  }

  if (g_target_options.lock)
  {
    verbose("Locking... ");
    target->ops->lock();
    verbose(" done.\n");
  }

  if (g_target_options.read)
  {
    verbose("Reading...");
    target->ops->read();
    verbose(" done.\n");
  }

  if (g_target_options.fuse)
  {
    if (g_target_options.fuse_name)
    {
      if (g_target_options.fuse_read && (g_target_options.fuse_write ||
          g_target_options.fuse_verify))
      error_exit("mutually exclusive fuse actions specified");
    }

    verbose("Fuse ");

    if (g_target_options.fuse_read)
    {
      verbose("read");
    }

    if (g_target_options.fuse_write)
    {
      if (g_target_options.fuse_read)
        verbose(", ");

      verbose("write");
    }

    if (g_target_options.fuse_verify)
    {
      if (g_target_options.fuse_write)
        verbose(", ");

      verbose("verify");
    }

    if (g_target_options.fuse_name || -1 == g_target_options.fuse_end)
    {
      verbose(" all");
    }
    else if (g_target_options.fuse_start == g_target_options.fuse_end)
    {
      verbose(" bit %d", g_target_options.fuse_start);
    }
    else
    {
      verbose(" bits %d:%d", g_target_options.fuse_end,
          g_target_options.fuse_start);
    }

    verbose(", ");

    if (g_target_options.fuse_name)
    {
      verbose("file '%s'\n", g_target_options.fuse_name);
    }
    else
    {
      verbose("value 0x%x (%u)\n", g_target_options.fuse_value,
          g_target_options.fuse_value);
    }

    target->ops->fuse();

    verbose("done.\n");
  }

  target->ops->deselect();

  dap_reset_target_hw(1);

  dap_disconnect();
  dap_led(0, 0);

  dbg_close();

  return 0;
}

