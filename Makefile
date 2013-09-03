# Makefile for pngquant
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

ifeq ($(uname_S),SunOS)
	VERSION = $(shell grep 'define PNGQUANT_VERSION' pngquant.c | grep -Eo '2\.[0-9.]*')
else
	VERSION = $(shell grep 'define PNGQUANT_VERSION' pngquant.c | egrep -Eo '2\.[0-9.]*')
endif

# This changes default "cc" to "gcc", but still allows customization of the CC variable
# if this line causes problems with non-GNU make, just remove it:
CC := $(patsubst cc,gcc,$(CC))

BIN ?= pngquant
PREFIX ?= /usr/local
BINPREFIX = $(PREFIX)/bin

# Alternatively, build libpng and zlib in these directories:
CUSTOMLIBPNG ?= ../libpng
CUSTOMZLIB ?= ../zlib

CFLAGSOPT ?= -DNDEBUG -O3 -fstrict-aliasing -ffast-math -funroll-loops -fomit-frame-pointer -ffinite-math-only

CFLAGS ?= -Wall -Wno-unknown-pragmas -I. -I$(CUSTOMLIBPNG) -I$(CUSTOMZLIB) -I/usr/local/include/ -I/usr/include/ -I/usr/X11/include/ -I/opt/local/include/libpng15 $(CFLAGSOPT)
CFLAGS += -std=c99 $(CFLAGSADD)

LDFLAGS ?= -L$(CUSTOMLIBPNG) -L$(CUSTOMZLIB) -L/usr/local/lib/ -L/usr/lib/ -L/usr/X11/lib/ -L/opt/local/lib
ifeq ($(uname_S),SunOS)
	LDFLAGS += /opt/local/lib/libpng15.a -lz -lm lib/libimagequant.a -lm $(LDFLAGSADD)
else
	LDFLAGS += -lpng -lz -lm lib/libimagequant.a -lm $(LDFLAGSADD)
endif


OBJS = pngquant.o rwpng.o
COCOA_OBJS = rwpng_cocoa.o

DISTLIBFILES = lib/*.[ch] lib/Makefile lib/COPYRIGHT lib/MANUAL.md
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

lib/libimagequant.a::
	$(MAKE) -C lib -$(MAKEFLAGS) static

openmp::
	$(MAKE) CFLAGSADD=-fopenmp OPENMPFLAGS="-Bstatic -lgomp" -j8 -$(MAKEFLAGS)

$(BIN): $(OBJS) lib/libimagequant.a
	$(CC) $(OBJS) $(LDFLAGS) $(OPENMPFLAGS) $(FRAMEWORKS) -o $@

rwpng_cocoa.o: rwpng_cocoa.m
	clang -c $(CFLAGS) -o $@ $<

$(OBJS): rwpng.h build_configuration

install: $(BIN)
	install -m 0755 -p -D $(BIN) $(DESTDIR)$(BINPREFIX)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINPREFIX)/$(BIN)

dist: $(TARFILE)

$(TARFILE): $(DISTFILES)
	rm -rf $(TARFILE) $(TARNAME)
	mkdir -p $(TARNAME)/lib
	cp $(DISTFILES) $(TARNAME)
	cp $(DISTLIBFILES) $(TARNAME)/lib
	tar -cjf $(TARFILE) --numeric-owner --exclude='._*' $(TARNAME)
	rm -rf $(TARNAME)
	shasum $(TARFILE)

clean:
	rm -f $(BIN) $(OBJS) $(COCOA_OBJS) $(TARFILE) build_configuration
	$(MAKE) -C lib -$(MAKEFLAGS) clean

build_configuration::
	@test -f build_configuration && test $(BUILD_CONFIGURATION) = "`cat build_configuration`" || echo > build_configuration $(BUILD_CONFIGURATION)

.PHONY: all openmp install uninstall dist clean
.DELETE_ON_ERROR:
