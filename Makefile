PREFIX ?= /usr/local
DEBUG ?= 0
ICEPROG ?= 1
PROGRAM_PREFIX ?=

CXX ?= clang++
CC ?= clang
PKG_CONFIG ?= pkg-config

C_STD ?= c99
CXX_STD ?= c++11
ifeq ($(DEBUG),1)
OPT_LEVEL ?= 0
DBG_LEVEL ?= -ggdb
else
OPT_LEVEL ?= 2
DBG_LEVEL ?=
endif
WARN_LEVEL ?= all

LDLIBS = -lm -lstdc++
CFLAGS += -MD -O$(OPT_LEVEL) $(DBG_LEVEL) -W$(WARN_LEVEL) -std=$(C_STD) -I$(PREFIX)/include
CXXFLAGS += -MD -O$(OPT_LEVEL) $(DBG_LEVEL) -W$(WARN_LEVEL) -std=$(CXX_STD) -I$(PREFIX)/include

DESTDIR ?=
CHIPDB_SUBDIR ?= $(PROGRAM_PREFIX)icebox

ifeq ($(MXE),1)
EXE = .exe
CXX = /usr/local/src/mxe/usr/bin/i686-w64-mingw32.static-gcc
CC = $(CXX)
PKG_CONFIG = /usr/local/src/mxe/usr/bin/i686-w64-mingw32.static-pkg-config
endif

ifneq ($(shell uname -s),Darwin)
  LDLIBS = -L/usr/local/lib -lm
else
  LIBFTDI_NAME = $(shell $(PKG_CONFIG) --exists libftdi1 && echo ftdi1 || echo ftdi)
  LDLIBS = -L/usr/local/lib -l$(LIBFTDI_NAME) -lm
endif

ifeq ($(STATIC),1)
LDFLAGS += -static
LDLIBS += $(shell for pkg in libftdi1 libftdi; do $(PKG_CONFIG) --silence-errors --static --libs $$pkg && exit; done; echo -lftdi; )
CFLAGS += $(shell for pkg in libftdi1 libftdi; do $(PKG_CONFIG) --silence-errors --static --cflags $$pkg && exit; done; )
else
LDLIBS += $(shell for pkg in libftdi1 libftdi; do $(PKG_CONFIG) --silence-errors --libs $$pkg && exit; done; echo -lftdi; )
CFLAGS += $(shell for pkg in libftdi1 libftdi; do $(PKG_CONFIG) --silence-errors --cflags $$pkg && exit; done; )
endif

all: $(PROGRAM_PREFIX)iceprog$(EXE)

$(PROGRAM_PREFIX)iceprog$(EXE): iceprog.o mpsse.o jtag_tap.o
	$(CC) -o $@ $(LDFLAGS) $^ $(LDLIBS)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(PROGRAM_PREFIX)iceprog$(EXE) $(DESTDIR)$(PREFIX)/bin/$(PROGRAM_PREFIX)iceprog$(EXE)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROGRAM_PREFIX)iceprog$(EXE)

clean:
	rm -f $(PROGRAM_PREFIX)iceprog
	rm -f $(PROGRAM_PREFIX)iceprog.exe
	rm -f *.o *.d

-include *.d

.PHONY: all install uninstall clean

