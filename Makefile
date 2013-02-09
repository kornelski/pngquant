# Makefile for pngquant
VERSION = $(shell grep 'define PNGQUANT_VERSION' pngquant.c | egrep -Eo '1\.[0-9.]*')

# This changes default "cc" to "gcc", but still allows customization of the CC variable
# if this line causes problems with non-GNU make, just remove it:
CC := $(patsubst cc,gcc,$(CC))

BIN ?= pngquant
PREFIX ?= /usr/local
BINPREFIX = $(PREFIX)/bin

# Alternatively, build libpng in this directory:
CUSTOMLIBPNG ?= ../libpng

CFLAGSOPT ?= -DNDEBUG -O3 -fstrict-aliasing -ffast-math -funroll-loops -fomit-frame-pointer -ffinite-math-only

CFLAGS ?= -Wall -Wno-unknown-pragmas -I. -I$(CUSTOMLIBPNG) -I/usr/local/include/ -I/usr/include/ -I/usr/X11/include/ $(CFLAGSOPT)
CFLAGS += -std=c99 $(CFLAGSADD)

LDFLAGS ?= -L$(CUSTOMLIBPNG) -L/usr/local/lib/ -L/usr/lib/ -L/usr/X11/lib/
LDFLAGS += -lpng -lm $(LDFLAGSADD)

LIBOBJS = rwpng.o lib/pam.o lib/mediancut.o lib/blur.o lib/mempool.o lib/viter.o lib/nearest.o lib/libimagequant.o
OBJS = pngquant.o $(LIBOBJS)
COCOA_OBJS = rwpng_cocoa.o

DISTFILES = $(OBJS:.o=.c) *.[hm] pngquant.1 Makefile README.md INSTALL CHANGELOG COPYRIGHT
TARNAME = pngquant-$(VERSION)
TARFILE = $(TARNAME)-src.tar.bz2

ifdef USE_COCOA
CFLAGS += -DUSE_COCOA=1
OBJS += $(COCOA_OBJS)
FRAMEWORKS += -framework Cocoa
endif

BUILD_CONFIGURATION="$(CC) $(CFLAGS) $(LDFLAGS)"

all: $(BIN)

openmp::
	$(MAKE) CFLAGSADD=-fopenmp OPENMPFLAGS="-Bstatic -lgomp" -j8 $(MAKEFLAGS)

$(BIN): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) $(OPENMPFLAGS) $(FRAMEWORKS) -o $@

rwpng_cocoa.o: rwpng_cocoa.m
	clang -c $(CFLAGS) -o $@ $<

$(OBJS): lib/pam.h rwpng.h build_configuration

install: $(BIN)
	install -m 0755 -p -D $(BIN) $(DESTDIR)$(BINPREFIX)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINPREFIX)/$(BIN)

dist: $(TARFILE)

$(TARFILE): $(DISTFILES)
	rm -rf $(TARFILE) $(TARNAME)
	mkdir $(TARNAME)
	cp $(DISTFILES) $(TARNAME)
	tar -cjf $(TARFILE) --numeric-owner --exclude='._*' $(TARNAME)
	rm -rf $(TARNAME)
	shasum $(TARFILE)

clean:
	rm -f $(BIN) $(OBJS) $(COCOA_OBJS) $(TARFILE) build_configuration

build_configuration::
	@test -f build_configuration && test $(BUILD_CONFIGURATION) = "`cat build_configuration`" || echo > build_configuration $(BUILD_CONFIGURATION)

.PHONY: all openmp install uninstall dist clean
.DELETE_ON_ERROR:
