# Makefile for pngquant
VERSION = $(shell grep 'define PNGQUANT_VERSION' pngquant.c | grep -Eo '[12]\.[0-9.]*')

# This changes default "cc" to "gcc", but still allows customization of the CC variable
# if this line causes problems with non-GNU make, just remove it:
CC := $(patsubst cc,gcc,$(CC))

BIN ?= pngquant
PREFIX ?= /usr/local
BINPREFIX = $(PREFIX)/bin

# Alternatively, build libpng and zlib in these directories:
CUSTOMLIBPNG ?= ../libpng
CUSTOMZLIB ?= ../zlib

CFLAGSOPT ?= -DNDEBUG -O3 -ffast-math -funroll-loops -fomit-frame-pointer

CFLAGS ?= -Wall -Wno-unknown-pragmas -I. -I/usr/local/include/ -I/usr/include/ $(CFLAGSOPT)
CFLAGS += $(CFLAGSADD)

ifeq ($(CC), icc)
# disable omp pragmas warning when -openmp is not set
CFLAGS += -fast -wd3180
endif

LDFLAGS ?= -L/usr/local/lib/ -L/usr/lib/

ifneq "$(wildcard $(CUSTOMZLIB))" ""
LDFLAGS += -L$(CUSTOMZLIB) -L$(CUSTOMZLIB)/lib
CFLAGS += -I$(CUSTOMZLIB) -I$(CUSTOMZLIB)/include
endif

ifneq "$(wildcard $(CUSTOMLIBPNG))" ""
LDFLAGS += -L$(CUSTOMLIBPNG) -L$(CUSTOMLIBPNG)/lib
CFLAGS += -I$(CUSTOMLIBPNG) -I$(CUSTOMLIBPNG)/include

else ifneq "$(wildcard /usr/local/include/png.h)" ""

else ifneq "$(wildcard /opt/local/include/libpng15)" ""
LDFLAGS += -L/opt/local/lib
CFLAGS += -I/opt/local/include/libpng15

else ifneq "$(wildcard /opt/X11/include/libpng15/)" ""
LDFLAGS += -L/opt/X11/lib
CFLAGS += -I/opt/X11/include/libpng15/

else ifneq "$(wildcard /usr/X11/lib/)" ""
LDFLAGS += -L/usr/X11/lib/
CFLAGS += -I/usr/X11/include/
endif

LDFLAGS += -lpng -lz -lm lib/libimagequant.a -lm $(LDFLAGSADD)

OBJS = pngquant.o rwpng.o
COCOA_OBJS = rwpng_cocoa.o

DISTLIBFILES = lib/*.[ch] lib/Makefile lib/COPYRIGHT lib/MANUAL.md
DISTFILES = $(OBJS:.o=.c) *.[hm] pngquant.1 Makefile README.md INSTALL CHANGELOG COPYRIGHT
TARNAME = pngquant-$(VERSION)
TARFILE = $(TARNAME)-src.tar.bz2

ifdef USE_COCOA
CFLAGS += -mmacosx-version-min=10.6 -DUSE_COCOA=1
LDLAGS += -mmacosx-version-min=10.6
OBJS += $(COCOA_OBJS)
FRAMEWORKS += -framework Cocoa
endif

ifdef USE_LCMS
CFLAGS += $(shell pkg-config --cflags lcms2) -DUSE_LCMS=1
LDFLAGS += $(shell pkg-config --libs lcms2)
endif

BUILD_CONFIGURATION="$(CC) $(CFLAGS) $(LDFLAGS)"

all: $(BIN)

lib/libimagequant.a::
	$(MAKE) -C lib static

openmp::
ifeq ($(CC), icc)
	$(MAKE) CFLAGSADD="$(CFLAGSADD) -openmp" OPENMPFLAGS=-openmp -j8 $(MKFLAGS)
else
	$(MAKE) CFLAGSADD="$(CFLAGSADD) -fopenmp" OPENMPFLAGS="-Bstatic -lgomp" -j8 $(MKFLAGS)
endif


$(BIN): $(OBJS) lib/libimagequant.a
	$(CC) $(OBJS) $(LDFLAGS) $(OPENMPFLAGS) $(FRAMEWORKS) -o $@

rwpng_cocoa.o: rwpng_cocoa.m
	clang -c $(CFLAGS) -o $@ $<

$(OBJS): rwpng.h build_configuration

install: $(BIN)
	install -m 0755 -p $(BIN) $(DESTDIR)$(BINPREFIX)/$(BIN)

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
	$(MAKE) -C lib clean

build_configuration::
	@test -f build_configuration && test $(BUILD_CONFIGURATION) = "`cat build_configuration`" || echo > build_configuration $(BUILD_CONFIGURATION)

.PHONY: all openmp install uninstall dist clean
.DELETE_ON_ERROR:
