cmake_minimum_required(VERSION 3.12)
project(LGVID)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(
	FFMPEG
	REQUIRED
	libswscale
	libswresample
	libavutil
	libavformat
	libavcodec
)

include_directories(
	${FFMPEG_INCLUDE_DIRS}
	${LGVID_SOURCE_DIR}
)

add_compile_definitions(SHIP USE_STD_THREAD)
if (NOT WIN32)
	add_compile_options("SHELL:-include comcompat.h")
endif()

add_library(
	lgvid
	SHARED
	lgviddecoder.h
	lgvid.cpp
)

target_link_libraries(lgvid PRIVATE ${FFMPEG_LIBRARIES})

if (NOT WIN32)
	target_link_options(lgvid PRIVATE "-Wl,--no-undefined")
endif()

set_target_properties(
	lgvid
	PROPERTIES
	SOVERSION 1
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
)

install(
	TARGETS
	lgvid
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

