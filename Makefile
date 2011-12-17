# Makefile for pngquant

CC=gcc

BIN = pngquant
PREFIX ?= /usr
BINPREFIX = $(PREFIX)/bin

# Change this to point to directory where include/png.h can be found:
SYSTEMLIBPNG=/usr/X11

# Alternatively, build libpng and zlib in these directories:
CUSTOMLIBPNG = ../libpng
CUSTOMZLIB = ../zlib

CFLAGSOPT ?= -O3 -fearly-inlining -fstrict-aliasing -ffast-math -funroll-loops -fomit-frame-pointer -fexpensive-optimizations -ffinite-math-only -funsafe-loop-optimizations

CFLAGS ?= -DNDEBUG -g -Wall -I. -I$(CUSTOMLIBPNG) -I$(CUSTOMZLIB) -I$(SYSTEMLIBPNG)/include/ $(CFLAGSOPT)
CFLAGS += -std=c99

LDFLAGS ?= -L$(CUSTOMLIBPNG) -L$(CUSTOMZLIB) -L$(SYSTEMLIBPNG)/lib/ -L/usr/lib/
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

