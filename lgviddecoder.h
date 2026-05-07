/* lgvideodecoder.h is free software. It comes without any warranty,
 * to the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details.
 */

#ifndef __LGVIDDECODER_H
#define __LGVIDDECODER_H

#ifndef BOOL
#define BOOL int
#define FALSE 0
#define TRUE 1
#endif


#ifdef _WIN32
#pragma pack(push, 4)
#endif

#ifndef __cplusplus
enum
{
	LGVID_SEEK_Set,
	LGVID_SEEK_Cur,
	LGVID_SEEK_End
};

typedef struct sLGVidFrameFormat
{
	int width;
	int height;
	int bpp;
	unsigned int rmask;
	unsigned int gmask;
	unsigned int bmask;
	BOOL cropped;
	int croprect[4];
} sLGVidFrameFormat;

typedef struct sLGVidLockResult
{
	char *buffer;
	int pitch;
} sLGVidLockResult;
#endif

// host interface that allows a LGVideoDecoder to access necessary host functions
#undef INTERFACE
#define INTERFACE ILGVideoDecoderHost
DECLARE_INTERFACE(ILGVideoDecoderHost)
{
#ifdef __cplusplus
public:
	enum
	{
		SEEK_Set,
		SEEK_Cur,
		SEEK_End
	};

	struct sFrameFormat
	{
		int width;
		int height;
		int bpp;
		unsigned int rmask;
		unsigned int gmask;
		unsigned int bmask;
		// Cropping params are only for information and for placement of possible subtitles
		// the decoder should not actually crop the image, that is done by the host.
		// Note that cropping may be toggle during playback so subtitles would have to always
		// keep up to date for correct positioning.
		BOOL cropped;
		int croprect[4];
	};

	struct sLockResult
	{
		char *buffer;
		int pitch;
	};

public:
#endif
	// get config var value, can be used if decoder has any user configurable settings
	STDMETHOD_(BOOL,GetConfigValue)(THIS_ const char *name, char *buffer, int len) PURE;

	// output string to log (debug output)
	STDMETHOD_(void,LogPrint)(THIS_ const char *s) PURE;

	//
	// file I/O
	//

	// open file, returns a file handle, NULL if failed
	STDMETHOD_(void*,FileOpen)(THIS_ const char *filename) PURE;
	// close file handle
	STDMETHOD_(void,FileClose)(THIS_ void *handle) PURE;
	// get file size
	STDMETHOD_(size_t,FileSize)(THIS_ void *handle) PURE;
	// read data from file
	STDMETHOD_(size_t,FileRead)(THIS_ void *handle, void *buf, size_t count) PURE;
	// move current file read pos
#ifdef _WIN32
	STDMETHOD_(size_t,FileSeek)(THIS_ void *handle, long offset, int origin) PURE;
#else
	STDMETHOD_(size_t,FileSeek)(THIS_ void *handle, int offset, int origin) PURE;
#endif

	//
	// audio buffer access
	//

	// create audio buffer for sound playback if video contains audio track (may only be called once per decoder)
	// (audio data is expected to be 16-bit signed)
	STDMETHOD_(BOOL,CreateAudioBuffer)(THIS_ int nSampleRate, int nChannels, int nBufferSize) PURE;

	// load audio data to sound buffer, may only be called from inside RequestAudio (can be called several times)
	// returns number of bytes actually queued
	STDMETHOD_(int,QueueAudioData)(THIS_ void *data, int len) PURE;

	//
	// video buffer access
	//

	// get frame buffer format
#ifdef __cplusplus
	STDMETHOD_(void,GetFrameFormat)(THIS_ sFrameFormat &fmt) PURE;
#else
	STDMETHOD_(void,GetFrameFormat)(THIS_ sLGVidFrameFormat *fmt) PURE;
#endif

	// create an image buffer that can be used to store a video frame using LockBuffer/UnlockBuffer
	// the decoder can create several buffers to queue multiple frame internally
	// returns handle or NULL if create failed
	STDMETHOD_(void*,CreateImageBuffer)(THIS) PURE;

	// lock/unlock image buffer so a video frame can be copied to it
#ifdef __cplusplus
	STDMETHOD_(BOOL,LockBuffer)(THIS_ void *handle, sLockResult &lock) PURE;
#else
	STDMETHOD_(BOOL,LockBuffer)(THIS_ void *handle, sLGVidLockResult *lock) PURE;
#endif
	STDMETHOD_(void,UnlockBuffer)(THIS_ void *handle) PURE;

	// present an image buffer to the host's video frame buffer, may only be called from inside RequestVideoFrame
	// EndVideoFrame may stall while waiting for vsync if the frame buffer is the screen
	STDMETHOD_(void,BeginVideoFrame)(THIS_ void *handle) PURE;
	STDMETHOD_(void,EndVideoFrame)(THIS) PURE;

	//
	// Available in T2 v1.28+ / SS2 v2.48+
	//

	// same as CreateAudioBuffer, but also permits setting a custom bit depth
	// (8/16/24/32 are supported)
	STDMETHOD_(BOOL,CreateAudioBuffer2)(int nSampleRate, int nChannels, int nBitsPerSample, int nBufferSize) PURE;
};


// decoder interface
#undef INTERFACE
#define INTERFACE ILGVideoDecoder
DECLARE_INTERFACE(ILGVideoDecoder)
{
#ifdef __cplusplus
public:
#endif
	// shut down and delete decoder instance
	STDMETHOD_(void,Destroy)(THIS) PURE;

	// start decoding for playback
	STDMETHOD_(BOOL,Start)(THIS) PURE;

	// returns FALSE as long as video hasn't finished
	STDMETHOD_(BOOL,IsFinished)(THIS) PURE;

	// returns TRUE if a video frame is available
	STDMETHOD_(BOOL,IsVideoFrameAvailable)(THIS) PURE;

	// called to request another video frame, data is sent to host with BeginVideoFrame/EndVideoFrame
	STDMETHOD_(void,RequestVideoFrame)(THIS) PURE;

	// called to request more audio data, data is sent to host with QueueAudioData
	// 'len' is set to TRUE to notify that audio system has completed playing all audio
	// this function can be called from another thread
	STDMETHOD_(void,RequestAudio)(THIS_ unsigned int len) PURE;
};

// updated decoder interface supported by T2 v1.22+ / SS2 v2.43+
#undef INTERFACE
#define INTERFACE ILGVideoDecoder2
DECLARE_INTERFACE_(ILGVideoDecoder2, ILGVideoDecoder)
{
#ifdef __cplusplus
public:
#endif
	// ILGVideoDecoder methods

	STDMETHOD_(void,Destroy)(THIS) PURE;
	STDMETHOD_(BOOL,Start)(THIS) PURE;
	STDMETHOD_(BOOL,IsFinished)(THIS) PURE;
	STDMETHOD_(BOOL,IsVideoFrameAvailable)(THIS) PURE;
	STDMETHOD_(void,RequestVideoFrame)(THIS) PURE;
	STDMETHOD_(void,RequestAudio)(THIS_ unsigned int len) PURE;

	// ILGVideoDecoder2 methods

	// returns the current playback time in milliseconds (used to better synchronize subtitles)
	STDMETHOD_(size_t,GetCurrentPlaybackTime)(THIS) PURE;
};


typedef ILGVideoDecoder* (*PCREATELGVIDEODECODER)(ILGVideoDecoderHost *pHostIface, const char *filename);
typedef ILGVideoDecoder2* (*PCREATELGVIDEODECODER2)(ILGVideoDecoderHost *pHostIface, const char *filename);

/*

// video decoder DLL interface, this function is called by dark to create a decoder instance for the supplied movie file
// CreateLGVideoDecoder2 is optional and used by T2 v1.22+ / SS2 v2.43+ instead of CreateLGVideoDecoder when available

extern "C" __declspec(dllexport) ILGVideoDecoder* CreateLGVideoDecoder(ILGVideoDecoderHost *pHostIface, const char *filename);
extern "C" __declspec(dllexport) ILGVideoDecoder2* CreateLGVideoDecoder2(ILGVideoDecoderHost *pHostIface, const char *filename);

*/

#ifdef _WIN32
#pragma pack(pop)
#endif

#endif /* !__LGVIDDECODER_H */
