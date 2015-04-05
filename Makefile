-include config.mk

BIN ?= pngquant
BINPREFIX = $(DESTDIR)$(PREFIX)/bin

OBJS = pngquant.o rwpng.o
COCOA_OBJS = rwpng_cocoa.o

ifeq (1, $(COCOA_READER))
OBJS += $(COCOA_OBJS)
endif

STATICLIB = lib/libimagequant.a

DISTFILES = *.[chm] pngquant.1 Makefile configure README.md INSTALL CHANGELOG COPYRIGHT
TARNAME = pngquant-$(VERSION)
TARFILE = $(TARNAME)-src.tar.bz2

LIBDISTFILES = lib/*.[ch] lib/COPYRIGHT lib/MANUAL.md lib/configure lib/Makefile

DLL=libimagequant.dll
DLLIMP=libimagequant_dll.a
DLLDEF=libimagequant_dll.def

all: $(BIN)

staticlib:
	$(MAKE) -C lib static

$(STATICLIB): config.mk staticlib

$(OBJS): $(wildcard *.h) config.mk

rwpng_cocoa.o: rwpng_cocoa.m
	$(CC) -Wno-enum-conversion -c $(CFLAGS) -o $@ $< || clang -Wno-enum-conversion -c -O3 $(CFLAGS) -o $@ $<

$(BIN): $(OBJS) $(STATICLIB)
	$(CC) $^ $(LDFLAGS) -o $@

test: $(BIN)
	./test/test.sh ./test $(BIN)

dist: $(TARFILE)

$(TARFILE): $(DISTFILES)
	rm -rf $(TARFILE) $(TARNAME)
	mkdir -p $(TARNAME)/lib
	cp $(DISTFILES) $(TARNAME)
	cp $(LIBDISTFILES) $(TARNAME)/lib
	tar -cjf $(TARFILE) --numeric-owner --exclude='._*' $(TARNAME)
	rm -rf $(TARNAME)
	-shasum $(TARFILE)

install: $(BIN)
	-mkdir -p '$(BINPREFIX)'
	install -m 0755 -p '$(BIN)' '$(BINPREFIX)/$(BIN)'

uninstall:
	rm -f '$(BINPREFIX)/$(BIN)'

clean:
	$(MAKE) -C lib clean
	rm -f '$(BIN)' $(OBJS) $(COCOA_OBJS) $(STATICLIB) $(TARFILE)

distclean: clean
	$(MAKE) -C lib distclean
	rm -f config.mk pngquant-*-src.tar.bz2

config.mk:
ifeq ($(filter %clean %distclean, $(MAKECMDGOALS)), )
	./configure
endif

.PHONY: all clean dist distclean dll install uninstall test staticlib
.DELETE_ON_ERROR:
