COMPILER ?= gcc
UNAME ?= $(shell uname)

SRCS = \
  dap.c \
  edbg.c \
  target.c \
  target_atmel_cm0p.c \
  target_atmel_cm3.c \
  target_atmel_cm4.c \
  target_atmel_cm7.c \
  target_atmel_cm4v2.c \
  target_mchp_cm23.c \
  target_st_stm32g0.c \
  target_gd_gd32f4xx.c \
  target_nu_m480.c \
  target_lattice_lcmxo2.c \

HDRS = \
  dap.h \
  dbg.h \
  edbg.h \
  target.h

ifeq ($(UNAME), Linux)
  BIN = edbg
  SRCS += dbg_lin.c
  LIBS += -ludev
else
  ifeq ($(UNAME), Darwin)
    BIN = edbg
    SRCS += dbg_mac.c
    LIBS += -framework IOKit
    LIBS += -framework Foundation
    LIBS += -framework CoreFoundation
    LIBS += -framework Cocoa
    LIBS += /usr/local/lib/libhidapi.a
    CFLAGS += -I/usr/local/include/hidapi
  else
    BIN = edbg.exe
    SRCS += dbg_win.c
    LIBS += -lhid -lsetupapi
  endif
endif

CFLAGS += -W -Wall -Wextra -O3 -std=gnu11
#CFLAGS += -fno-diagnostics-show-caret
CFLAGS += -D_GNU_SOURCE

all: $(BIN)

$(BIN): $(SRCS) $(HDRS)
	$(COMPILER) $(CFLAGS) $(SRCS) $(LIBS) -o $(BIN)

clean:
	rm -rvf $(BIN)

