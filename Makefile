-include config.mk

LIQSRCDIR ?= lib
BIN ?= pngquant
BINPREFIX ?= $(DESTDIR)$(PREFIX)/bin
MANPREFIX ?= $(DESTDIR)$(PREFIX)/share/man

OBJS = pngquant.o pngquant_opts.o rwpng.o

STATICLIB = $(LIQSRCDIR)/libimagequant.a
DISTFILES = *.[chm] pngquant.1 Makefile configure README.md INSTALL CHANGELOG COPYRIGHT Cargo.toml
TARNAME = pngquant-$(VERSION)
TARFILE = $(TARNAME)-src.tar.gz

LIBDISTFILES = $(LIQSRCDIR)/*.[ch] $(LIQSRCDIR)/COPYRIGHT $(LIQSRCDIR)/README.md $(LIQSRCDIR)/configure $(LIQSRCDIR)/Makefile $(LIQSRCDIR)/Cargo.toml

TESTBIN = test/test

all: $(BIN)

$(LIQSRCDIR)/config.mk: config.mk
	( cd '$(LIQSRCDIR)'; ./configure $(LIQCONFIGUREFLAGS) )

$(STATICLIB): $(LIQSRCDIR)/config.mk $(LIBDISTFILES)
	$(MAKE) -C '$(LIQSRCDIR)' static

$(OBJS): $(wildcard *.h) config.mk

$(BIN): $(OBJS) $(STATICLIBDEPS)
	$(CC) $(OBJS) $(CFLAGS) $(LDFLAGS) -o $@

$(TESTBIN): test/test.o $(STATICLIBDEPS)
	$(CC) test/test.o $(CFLAGS) $(LDFLAGS) -o $@

test: $(BIN) $(TESTBIN)
	LD_LIBRARY_PATH='$(LIQSRCDIR)' ./test/test.sh ./test $(BIN) $(TESTBIN)

dist: $(TARFILE)

$(TARFILE): $(DISTFILES)
	test -n "$(VERSION)"
	make -C $(LIQSRCDIR) cargo
	rm -rf $(TARFILE) $(TARNAME)
	mkdir -p $(TARNAME)/lib/rust $(TARNAME)/lib/msvc-dist $(TARNAME)/rust
	cp $(DISTFILES) $(TARNAME)
	cp rust/*.rs $(TARNAME)/rust/
	cp $(LIBDISTFILES) $(TARNAME)/lib
	cp $(LIQSRCDIR)/rust/*.rs $(TARNAME)/lib/rust/
	cp $(LIQSRCDIR)/msvc-dist/*.[ch] $(TARNAME)/lib/msvc-dist/
	tar -czf $(TARFILE) --numeric-owner --exclude='._*' $(TARNAME)
	rm -rf $(TARNAME)
	-shasum $(TARFILE)

install: $(BIN) $(BIN).1
	-mkdir -p '$(BINPREFIX)'
	-mkdir -p '$(MANPREFIX)/man1'
	install -m 0755 -p '$(BIN)' '$(BINPREFIX)/$(BIN)'
	install -m 0644 -p '$(BIN).1' '$(MANPREFIX)/man1/'

uninstall:
	rm -f '$(BINPREFIX)/$(BIN)'
	rm -f '$(MANPREFIX)/man1/$(BIN).1'

clean:
	-test -n '$(LIQSRCDIR)' && $(MAKE) -C '$(LIQSRCDIR)' clean
	rm -f '$(BIN)' $(OBJS) $(TARFILE)

distclean: clean
	-test -n '$(LIQSRCDIR)' && $(MAKE) -C '$(LIQSRCDIR)' distclean
	rm -f config.mk pngquant-*-src.tar.gz

config.mk: Makefile
ifeq ($(filter %clean %distclean, $(MAKECMDGOALS)), )
	./configure
endif

.PHONY: all clean dist distclean dll install uninstall test
.DELETE_ON_ERROR:
