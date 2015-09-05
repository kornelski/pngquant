-include config.mk

STATICLIB=libimagequant.a
SHAREDLIB=libimagequant.so.0

DLL=libimagequant.dll
DLLIMP=libimagequant_dll.a
DLLDEF=libimagequant_dll.def

OBJS = pam.o mediancut.o blur.o mempool.o viter.o nearest.o libimagequant.o
SHAREDOBJS = $(subst .o,.lo,$(OBJS))

DISTFILES = $(OBJS:.o=.c) *.h MANUAL.md COPYRIGHT Makefile configure
TARNAME = libimagequant-$(VERSION)
TARFILE = $(TARNAME)-src.tar.bz2

all: static shared

static: $(STATICLIB)

shared: $(SHAREDLIB)

dll:
	$(MAKE) CFLAGSADD="-DLIQ_EXPORT='__declspec(dllexport)'" $(DLL)


$(DLL) $(DLLIMP): $(OBJS)
	$(CC) -fPIC -shared -o $(DLL) $^ $(LDFLAGS) -Wl,--out-implib,$(DLLIMP),--output-def,$(DLLDEF)

$(STATICLIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(SHAREDOBJS):
	$(CC) -fPIC $(CFLAGS) -c $(@:.lo=.c) -o $@

$(SHAREDLIB): $(SHAREDOBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

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
	rm -f $(OBJS) $(SHAREDOBJS) $(SHAREDLIB) $(STATICLIB) $(TARFILE) $(DLL) $(DLLIMP) $(DLLDEF)

distclean: clean
	rm -f config.mk

config.mk:
ifeq ($(filter %clean %distclean, $(MAKECMDGOALS)), )
	./configure
endif

.PHONY: all static shared clean dist distclean dll
.DELETE_ON_ERROR:
