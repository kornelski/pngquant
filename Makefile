# Makefile for pngquant

# This changes default "cc" to "gcc", but still allows customization of the CC variable
# if this line causes problems with non-GNU make, just remove it:
CC := $(patsubst cc,gcc,$(CC))

BIN = pngquant
PREFIX ?= /usr/local
BINPREFIX = $(PREFIX)/bin

# Alternatively, build libpng and zlib in these directories:
CUSTOMLIBPNG ?= ../libpng
CUSTOMZLIB ?= ../zlib

CFLAGSOPT ?= -O3 -fearly-inlining -fstrict-aliasing -ffast-math -funroll-loops -fomit-frame-pointer -momit-leaf-frame-pointer -ffinite-math-only -fno-trapping-math -funsafe-loop-optimizations

CFLAGS ?= -DNDEBUG -Wall -Wno-unknown-pragmas -I. -I$(CUSTOMLIBPNG) -I$(CUSTOMZLIB) -I/usr/local/include/ -I/usr/include/ -I/usr/X11/include/ $(CFLAGSOPT)
CFLAGS += -std=c99 $(CFLAGSADD)

LDFLAGS ?= -L$(CUSTOMLIBPNG) -L$(CUSTOMZLIB) -L/usr/local/lib/ -L/usr/lib/ -L/usr/X11/lib/
LDFLAGS += -lz -lpng -lm $(LDFLAGSADD)

OBJS = pngquant.o rwpng.o pam.o mediancut.o blur.o mempool.o viter.o nearest.o

all: $(BIN)

openmp::
	CFLAGSADD=-fopenmp LDFLAGSADD=-lgomp $(MAKE) -j 8 $(MAKEFLAGS)

$(BIN): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(OBJS): pam.h rwpng.h

install: $(BIN)
	install -m 0755 -p -D $(BIN) $(DESTDIR)$(BINPREFIX)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINPREFIX)/$(BIN)

clean:
	rm -f $(BIN) $(OBJS)

.PHONY: all install uninstall clean openmp

