-include config.mk

STATICLIB=libimagequant.a

DLL=libimagequant.dll
DLLIMP=libimagequant_dll.a
DLLDEF=libimagequant_dll.def

OBJS = pam.o mediancut.o blur.o mempool.o viter.o nearest.o libimagequant.o

BUILD_CONFIGURATION="$(CC) $(CFLAGS) $(LDFLAGS)"

DISTFILES = $(OBJS:.o=.c) *.h MANUAL.md COPYRIGHT Makefile configure
TARNAME = libimagequant-$(VERSION)
TARFILE = $(TARNAME)-src.tar.bz2

all: static

static: $(STATICLIB)

dll:
	$(MAKE) CFLAGSADD="-DLIQ_EXPORT='__declspec(dllexport)'" $(DLL)


$(DLL) $(DLLIMP): $(OBJS)
	$(CC) -fPIC -shared -o $(DLL) $(OBJS) $(LDFLAGS) -Wl,--out-implib,$(DLLIMP),--output-def,$(DLLDEF)

$(STATICLIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(OBJS): $(wildcard *.h) config.mk

dist: $(TARFILE)

$(TARFILE): $(DISTFILES)
	rm -rf $(TARFILE) $(TARNAME)
	mkdir $(TARNAME)
	cp $(DISTFILES) $(TARNAME)
	tar -cjf $(TARFILE) --numeric-owner --exclude='._*' $(TARNAME)
	rm -rf $(TARNAME)
	-shasum $(TARFILE)

clean:
	rm -f $(OBJS) $(STATICLIB) $(TARFILE) $(DLL) $(DLLIMP) $(DLLDEF)

distclean: clean
	rm -f config.mk

config.mk:
ifeq ($(filter %clean %distclean, $(MAKECMDGOALS)), )
	./configure
endif

.PHONY: all static clean dist distclean dll
.DELETE_ON_ERROR:
