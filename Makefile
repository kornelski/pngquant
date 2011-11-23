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

CFLAGS = -std=gnu99 -O3 -Wall -I. -I$(CUSTOMLIBPNG) -I$(CUSTOMZLIB) -I$(SYSTEMLIBPNG)/include/ -funroll-loops -fomit-frame-pointer

LDFLAGS = -L$(CUSTOMLIBPNG) -L$(CUSTOMZLIB) -L$(SYSTEMLIBPNG)/lib/ -L/usr/lib/ -lz -lpng -lm

OBJS = pngquant.o rwpng.o pam.o mediancut.o blur.o mempool.o viter.o nearest.o

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

install: $(BIN)
	cp $(BIN) $(BINPREFIX)/$(BIN)

uninstall:
	rm -f $(BINPREFIX)/$(BIN)

clean:
	rm -f pngquant $(OBJS)

.PHONY: all install uninstall clean

