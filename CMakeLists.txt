cmake_minimum_required(VERSION 3.12)
project(lgvid)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(USE_STD_THREAD "Use C++ threads" ON)
option(FFMPEG_DLL "Use runtime dynamic linking for FFmpeg shared libraries" OFF)
option(FFMPEG_DLL_COMBINED "Use single shared library for all FFmpeg library routines" OFF)

if (NOT FFMPEG_DLL)
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
endif()

include_directories(
	${FFMPEG_INCLUDE_DIRS}
	${lgvid_SOURCE_DIR}
	${lgvid_SOURCE_DIR}/ffmpeg
)

add_compile_definitions(SHIP)
if (USE_STD_THREAD)
	add_compile_definitions(USE_STD_THREAD)
endif()
if (FFMPEG_DLL)
	add_compile_definitions(FFMPEG_DLL)
	if (FFMPEG_DLL_COMBINED)
		add_compile_definitions(FFMPEG_DLL_COMBINED)
	endif()
endif()
if (NOT WIN32)
	add_compile_options("SHELL:-include comcompat.h")
endif()

add_library(
	lgvid
	SHARED
	lgviddecoder.h
	lgvid.cpp
)

if (NOT FFMPEG_DLL)
	target_link_libraries(lgvid PRIVATE ${FFMPEG_LIBRARIES})

	target_link_directories(lgvid PRIVATE ${FFMPEG_LIBRARY_DIRS})
endif()

if (WIN32)
	if (NOT MSVC)
		target_link_options(lgvid PRIVATE "-Wl,--exclude-all-symbols")
	endif()
else()
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
install(
	FILES
	lgviddecoder.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

