# Makefile for pngquant

CC=gcc

# Change this to point to directory where include/png.h can be found:
SYSTEMLIBPNG=/usr/X11

# Alternatively, build libpng and zlib in these directories:
CUSTOMLIBPNG = ../libpng
CUSTOMZLIB = ../zlib

CFLAGS = -std=c99 -O3 -Wall -I. -I$(CUSTOMLIBPNG) -I$(CUSTOMZLIB) -I$(SYSTEMLIBPNG)/include/ -funroll-loops -fomit-frame-pointer

LDFLAGS = -L$(CUSTOMLIBPNG) -L$(CUSTOMZLIB) -L$(SYSTEMLIBPNG)/lib/ -L/usr/lib/ -lz -lpng -lm

OBJS = pngquant.o rwpng.o pam.o

all: pngquant

pngquant: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

clean:
	rm -f pngquant $(OBJS)

