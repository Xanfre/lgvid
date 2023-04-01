TARGET ?=
ifeq ($(TARGET),)
CXX = g++
else
CXX = $(TARGET)-g++
endif

CXXFLAGS_ALL = -O2 $(CXXFLAGS)
CPPFLAGS_ALL = -std=gnu++11 -DFFMPEG_DLL $(CPPFLAGS)
LDOPTS = -Wl,--nxcompat -Wl,--no-seh -Wl,--dynamicbase lgvid.def $(LDFLAGS)

FFMPEG_INC ?=

LIBS ?=

ifneq ($(STATIC),)
	LIBS += -static
endif

all: lgvid.dll

clean:
	rm -f lgvid.o
	rm -f lgvid.dll

lgvid.o: lgvid.cpp
	$(CXX) -I. $(CXXFLAGS_ALL) $(CPPFLAGS_ALL) $(FFMPEG_INC) -c -o $@ $<

lgvid.dll: lgvid.o
	$(CXX) $(CXXFLAGS_ALL) -shared $(LDOPTS) $^ -o $@ $(LIBS)
