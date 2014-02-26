!if "$(PLATFORM)" == "X64"
MACHINE=X64
!else
MACHINE=X86
!endif

!ifndef USE_SSE
USE_SSE=1
!endif

CFLAGS = /Ox /GL /MD /nologo /DNDEBUG /DUSE_SSE=$(USE_SSE)
LINKFLAGS = /LTCG /nologo
OBJECTS = blur.obj libimagequant.obj mediancut.obj mempool.obj nearest.obj pam.obj viter.obj

.c.obj:
	$(CC) $(CFLAGS) /c $<

all: libimagequant.lib

libimagequant.lib : $(OBJECTS)
	lib /machine:$(MACHINE) /out:$@ $(LINKFLAGS) $**

clean:
	del *.obj
	del *.lib