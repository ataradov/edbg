UNAME := $(shell uname)

SRCS = \
  dap.c \
  edbg.c \
  target.c \
  target_atmel_cm0p.c \
  target_atmel_cm3.c \
  target_atmel_cm4.c \
  target_atmel_cm7.c

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
    LIBS += hidapi/mac/.libs/libhidapi.a
    LIBS += -framework IOKit
    LIBS += -framework CoreFoundation
    HIDAPI = hidapi/mac/.libs/libhidapi.a
    CFLAGS += -Ihidapi/hidapi
  else
    BIN = edbg.exe
    SRCS += dbg_win.c
    LIBS += -lhid -lsetupapi
  endif
endif

CFLAGS += -W -Wall -O2 -std=gnu99

all: $(BIN)

$(BIN): $(SRCS) $(HDRS) $(HIDAPI)
	gcc $(CFLAGS) $(SRCS) $(LIBS) -o $(BIN)

hidapi/mac/.libs/libhidapi.a:
	git clone git://github.com/signal11/hidapi.git
	cd hidapi && ./bootstrap
	cd hidapi && ./configure
	make -Chidapi

clean:
	rm -rvf $(BIN) hidapi

