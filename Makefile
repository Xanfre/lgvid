.PHONY: all clean

TARGET ?=
ifeq ($(TARGET),)
CXX = g++
else
CXX = $(TARGET)-g++
endif

TARGET_OS ?= $(OS)

CXXFLAGS_ALL = -O2 $(CXXFLAGS)
CPPFLAGS_ALL = -std=gnu++11 -Wall -Wextra -DSHIP $(CPPFLAGS)
LDFLAGS_ALL = $(LDFLAGS)

ifeq ($(TARGET_OS),Windows_NT)
	LDFLAGS_ALL += -Wl,--nxcompat -Wl,--no-seh -Wl,--dynamicbase lgvid.def
ifneq ($(USE_STD_THREAD),)
	CPPFLAGS_ALL += -DUSE_STD_THREAD
endif
else
	CXXFLAGS_ALL += -fPIC
	CPPFLAGS_ALL += -include comcompat.h -DUSE_STD_THREAD
endif
ifneq ($(FFMPEG_ALIGN),)
	CPPFLAGS_ALL += -DFFMPEG_ALIGN=$(FFMPEG_ALIGN)
endif
ifneq ($(FFMPEG_DLL),)
	CPPFLAGS_ALL += -DFFMPEG_DLL
ifneq ($(FFMPEG_COMBINED_DLL),)
	CPPFLAGS_ALL += -DFFMPEG_COMBINED_DLL
endif
endif

ifneq ($(FFMPEG_DLL),)
LIBS =
else
LIBS = -lavformat -lavcodec -lavutil -lswscale -lswresample
endif

ifeq ($(TARGET_OS),Windows_NT)
	SONAME ?= lgvid.dll
else
	SONAME ?= liblgvid.so
	LDFLAGS_ALL += -Wl,-soname,$(SONAME)
endif
ifneq ($(STATIC),)
	LIBS += $(EXTRA_LIBS) -static
endif

all: $(SONAME)

clean:
	rm -f lgvid.o $(SONAME)

lgvid.o: lgvid.cpp
	$(CXX) $(CXXFLAGS_ALL) $(CPPFLAGS_ALL) -c -o $@ $<

$(SONAME): lgvid.o
	$(CXX) $(CXXFLAGS_ALL) -shared $(LDFLAGS_ALL) $^ -o $@ $(LIBS)
