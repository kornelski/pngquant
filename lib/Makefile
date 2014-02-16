VERSION=2.0.0

# This changes default "cc" to "gcc", but still allows customization of the CC variable
# if this line causes problems with non-GNU make, just remove it:
CC := $(patsubst cc,gcc,$(CC))

STATICLIB=libimagequant.a
DLL=libimagequant.dll
DLLIMP=libimagequant_dll.a
DLLDEF=libimagequant_dll.def

CFLAGSOPT ?= -DNDEBUG -O3 -ffast-math -funroll-loops -fomit-frame-pointer

CFLAGS ?= -Wall -Wno-unknown-pragmas -I. $(CFLAGSOPT)
CFLAGS += -std=c99 $(CFLAGSADD)

ifdef USE_SSE
CFLAGS += -msse2 -DUSE_SSE=$(USE_SSE)
endif

OBJS = pam.o mediancut.o blur.o mempool.o viter.o nearest.o libimagequant.o

BUILD_CONFIGURATION="$(CC) $(CFLAGS) $(LDFLAGS)"

DISTFILES = $(OBJS:.o=.c) *.h MANUAL.md COPYRIGHT Makefile
TARNAME = libimagequant-$(VERSION)
TARFILE = $(TARNAME)-src.tar.bz2

all: static

static: $(STATICLIB)

dll:
	$(MAKE) CFLAGSADD="-DLIQ_EXPORT='__declspec(dllexport)'" $(DLL)

openmp::
	$(MAKE) CFLAGSADD=-fopenmp OPENMPFLAGS="-Bstatic -lgomp" -j8

$(DLL) $(DLLIMP): $(OBJS)
	$(CC) -fPIC -shared -o $(DLL) $(OBJS) $(LDFLAGS) -Wl,--out-implib,$(DLLIMP),--output-def,$(DLLDEF)

$(STATICLIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(OBJS): pam.h build_configuration

dist: $(TARFILE)

$(TARFILE): $(DISTFILES)
	rm -rf $(TARFILE) $(TARNAME)
	mkdir $(TARNAME)
	cp $(DISTFILES) $(TARNAME)
	tar -cjf $(TARFILE) --numeric-owner --exclude='._*' $(TARNAME)
	rm -rf $(TARNAME)
	-shasum $(TARFILE)

clean:
	rm -f $(OBJS) $(STATICLIB) $(TARFILE) build_configuration

build_configuration::
	@test -f build_configuration && test $(BUILD_CONFIGURATION) = "`cat build_configuration`" || echo > build_configuration $(BUILD_CONFIGURATION)

.PHONY: all openmp static clean dist dll
.DELETE_ON_ERROR:
