# This changes default "cc" to "gcc", but still allows customization of the CC variable
# if this line causes problems with non-GNU make, just remove it:
CC := $(patsubst cc,gcc,$(CC))

STATICLIB=libimagequant.a

CFLAGSOPT ?= -DNDEBUG -O3 -fstrict-aliasing -ffast-math -funroll-loops -fomit-frame-pointer -ffinite-math-only

CFLAGS ?= -Wall -Wno-unknown-pragmas -I. $(CFLAGSOPT)
CFLAGS += -std=c99 $(CFLAGSADD)

OBJS = pam.o mediancut.o blur.o mempool.o viter.o nearest.o libimagequant.o

BUILD_CONFIGURATION="$(CC) $(CFLAGS) $(LDFLAGS)"

all: static

static: $(STATICLIB)

openmp::
	$(MAKE) CFLAGSADD=-fopenmp OPENMPFLAGS="-Bstatic -lgomp" -j8 $(MAKEFLAGS)

$(STATICLIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(OBJS): pam.h build_configuration

clean:
	rm -f $(OBJS) $(STATICLIB) build_configuration

build_configuration::
	@test -f build_configuration && test $(BUILD_CONFIGURATION) = "`cat build_configuration`" || echo > build_configuration $(BUILD_CONFIGURATION)

.PHONY: all openmp static clean
