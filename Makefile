# Makefile for pngquant

# Run `USE_SSE=0 make` to compile for all CPUs

CC=gcc

ifdef USE_SSE
SSEFLAG=-DUSE_SSE=$(USE_SSE)
endif

BIN = pngquant
BINDIR ?= /usr/bin
MANDIR ?= /usr/share/man/man1

# Alternatively, build libpng and zlib in these directories:
CUSTOMLIBPNG ?= ../libpng
CUSTOMZLIB ?= ../zlib

CFLAGSOPT ?= -O3 -fearly-inlining -fstrict-aliasing -ffast-math -funroll-loops -fomit-frame-pointer -fexpensive-optimizations -ffinite-math-only -funsafe-loop-optimizations -ftree-vectorize

CFLAGS ?= -DNDEBUG -Wall -I. -I$(CUSTOMLIBPNG) -I$(CUSTOMZLIB) -I/usr/local/include/ -I/usr/include/ $(CFLAGSOPT) $(SSEFLAG)
CFLAGS += -std=c99

LDFLAGS ?= -L$(CUSTOMLIBPNG) -L$(CUSTOMZLIB) -L/usr/local/lib/ -L/usr/lib/
LDFLAGS += -lz -lpng -lm

OBJS = pngquant.o rwpng.o pam.o mediancut.o blur.o mempool.o viter.o nearest.o

all: $(BIN) pngquant.1.gz

$(BIN): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

pngquant.1.gz: pngquant.1
	gzip -9 <$< >$@

install: all
	install -m 0755 -p -D $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -m 0644 -p -D pngquant.1.gz $(DESTDIR)$(MANDIR)/pngquant.1.gz

uninstall:
	rm -f $(BINDIR)/$(BIN)

clean:
	rm -f pngquant pngquant.1.gz $(OBJS)

.PHONY: all install uninstall clean

