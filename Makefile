# Makefile for pngquant

# Run `USE_SSE=0 make` to compile for all CPUs

CC=gcc

ifdef USE_SSE
SSEFLAG=-DUSE_SSE=$(USE_SSE)
endif

BIN = pngquant
PREFIX ?= /usr
BINPREFIX = $(PREFIX)/bin

# Alternatively, build libpng and zlib in these directories:
CUSTOMLIBPNG ?= ../libpng
CUSTOMZLIB ?= ../zlib

CFLAGSOPT ?= -O3 -fearly-inlining -fstrict-aliasing -ffast-math -funroll-loops -fomit-frame-pointer -fexpensive-optimizations -ffinite-math-only -funsafe-loop-optimizations -ftree-vectorize

CFLAGS ?= -DNDEBUG -Wall -I. -I$(CUSTOMLIBPNG) -I$(CUSTOMZLIB) -I/usr/local/include/ -I/usr/include/ $(CFLAGSOPT) $(SSEFLAG)
CFLAGS += -std=c99

LDFLAGS ?= -L$(CUSTOMLIBPNG) -L$(CUSTOMZLIB) -L/usr/local/lib/ -L/usr/lib/
LDFLAGS += -lz -lpng -lm

OBJS = pngquant.o rwpng.o pam.o mediancut.o blur.o mempool.o viter.o nearest.o

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

install: $(BIN)
	cp $(BIN) $(BINPREFIX)/$(BIN)

uninstall:
	rm -f $(BINPREFIX)/$(BIN)

clean:
	rm -f pngquant $(OBJS)

.PHONY: all install uninstall clean

