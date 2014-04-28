-include config.mk

BIN ?= pngquant
BINPREFIX = $(PREFIX)/bin

OBJS = pngquant.o rwpng.o
COCOA_OBJS = rwpng_cocoa.o

ifdef COCOA_READER
OBJS += $(COCOA_OBJS)
endif

STATICLIB = lib/libimagequant.a
LIBOBJS = $(patsubst %.c, %.o, $(wildcard lib/*.c))

DISTFILES = *.[chm] pngquant.1 Makefile configure README.md INSTALL CHANGELOG COPYRIGHT
TARNAME = pngquant-$(VERSION)
TARFILE = $(TARNAME)-src.tar.bz2

LIBDISTFILES = lib/*.[ch] lib/COPYRIGHT lib/MANUAL.md
LIBTARNAME = libimagequant-$(VERSION)
LIBTARFILE = $(LIBTARNAME)-src.tar.bz2

DLL=libimagequant.dll
DLLIMP=libimagequant_dll.a
DLLDEF=libimagequant_dll.def

ifndef LIBQ_ONLY
all: $(BIN)
endif

lib: $(STATICLIB)

$(LIBOBJS): $(wildcard lib/*.h) config.mk

$(STATICLIB): $(LIBOBJS)
	$(AR) $(ARFLAGS) $@ $^

$(OBJS): $(wildcard *.h) config.mk

rwpng_cocoa.o: rwpng_cocoa.m
	$(CC) -c $(CFLAGS) -o $@ $<

$(BIN): $(STATICLIB) $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

dist: $(TARFILE)

$(TARFILE): $(DISTFILES)
	rm -rf $(TARFILE) $(TARNAME)
	mkdir -p $(TARNAME)/lib
	cp $(DISTFILES) $(TARNAME)
	cp $(LIBDISTFILES) $(TARNAME)/lib
	tar -cjf $(TARFILE) --numeric-owner --exclude='._*' $(TARNAME)
	rm -rf $(TARNAME)
	-shasum $(TARFILE)

libdist: $(LIBTARFILE)

$(LIBTARFILE):
	rm -rf $(LIBTARFILE) $(LIBTARNAME)
	mkdir $(LIBTARNAME)
	cp $(LIBDISTFILES) $(LIBTARNAME)
	tar -cjf $(LIBTARFILE) --numeric-owner --exclude='._*' $(LIBTARNAME)
	rm -rf $(LIBTARNAME)
	-shasum $(LIBTARFILE)

dll: $(DLL) $(DLLIMP)

$(DLL) $(DLLIMP): $(STATICLIB) $(LIBOBJS)
	$(CC) -fPIC -shared -DLIQ_EXPORT='__declspec(dllexport)' -o $(DLL) $(LIBOBJS) -Wl,--out-implib,$(DLLIMP),--output-def,$(DLLDEF)

install: $(BIN)
	install -m 0755 -p $(BIN) $(BINPREFIX)/$(BIN)

uninstall:
	rm -f $(BINPREFIX)/$(BIN)

clean:
	rm -f $(BIN) $(OBJS) $(COCOA_OBJS) $(STATICLIB) $(LIBOBJS) $(TARFILE) $(LIBTARFILE) $(DLL) $(DLLIMP) $(DLLDEF)

distclean: clean
	rm -f config.mk

ifeq ($(filter %clean %dist, $(MAKECMDGOALS)), )
config.mk:
	./configure
endif

.PHONY: all clean dist distclean libdist dll lib install uninstall
.DELETE_ON_ERROR:
