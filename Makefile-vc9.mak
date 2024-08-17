CPU=i386
NODEBUG=1
!include <win32.mak>

CXXFLAGS = $(cdebug) $(cflags) $(cvarsdll)
CPPFLAGS = /D_CRT_SECURE_NO_WARNINGS /D__STDC_CONSTANT_MACROS /D__STDC_LIMIT_MACROS /DSHIP /FI"inttypes.h"
LDFLAGS = $(ldebug) $(dlllflags)

all: lgvid.dll

clean:
	-del /q lgvid.dll lgvid.dll.manifest lgvid.exp lgvid.lib lgvid.obj >nul 2>&1

lgvid.obj: lgvid.cpp
	$(cc) $(CXXFLAGS) $(CPPFLAGS) /out:$@ /c $**

lgvid.dll: lgvid.obj
	$(link) $(LDFLAGS) /def:lgvid.def /out:$@ $**

