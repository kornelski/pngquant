# Makefile for pngquant
VERSION = 1.7.4
LONGVER = "version $(VERSION) (July 2012)"

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
CFLAGS += -std=c99 -DPNGQUANT_VERSION='$(LONGVER)' $(CFLAGSADD)

LDFLAGS ?= -L$(CUSTOMLIBPNG) -L$(CUSTOMZLIB) -L/usr/local/lib/ -L/usr/lib/ -L/usr/X11/lib/
LDFLAGS += -lz -lpng -lm $(LDFLAGSADD)

OBJS = pngquant.o rwpng.o pam.o mediancut.o blur.o mempool.o viter.o nearest.o

DISTFILES = *.[ch1] Makefile README.md INSTALL CHANGELOG COPYRIGHT
TARNAME = pngquant-$(VERSION)
TARFILE = $(TARNAME).tar.gz

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

dist: $(TARFILE)

$(TARFILE): $(DISTFILES)
	rm -rf $(TARFILE) $(TARNAME)
	mkdir $(TARNAME)
	cp -r $(DISTFILES) $(TARNAME)
	tar -czf $(TARFILE) $(TARNAME)
	rm -rf $(TARNAME)

clean:
	rm -f $(BIN) $(OBJS) $(TARFILE)

.PHONY: all openmp install uninstall dist clean
.DELETE_ON_ERROR:
