
# LGVid

## About

LGVid is a video and movie playback library for games based on the Dark Engine. It employs FFmpeg to support a wide range of containers, audio, and video formats.  
This repository is a fork of the original release of LGVid included with the NewDark binaries. It is currently based on the latest original release.  

## Building

The FFmpeg (version 9.0) headers, minimally, are required if you are not also building FFmpeg. Said libraries can be linked statically or dynamically instead by defining `FFMPEG_DLL`. If dynamically linking, the libraries can be in their original separated forms or in a combined `ffmpeg.dll` if preferred. If statically linking, refer to the [FFmpeg documentation](https://ffmpeg.org/documentation.html) for building instructions and the necessary libraries to do so.  
To build the source on Windows, you will need Visual Studio 2008 Professional with Service Pack 1 or MinGW-w64.  
Windows releases are built with Visual Studio 2008.  

