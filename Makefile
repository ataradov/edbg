UNAME := $(shell uname)

SRCS = \
  dap.c \
  edbg.c \
  target.c \
  target_cm0p.c \
  target_cm4.c \
  target_cm7.c

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
  BIN = edbg.exe
  SRCS += dbg_win.c
  LIBS += -lhid -lsetupapi
endif

CFLAGS += -W -Wall -O2 -std=gnu99

all: $(SRCS) $(HDRS)
	gcc $(CFLAGS) $(SRCS) $(LIBS) -o $(BIN)

clean:
	-rm $(BIN)

