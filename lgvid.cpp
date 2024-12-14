/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.
 */

// Based on tutorial08.c
// A pedagogical video player that really works!
//
// Code based on FFplay, Copyright (c) 2003 Fabrice Bellard,
// and a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)


// load ffmpeg as a DLL on MSVC by default
#if defined(_MSC_VER) && !defined(FFMPEG_DLL) && !defined(FFMPEG_FORCE_NO_DLL)
#define FFMPEG_DLL
#endif
#if defined(FFMPEG_DLL) && !defined(_WIN32)
#error Runtime dynamic linking is supported only on Windows
#endif


#if __cplusplus < 201103L
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#endif

#include <new>
#if __cplusplus >= 201103L && defined(USE_STD_THREAD)
#include <condition_variable>
#include <mutex>
#include <thread>
#elif !defined(_WIN32)
#error C++11 must be used with USE_STD_THREAD defined on non-Windows platforms
#endif

#if _WIN32
#include <windows.h>
#if __cplusplus < 201103L || !defined(USE_STD_THREAD)
#include <process.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif
#else
#include <unistd.h>
#define DWORD int
#define Sleep(x) usleep(x)
#endif

extern "C"
{
#ifdef _WIN32
// must have same alignment as the ffmpeg libs
#ifndef FFMPEG_ALIGN
#define FFMPEG_ALIGN 8
#endif
#pragma pack(push, FFMPEG_ALIGN)
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
#ifdef _WIN32
#pragma pack(pop)
#endif
}

#include "lgviddecoder.h"


#ifdef _DEBUG
#define DEBUG
#endif
#ifdef DEBUG
#include <assert.h>
#define Warning(_x) FFmpeg::mprintf _x
#define Assert_(_x) assert(_x)
#define AssertMsg(_x, _msg) assert(_x)
#define AssertMsg1(_x, _msg, _a1) assert(_x)
#else
#define Warning(_x)
#define Assert_(_x)
#define AssertMsg(_x, _msg)
#define AssertMsg1(_x, _msg, _a1)
#endif


#define AUDIO_BUFFER_SIZE 16*1024
#define MIN_AUDIOQ_SIZE (20 * 16 * 1024)
#define MIN_FRAMES 5
#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0
#define SAMPLE_CORRECTION_PERCENT_MAX 10
#define AUDIO_DIFF_AVG_NB 20
#define VIDEO_PICTURE_QUEUE_SIZE 2
#define DEFAULT_AV_SYNC_TYPE AV_SYNC_EXTERNAL_MASTER

static int sws_flags = SWS_BICUBIC;
static AVSampleFormat out_audio_fmt = AV_SAMPLE_FMT_S16;
static int out_audio_nb_ch = 2;


#ifdef DEBUG
// uncomment to simulate movie playback on slower CPU
//#define SIMULATE_SLOW_CPU

// simulated extra time (ms) for an audio packet to decode
#define AUDIO_DECODE_STALL (1000/60)
// simluated extra time (ms) for a video frame to decode
#define VIDEO_DECODE_STALL (1000/25)
#endif

///////////////////////////////////////////////////////////////////////////////

struct VideoState;

class cLGVideoDecoder : public ILGVideoDecoder2
{
public:
	ILGVideoDecoderHost *m_pHostIface;

	VideoState *is;

public:
	cLGVideoDecoder(ILGVideoDecoderHost *pHostIface);
	virtual ~cLGVideoDecoder();

	// ILGVideoDecoder interface implementation
	STDMETHOD_(void,Destroy)();
	STDMETHOD_(BOOL,Start)();
	STDMETHOD_(BOOL,IsFinished)();
	STDMETHOD_(BOOL,IsVideoFrameAvailable)();
	STDMETHOD_(void,RequestVideoFrame)();
	STDMETHOD_(void,RequestAudio)(unsigned int len);

	// ILGVideoDecoder2 interface implementation
	STDMETHOD_(size_t,GetCurrentPlaybackTime)();

	void Stop();
	BOOL Init(const char *filename);
};


///////////////////////////////////////////////////////////////////////////////
//
// FFmpeg dynamic lib API
//

namespace FFmpeg
{
	void* (*av_malloc)(size_t size);
	void (*av_freep)(void *ptr);
	int64_t (*av_gettime)(void);
	int (*av_get_bytes_per_sample)(enum AVSampleFormat sample_fmt);
	int (*av_samples_get_buffer_size)(int *linesize, int nb_channels, int nb_samples, enum AVSampleFormat sample_fmt, int align);
	int (*av_channel_layout_check)(const AVChannelLayout *channel_layout);
	void (*av_channel_layout_default)(AVChannelLayout *ch_layout, int nbchannels);
	AVFrame* (*av_frame_alloc)(void);
	void (*av_frame_free)(AVFrame **frame);
#ifndef SHIP
	void (*av_log_set_callback)(void (*callback)(void*, int, const char*, va_list));
	void (*av_log_set_level)(int level);
#endif

	int (*av_read_frame)(AVFormatContext *s, AVPacket *pkt);
	int (*av_find_best_stream)(AVFormatContext *ic, enum AVMediaType type, int wanted_stream_nb, int related_stream, const AVCodec **decoder_ret, int flags);
	int (*av_probe_input_buffer)(AVIOContext *pb, const AVInputFormat **fmt, const char *filename, void *logctx, unsigned int offset, unsigned int max_probe_size);
#ifdef DEBUG
	void (*av_dump_format)(AVFormatContext *ic, int index, const char *url, int is_output);
#endif

	AVFormatContext* (*avformat_alloc_context)(void);
	int (*avformat_open_input)(AVFormatContext **ps, const char *url, const AVInputFormat *fmt, AVDictionary **options);
	void (*avformat_close_input)(AVFormatContext **s);
	int (*avformat_find_stream_info)(AVFormatContext *ic, AVDictionary **options);

	AVIOContext* (*avio_alloc_context)(unsigned char *buffer, int buffer_size, int write_flag, void *opaque, int (*read_packet)(void *opaque, uint8_t *buf, int buf_size), int (*write_packet)(void *opaque, /* const uint8_t *buf */ uint8_t *buf, int buf_size), int64_t (*seek)(void *opaque, int64_t offset, int whence));
	void (*avio_context_free)(AVIOContext **s);

	AVPacket* (*av_packet_alloc)(void);
	void (*av_packet_free)(AVPacket **pkt);
	void (*av_packet_move_ref)(AVPacket *dst, AVPacket *src);
	void (*av_packet_unref)(AVPacket *pkt);

	AVCodecContext* (*avcodec_alloc_context3)(const AVCodec *codec);
	void (*avcodec_free_context)(AVCodecContext **avctx);
	int (*avcodec_parameters_to_context)(AVCodecContext *codec, const AVCodecParameters *par);
	int (*avcodec_open2)(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options);
	const AVCodec* (*avcodec_find_decoder)(enum AVCodecID id);
	int (*avcodec_send_packet)(AVCodecContext *avctx, const AVPacket *avpkt);
	int (*avcodec_receive_frame)(AVCodecContext *avctx, AVFrame *frame);

	int (*swr_alloc_set_opts2)(struct SwrContext **ps, const AVChannelLayout *out_ch_layout, enum AVSampleFormat out_sample_fmt, int out_sample_rate, const AVChannelLayout *in_ch_layout, enum AVSampleFormat in_sample_fmt, int in_sample_rate, int log_offset, void *log_ctx);
	void (*swr_free)(struct SwrContext **s);
	int (*swr_init)(struct SwrContext *s);
	int (*swr_convert)(struct SwrContext *s, /* uint8_t *const*out */ uint8_t **out, int out_count, /* const uint8_t *const*in */ const uint8_t **in, int in_count);

	struct SwsContext* (*sws_getCachedContext)(struct SwsContext *context, int srcW, int srcH, enum AVPixelFormat srcFormat, int dstW, int dstH, enum AVPixelFormat dstFormat, int flags, SwsFilter *srcFilter, SwsFilter *dstFilter, const double *param);
	void (*sws_freeContext)(struct SwsContext *swsContext);
	int (*sws_scale)(struct SwsContext *c, const uint8_t* const srcSlice[], const int srcStride[], int srcSliceY, int srcSliceH, uint8_t* const dst[], const int dstStride[]);

	//

#ifdef FFMPEG_DLL
#ifdef FFMPEG_DLL_COMBINED
#if !defined(FFMPEG_DLL_NAME)
#define FFMPEG_DLL_NAME "ffmpeg.dll"
#endif
	const char *fflibs[1] = { FFMPEG_DLL_NAME };
	HMODULE hDll[1] = { NULL };
#else
	const char *fflibs[5] = {
		// NOTE: keep up-to-date with the FFmpeg version used
		"avutil-58.dll",
		"avformat-60.dll",
		"avcodec-60.dll",
		"swresample-4.dll",
		"swscale-7.dll"
	};
	HMODULE hDll[5] = { NULL };
#endif
#endif
	cLGVideoDecoder *pOuter = NULL;

	//

#ifdef FFMPEG_DLL
	#define FIRST_FF_LIB() unsigned int _fflib = 0;
	#define NEXT_FF_LIB() \
		if (_fflib + 1 < sizeof(fflibs)/sizeof(fflibs[0])) \
		{ \
			_fflib++; \
		}
	#define CUR_FF_LIB() fflibs[_fflib]
	#define INIT_FF_CALL(_name) \
		if (!((void*&)_name = (void*)GetProcAddress(hDll[_fflib], #_name))) \
		{ \
			mprintf("failed to resolve FFmpeg call %s", #_name); \
			goto fail;\
		}
#else
	#define FIRST_FF_LIB()
	#define NEXT_FF_LIB()
	#define CUR_FF_LIB()
	#define INIT_FF_CALL(_name) _name = :: _name;
#endif

	static void mprintf(const char *fmt, ...)
	{
		char buf[1536];

		if (pOuter)
		{
			va_list ap;
			va_start(ap, fmt);
			vsprintf(buf, fmt, ap);
			va_end(ap);

			pOuter->m_pHostIface->LogPrint(buf);
		}
	}

#ifndef SHIP
	static void ffmpeg_log_callback(void *ptr, int, const char *fmt, va_list vl)
	{
		char s[1024] = {0};

		vsprintf(s, fmt, vl);

		AVClass *avc = ptr ? *(AVClass **)ptr : NULL;

		if (ptr)
		{
			if (avc)
				mprintf("FFMPEG> %s[%s]: %s", avc->class_name, avc->item_name(ptr), s);
			else
				mprintf("FFMPEG> %s: %s", avc->class_name, s);
		}
		else
			mprintf("FFMPEG> %s", s);
	}
#endif

	void Shutdown()
	{
#ifdef FFMPEG_DLL
		for (unsigned int i = 0; i < sizeof(fflibs)/sizeof(fflibs[0]); i++)
		{
			if (hDll[i] && (!pOuter || !pOuter->m_pHostIface->GetConfigValue("no_unload_ffmpeg", NULL, 0)))
			{
				FreeLibrary(hDll[i]);
				hDll[i] = NULL;
			}
		}
#endif

		pOuter = NULL;
	}

	BOOL Init(cLGVideoDecoder *pOuter_)
	{
		pOuter = pOuter_;

#ifdef FFMPEG_DLL
		BOOL loaded = TRUE;
		for (unsigned int i = 0; i < sizeof(fflibs)/sizeof(fflibs[0]); i++)
		{
			if (hDll[i])
				continue;
			else if (loaded)
				loaded = FALSE;

			hDll[i] = LoadLibrary(fflibs[i]);
			if (!hDll[i])
			{
				// NOTE: could show message box (once) here, OTOH message boxes and fullscreen is just a bad combo
				char s[MAX_PATH];
				s[0] = 0;
				GetCurrentDirectory(sizeof(s), s);
				mprintf("Failed to find/load %s, no movie playback possible (err 0x%X, cwd: %s)", fflibs[i], GetLastError(), s);
				return FALSE;
			}
		}
		if (loaded)
			return TRUE;
#endif

		FIRST_FF_LIB(); // libavutil
		INIT_FF_CALL(av_malloc);
		INIT_FF_CALL(av_freep);
		INIT_FF_CALL(av_gettime);
		INIT_FF_CALL(av_get_bytes_per_sample);
		INIT_FF_CALL(av_samples_get_buffer_size);
		INIT_FF_CALL(av_channel_layout_check);
		INIT_FF_CALL(av_channel_layout_default);
		INIT_FF_CALL(av_frame_alloc);
		INIT_FF_CALL(av_frame_free);
#ifndef SHIP
		INIT_FF_CALL(av_log_set_callback);
		INIT_FF_CALL(av_log_set_level);
#endif

		NEXT_FF_LIB(); // libavformat
		INIT_FF_CALL(av_read_frame);
		INIT_FF_CALL(av_find_best_stream);
		INIT_FF_CALL(av_probe_input_buffer);
#ifdef DEBUG
		INIT_FF_CALL(av_dump_format);
#endif

		INIT_FF_CALL(avformat_alloc_context);
		INIT_FF_CALL(avformat_open_input);
		INIT_FF_CALL(avformat_close_input);
		INIT_FF_CALL(avformat_find_stream_info);

		INIT_FF_CALL(avio_alloc_context);
		INIT_FF_CALL(avio_context_free);

		NEXT_FF_LIB(); // libavcodec
		INIT_FF_CALL(av_packet_alloc);
		INIT_FF_CALL(av_packet_free);
		INIT_FF_CALL(av_packet_move_ref);
		INIT_FF_CALL(av_packet_unref);

		INIT_FF_CALL(avcodec_alloc_context3);
		INIT_FF_CALL(avcodec_free_context);
		INIT_FF_CALL(avcodec_parameters_to_context);
		INIT_FF_CALL(avcodec_open2);
		INIT_FF_CALL(avcodec_find_decoder);
		INIT_FF_CALL(avcodec_send_packet);
		INIT_FF_CALL(avcodec_receive_frame);

		NEXT_FF_LIB(); // libswresample
		INIT_FF_CALL(swr_alloc_set_opts2);
		INIT_FF_CALL(swr_free);
		INIT_FF_CALL(swr_init);
		INIT_FF_CALL(swr_convert);

		NEXT_FF_LIB(); // libswscale
		INIT_FF_CALL(sws_getCachedContext);
		INIT_FF_CALL(sws_freeContext);
		INIT_FF_CALL(sws_scale);

#ifndef SHIP
		if ( pOuter->m_pHostIface->GetConfigValue("ffmpeg_spew", NULL, 0) )
		{
			av_log_set_callback(ffmpeg_log_callback);

			av_log_set_level(99);
		}
#endif

		return TRUE;

#ifdef FFMPEG_DLL
fail:
		mprintf("WARNING: cannot play movie, wrong version of \"%s\"", CUR_FF_LIB());

		Shutdown();

		return FALSE;
#endif
	}

	#undef INIT_FF_CALL
	#undef NEXT_FF_LIB
	#undef CUR_FF_LIB
	#undef INIT_FF_CALL

	//
	// I/O interface
	//

	struct IOContext
	{
		void *pStream;
		unsigned long size;

		IOContext(void *str) { pStream = str; size = pOuter->m_pHostIface->FileSize(str); }
	};

	static int Read(void *opaque, uint8_t *buf, int buf_size)
	{
		IOContext *io = (IOContext*)opaque;
		int bytes = pOuter->m_pHostIface->FileRead(io->pStream, buf, buf_size);
		return (bytes != 0) ? bytes : AVERROR_EOF;
	}

	static int64_t Seek(void *opaque, int64_t offset, int whence)
	{
		IOContext *io = (IOContext*)opaque;
		return (whence == AVSEEK_SIZE) ? (int64_t)io->size : pOuter->m_pHostIface->FileSeek(io->pStream, (long)offset, whence);
	}

	int OpenFile(AVFormatContext **ic_ptr, const char *filename)
	{
		void *pStream = pOuter->m_pHostIface->FileOpen(filename);
		if (!pStream)
		{
			mprintf("failed to open file '%s'", filename);
			return -1;
		}

		IOContext *ctxt = new (std::nothrow) IOContext(pStream);
		if (!ctxt)
			return -1;

		const AVInputFormat *fmt = NULL;
		int ret = 0;

		AVIOContext *pb = avio_alloc_context(NULL, 0, 0, ctxt, Read, NULL, Seek);
		(*ic_ptr)->pb = pb;

		// determine format
		if (av_probe_input_buffer(pb, &fmt, filename, NULL, 0, 0) < 0 || !fmt)
		{
			Warning(("failed to determine file format '%s'\n", filename));
			ret = -1;
			goto fail;
		}

		ret = avformat_open_input(ic_ptr, filename, fmt, 0);

		if (ret)
		{
fail:
			pOuter->m_pHostIface->FileClose(pStream);
			delete ctxt;
			avio_context_free(&pb);
		}

		return ret;
	}

	void CloseFile(AVFormatContext *s)
	{
		if (s)
		{
			AVIOContext *pb = s->pb;

			if (pb && pb->opaque)
			{
				IOContext *io = (IOContext*)pb->opaque;

				if (io->pStream)
					pOuter->m_pHostIface->FileClose(io->pStream);

				delete io;
				pb->opaque = NULL;
			}

			avio_context_free(&pb);
		}

		avformat_close_input(&s);
	}
}


#ifdef _WIN32
#pragma pack(8)
#endif


///////////////////////////////////////////////////////////////////////////////
// Thread Utilities

#if __cplusplus >= 201103L && defined(USE_STD_THREAD)
class cThreadMutex
{
public:
	operator std::mutex& () { return mutex; }

	BOOL Wait()
	{
		mutex.lock();

		return TRUE;
	}

	BOOL Release()
	{
		mutex.unlock();

		return TRUE;
	}

private:
	std::mutex mutex;
};


class cWorkerThread
{
public:
	cWorkerThread() : thread() { }

	virtual ~cWorkerThread()
	{
		if (thread)
			WaitForClose();
	}

	BOOL Create()
	{
		if (thread)
			return FALSE;

		thread = new (std::nothrow) std::thread(cWorkerThread::thread_proc_wrapper, this);

		return thread != NULL;
	}

	void WaitForClose()
	{
		if (!thread)
			return;

		thread->join();
		delete thread;
		thread = NULL;
	}

protected:
	virtual DWORD ThreadProc() = 0;

private:
	static void thread_proc_wrapper(void *pv)
	{
		cWorkerThread *thread = (cWorkerThread *) pv;
		thread->ThreadProc();
	}

	std::thread *thread;
};
#else
class cThreadLock
{
public:
	cThreadLock() { InitializeCriticalSection(&m_CritSec); }
	~cThreadLock() { DeleteCriticalSection(&m_CritSec); }
	void Lock() { EnterCriticalSection(&m_CritSec); }
	void Unlock() { LeaveCriticalSection(&m_CritSec); }
private:
	CRITICAL_SECTION m_CritSec;
};

class cAutoLock
{
public:
	cAutoLock(cThreadLock &lock) : m_lock(lock) { m_lock.Lock(); }
	~cAutoLock() { m_lock.Unlock(); }
private:
	cThreadLock &m_lock;
};

class cThreadSyncObject
{
public:
	~cThreadSyncObject() { if (m_hSyncObject) CloseHandle(m_hSyncObject); }
	BOOL operator!() const { return !m_hSyncObject; }
	operator HANDLE () { return m_hSyncObject; }
	BOOL Wait(DWORD dwTimeout = INFINITE) { return WaitForSingleObject(m_hSyncObject, dwTimeout) == WAIT_OBJECT_0; }
protected:
	cThreadSyncObject() : m_hSyncObject(NULL) {}
	HANDLE m_hSyncObject;
};

class cThreadEvent : public cThreadSyncObject
{
public:
	cThreadEvent(BOOL fManualReset = FALSE) { m_hSyncObject = CreateEvent(NULL, fManualReset, FALSE, NULL); }
	BOOL Set() { return SetEvent(m_hSyncObject); }
	BOOL Reset() { return ResetEvent(m_hSyncObject); }
	BOOL Pulse() { return PulseEvent(m_hSyncObject); }
	BOOL Check() { return Wait(0); }
};

class cThreadSemaphore : public cThreadSyncObject
{
public:
	cThreadSemaphore(long initialValue, long maxValue) { m_hSyncObject = CreateSemaphore(NULL, initialValue, maxValue, NULL); }
	BOOL Release(long releaseCount = 1, long * pPreviousCount = NULL) { return ReleaseSemaphore(m_hSyncObject, releaseCount, pPreviousCount); }
};

class cThreadMutex : public cThreadSyncObject
{
public:
	cThreadMutex(BOOL fEstablishInitialOwnership = FALSE) { m_hSyncObject = CreateMutex(NULL, fEstablishInitialOwnership, NULL); }
	BOOL Release() { return ReleaseMutex(m_hSyncObject); }
};


class cWorkerThread
{
public:
	cWorkerThread() : m_hThread(NULL), m_EventSend(TRUE) {}
	virtual ~cWorkerThread() { if (m_hThread) WaitForClose(); }
	BOOL Create()
	{
		unsigned int threadid;
		cAutoLock lock(m_Lock);
		if (ThreadExists())
		{
			AssertMsg(FALSE, "thread already created");
			return FALSE;
		}
		m_hThread = (HANDLE) _beginthreadex(NULL, 0, cWorkerThread::InitialThreadProc, this, 0, &threadid);
		AssertMsg1(m_hThread, "create thread failed (%x)", GetLastError());
		if (!m_EventComplete.Wait(10000))
		{
			AssertMsg(FALSE, "timed out waiting for worker thread to init");
		}
		return m_hThread != NULL;
	}
	void WaitForClose(DWORD dwErrorTimeout = 10000)
	{
		if (!m_hThread)
			return;
		if (WaitForSingleObject(m_hThread, dwErrorTimeout) == WAIT_TIMEOUT)
		{
			AssertMsg(FALSE, "timed out waiting for worker thread to close");
		}
		CloseHandle(m_hThread);
		m_hThread = NULL;
	}
	BOOL ThreadExists() { DWORD dwExitCode; return (m_hThread && GetExitCodeThread(m_hThread, &dwExitCode) && dwExitCode == STILL_ACTIVE); }
	int GetPriority() const { return GetThreadPriority(m_hThread); }
	BOOL SetPriority(int priority) { return SetThreadPriority(m_hThread, priority); }
	DWORD Suspend() { return SuspendThread(m_hThread); }
	DWORD Resume() { return ResumeThread(m_hThread); }
	BOOL Terminate(DWORD dwExitCode) { return TerminateThread(m_hThread, dwExitCode); }
	DWORD CallWorker(DWORD dw, BOOL fBoostWorkerPriorityToMaster = TRUE) { return Call(dw, fBoostWorkerPriorityToMaster); }
	DWORD CallMaster(DWORD dw) { return Call(dw, FALSE); }
	DWORD WaitForCall() { m_EventSend.Wait(); return m_dwParam; }
	BOOL PeekCall(DWORD *pParam = NULL) { if (!m_EventSend.Check()) return FALSE; if (pParam) *pParam = m_dwParam; return TRUE; }
	void Reply(DWORD dw) { m_dwParam = kInvalidCallParam; m_dwReturnVal = dw; m_EventSend.Reset(); m_EventComplete.Set(); }
	HANDLE GetCallHandle() { return m_EventSend; }
	DWORD GetCallParam() const { return m_dwParam; }
protected:
	enum { kInvalidCallParam = 0xffffffff };
	virtual DWORD ThreadProc() = 0;
	cThreadLock m_Lock;
private:
	DWORD Call(DWORD dwParam, BOOL fBoostPriority)
	{
		AssertMsg(!m_EventSend.Check(), "nested inter-thread call");
		cAutoLock lock(m_Lock);
		if (!ThreadExists())
			return E_FAIL;
		int iInitialPriority;
		if (fBoostPriority)
		{
			iInitialPriority = GetPriority();
			const int iNewPriority = GetThreadPriority(GetCurrentThread());
			if (iNewPriority > iInitialPriority)
				SetPriority(iNewPriority);
		}
		m_dwParam = dwParam;
		m_EventSend.Set();
		m_EventComplete.Wait();
		if (fBoostPriority)
			SetPriority(iInitialPriority);
		return m_dwReturnVal;
	}
	static unsigned __stdcall InitialThreadProc(LPVOID pv)
	{
		cWorkerThread * pThread = (cWorkerThread *) pv;
		pThread->m_EventComplete.Set();
		return pThread->ThreadProc();
	}
	HANDLE          m_hThread;
	cThreadEvent    m_EventSend;
	cThreadEvent    m_EventComplete;
	DWORD           m_dwParam;
	DWORD           m_dwReturnVal;
};
#endif

///////////////////////////////////////////////////////////////////////////////
// SDL_Cond

#if __cplusplus >= 201103L && defined(USE_STD_THREAD)
typedef std::condition_variable SDL_cond;


int SDL_CondSignal(SDL_cond &cond)
{
	cond.notify_one();

	return 0;
}

int SDL_CondWait(SDL_cond &cond, cThreadMutex &mutex)
{
	std::unique_lock<std::mutex> lock(mutex, std::defer_lock);
	cond.wait(lock);

	return 0;
}
#else
struct SDL_cond
{
	cThreadMutex lock;
	int waiting;
	int signals;
	cThreadSemaphore wait_sem;
	cThreadSemaphore wait_done;

	SDL_cond() : wait_sem(0, 32*1024), wait_done(0, 32*1024)
	{
		waiting = signals = 0;
	}
};


int SDL_CondSignal(SDL_cond &cond)
{
	cond.lock.Wait();

	if (cond.waiting > cond.signals) {
		++cond.signals;
		cond.wait_sem.Release();
		cond.lock.Release();
		cond.wait_done.Wait();
	} else {
		cond.lock.Release();
	}

	return 0;
}

int SDL_CondWaitTimeout(SDL_cond &cond, cThreadMutex &mutex, uint32_t ms)
{
	int retval;

	cond.lock.Wait();
	++cond.waiting;
	cond.lock.Release();

	mutex.Release();

	retval = !cond.wait_sem.Wait(ms);

	cond.lock.Wait();

	if (cond.signals > 0) {
		if (retval > 0)
			cond.wait_sem.Wait();
		cond.wait_done.Release();
		--cond.signals;
	}

	--cond.waiting;
	cond.lock.Release();

	mutex.Wait();

	return retval;
}

inline int SDL_CondWait(SDL_cond &cond, cThreadMutex &mutex)
{
	return SDL_CondWaitTimeout(cond, mutex, ~0);
}
#endif


///////////////////////////////////////////////////////////////////////////////
//
// PacketQueue
//

struct PacketQueue
{
	struct PacketList
	{
		AVPacket *pkt;
		PacketList *next;
	};

	PacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	cThreadMutex mutex;
	SDL_cond cond;

	PacketQueue() : first_pkt(NULL), last_pkt(NULL), nb_packets(0), size(0), quit(0), finished(0) { };

	~PacketQueue()
	{
		Flush();
	}

	int Put(AVPacket *src_pkt)
	{
		Assert_(!finished && !quit);

		PacketList *new_pkt;
		AVPacket *pkt;

		new_pkt = (PacketList*)FFmpeg::av_malloc(sizeof(PacketList));
		pkt = FFmpeg::av_packet_alloc();
		if (!new_pkt || !pkt) {
			if (new_pkt)
				FFmpeg::av_freep(&new_pkt);
			else if (pkt)
				FFmpeg::av_packet_free(&pkt);
			return -1;
		}

		FFmpeg::av_packet_move_ref(pkt, src_pkt);
		new_pkt->pkt = pkt;
		new_pkt->next = NULL;

		mutex.Wait();

		if (!last_pkt)
			first_pkt = new_pkt;
		else
			last_pkt->next = new_pkt;
		last_pkt = new_pkt;
		nb_packets++;
		size += new_pkt->pkt->size + sizeof(*new_pkt);

		SDL_CondSignal(cond);

		mutex.Release();
		return 0;
	}

	int Get(AVPacket *dst_pkt)
	{
		PacketList *cur_pkt;
		int ret;

		mutex.Wait();

		for (;;) {
			if (quit) {
				ret = -1;
				break;
			}

			cur_pkt = first_pkt;
			if (cur_pkt) {
				first_pkt = cur_pkt->next;
				if (!first_pkt)
					last_pkt = NULL;
				nb_packets--;
				size -= cur_pkt->pkt->size + sizeof(*cur_pkt);
				FFmpeg::av_packet_move_ref(dst_pkt, cur_pkt->pkt);
				FFmpeg::av_packet_free(&cur_pkt->pkt);
				FFmpeg::av_freep(&cur_pkt);
				ret = 0;
				break;
			} else {
				// If finished and queue is empty, let receiver know
				if (finished) {
					ret = -1;
					break;
				}

				SDL_CondWait(cond, mutex);
			}
		}
		mutex.Release();
		return ret;
	}

	void Flush()
	{
		PacketList *pkt, *tmp;

		mutex.Wait();
		for (pkt = first_pkt; pkt != NULL; pkt = tmp) {
			tmp = pkt->next;
			FFmpeg::av_packet_free(&pkt->pkt);
			FFmpeg::av_freep(&pkt);
		}
		last_pkt = NULL;
		first_pkt = NULL;
		nb_packets = 0;
		size = 0;
		mutex.Release();
	}

	void Quit(int ret = 1)
	{
		mutex.Wait();
		quit = ret;

		SDL_CondSignal(cond);

		mutex.Release();
	}

	void Finished(int ret = 1)
	{
		mutex.Wait();
		finished = ret;

		SDL_CondSignal(cond);

		mutex.Release();
	}

	void Depleted()
	{
		mutex.Wait();

		finished = 1;
		if (!first_pkt)
			SDL_CondSignal(cond);

		mutex.Release();
	}

private:
	int quit;
	int finished;
};


///////////////////////////////////////////////////////////////////////////////
//
// VideoState
//

class DecodeThread;
class VideoThread;

struct VideoState
{
	struct VideoPicture
	{
		void *bmp;
		double pts;				///<presentation time stamp for this picture
		double target_clock;	///<av_gettime() time at which this should be displayed ideally
		int64_t pos;			///<byte position in file
	};

	enum AV_SYNC
	{
		AV_SYNC_AUDIO_MASTER,
		AV_SYNC_VIDEO_MASTER,
		AV_SYNC_EXTERNAL_MASTER
	};

	cLGVideoDecoder *pOuter;

	AVFormatContext *pFormatCtx;
	int             videoStream, audioStream;

	AV_SYNC         av_sync_type;
	double          external_clock; /* external clock base */
	int64_t         external_clock_time;

	double          audio_clock;
	AVCodecContext  *audio_ctx;
	PacketQueue     audioq;
	AVFrame         *audio_frame;
	uint8_t         *audio_buf;
	unsigned int    audio_buf_size;
	unsigned int    audio_buf_index;
	AVPacket        *audio_pkt;
	SwrContext      *avr_context;
	uint8_t         *avr_buffer;
	double          audio_diff_cum; /* used for AV difference average computation */
	double          audio_diff_avg_coef;
	double          audio_diff_threshold;
	int             audio_diff_avg_count;
	double          frame_timer;
	double          frame_last_pts;
	double          frame_last_delay;
	double          video_clock; ///<pts of last decoded frame / predicted pts of next decoded frame
	double          video_current_pts; ///<current displayed pts (different from video_clock if frame fifos are used)
	double          video_current_pts_drift; ///<video_current_pts - time (av_gettime) at which we updated video_current_pts - used to have running video pts
	int64_t         video_current_pos; ///<current displayed file pos
	int64_t         video_current_pts_time;  ///<time (av_gettime) at which we updated video_current_pts - used to have running video pts
	AVCodecContext  *video_ctx;
	PacketQueue     videoq;

	AVPixelFormat   pict_pix_fmt;
	VideoPicture    pictq[VIDEO_PICTURE_QUEUE_SIZE];
	int             pictq_size, pictq_rindex, pictq_windex;
	cThreadMutex    pictq_mutex;
	SDL_cond        pictq_cond;
	DecodeThread    *parse_tid;
	VideoThread     *video_tid;
	int             quit;
	SwsContext      *img_convert_ctx;

	float           skip_frames;
	float           skip_frames_index;
	int             refresh;

	VideoState(cLGVideoDecoder *pOuter_)
		: pOuter(pOuter_),
		pFormatCtx(NULL),
		videoStream(-1),
		audioStream(-1),
		av_sync_type(DEFAULT_AV_SYNC_TYPE),
		external_clock(0),
		external_clock_time(0),
		audio_clock(0),
		audio_ctx(NULL),
		audio_buf(NULL),
		audio_buf_size(0),
		audio_buf_index(0),
		avr_context(NULL),
		avr_buffer(NULL),
		audio_diff_cum(0),
		audio_diff_avg_coef(0),
		audio_diff_threshold(0),
		audio_diff_avg_count(0),
		frame_timer(0),
		frame_last_pts(0),
		frame_last_delay(0),
		video_clock(0),
		video_current_pts(0),
		video_current_pts_drift(0),
		video_current_pos(0),
		video_current_pts_time(0),
		video_ctx(NULL),
		pict_pix_fmt(AV_PIX_FMT_NONE),
		pictq_size(0),
		pictq_rindex(0),
		pictq_windex(0),
		parse_tid(NULL),
		video_tid(NULL),
		quit(0),
		img_convert_ctx(NULL),
		skip_frames(0),
		skip_frames_index(0),
		refresh(0)
	{
		audio_pkt = FFmpeg::av_packet_alloc();
		audio_frame = FFmpeg::av_frame_alloc();

		memset(pictq, 0, sizeof(pictq));
	}

	~VideoState()
	{
		Close();

		FFmpeg::av_packet_free(&audio_pkt);
		FFmpeg::av_frame_free(&audio_frame);
	}

	double get_audio_clock() const
	{
		double pts;
		int hw_buf_size, bytes_per_sec;
		pts = audio_clock;
		hw_buf_size = audio_buf_size - audio_buf_index;
		bytes_per_sec = 0;
		if (audio_ctx) {
			bytes_per_sec = audio_ctx->sample_rate *
				FFmpeg::av_get_bytes_per_sample(out_audio_fmt) * audio_ctx->ch_layout.nb_channels;
		}
		if (bytes_per_sec)
			pts -= (double)hw_buf_size / bytes_per_sec;
		return pts;
	}

	void DecodeFinished()
	{
		videoq.Finished();
		audioq.Finished();
	}

	double get_video_clock() const
	{
		return video_current_pts_drift + FFmpeg::av_gettime() / 1000000.0;
	}

	double get_external_clock() const
	{
		const int64_t ti = FFmpeg::av_gettime();
		return external_clock + ((ti - external_clock_time) * 1e-6);
	}

	double get_master_clock() const
	{
		double val;

		if (av_sync_type == AV_SYNC_VIDEO_MASTER)
		{
			if (video_ctx)
				val = get_video_clock();
			else
				val = get_audio_clock();
		}
		else if (av_sync_type == AV_SYNC_AUDIO_MASTER)
		{
			if (audio_ctx)
				val = get_audio_clock();
			else
				val = get_video_clock();
		}
		else
		{
			val = get_external_clock();
		}

		return val;
	}

	BOOL Open(const char *filename, AVPixelFormat dst_pix_fmt)
	{
		Close();

		pFormatCtx = FFmpeg::avformat_alloc_context();

		// Open video file
		if (FFmpeg::OpenFile(&pFormatCtx, filename)!=0)
			return FALSE; // Couldn't open file

		// Retrieve stream information
		if (FFmpeg::avformat_find_stream_info(pFormatCtx, NULL)<0)
			return FALSE; // Couldn't find stream information

		if (pFormatCtx->pb)
			pFormatCtx->pb->eof_reached = 0;

#ifdef DEBUG
		// Dump information about file onto standard error
		FFmpeg::av_dump_format(pFormatCtx, 0, filename, 0);
#endif

		// Find the first video stream

		int st_index[AVMEDIA_TYPE_NB];
		memset(st_index, -1, sizeof(st_index));

		for (unsigned int i=0; i<pFormatCtx->nb_streams; i++)
			pFormatCtx->streams[i]->discard = AVDISCARD_ALL;

		st_index[AVMEDIA_TYPE_VIDEO] = FFmpeg::av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
		st_index[AVMEDIA_TYPE_AUDIO] = FFmpeg::av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, st_index[AVMEDIA_TYPE_VIDEO], NULL, 0);

		// Open streams
		if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
			stream_component_open(st_index[AVMEDIA_TYPE_AUDIO]);
		}
		if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
			stream_component_open(st_index[AVMEDIA_TYPE_VIDEO]);
		}

		if (videoStream < 0) {
			AssertMsg1(FALSE, "%s: could not open codecs\n", filename);
			return FALSE;
		}

		// Init video scaler
		pict_pix_fmt = dst_pix_fmt;

		// Allocate frame buffer(s)
		for (int i = 0; i < VIDEO_PICTURE_QUEUE_SIZE; ++i)
			pictq[i].bmp = pOuter->m_pHostIface->CreateImageBuffer();

		return TRUE;
	}

	void Stop();

	void Close()
	{
		Stop();

		FFmpeg::swr_free(&avr_context);
		FFmpeg::av_freep(&avr_buffer);

		videoStream = -1;
		audioStream = -1;

		videoq.Flush();
		audioq.Flush();

		// Close the codec
		FFmpeg::avcodec_free_context(&video_ctx);
		FFmpeg::avcodec_free_context(&audio_ctx);

		// Close the video file
		FFmpeg::CloseFile(pFormatCtx);
		pFormatCtx = NULL;

		FFmpeg::sws_freeContext(img_convert_ctx);
		img_convert_ctx = NULL;

		for (int i = 0; i < VIDEO_PICTURE_QUEUE_SIZE; ++i)
			pictq[i].bmp = NULL;
	}

	BOOL Play();

	void video_refresh_timer()
	{
		VideoPicture *vp;

		if (video_ctx) {
retry:
			if (pictq_size != 0) {
				double time = FFmpeg::av_gettime()/1000000.0;
				double next_target;

				/* dequeue the picture */
				vp = &pictq[pictq_rindex];

				if (time < vp->target_clock) {
					Warning(("ffmpeg: too early to display frame t:%g targt:%g", time, vp->target_clock));
					return;
				}

				/* update current video pts */
				video_current_pts = vp->pts;
				video_current_pts_drift = video_current_pts - time;
				video_current_pos = vp->pos;

				if (pictq_size > 1) {
					VideoPicture *nextvp= &pictq[(pictq_rindex+1)%VIDEO_PICTURE_QUEUE_SIZE];
					Assert_(nextvp->target_clock >= vp->target_clock);
					next_target = nextvp->target_clock;
				} else {
					next_target = vp->target_clock + video_clock - vp->pts; //FIXME pass durations cleanly
				}

				#define FRAME_SKIP_FACTOR 0.05f
				const BOOL framedrop = TRUE;

				if (framedrop && time > next_target) {
					skip_frames *= 1.0f + FRAME_SKIP_FACTOR;
					//if (pictq_size > 1 || time > next_target + 0.5) {
					if (pictq_size > 1 && time > next_target + 0.5) {
						/* update queue size and signal for next picture */
						if (++pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE)
							pictq_rindex = 0;

						Warning(("ffmpeg: dropped display frame dt:%g q:%d sf:%g", next_target-time, pictq_size, skip_frames));

						pictq_mutex.Wait();
						pictq_size--;
						SDL_CondSignal(pictq_cond);
						pictq_mutex.Release();
						goto retry;
					}
				}

				// show the picture!
				pOuter->m_pHostIface->BeginVideoFrame(pictq[pictq_rindex].bmp);

				// update queue for next picture!
				if (++pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
					pictq_rindex = 0;
				}

				pictq_mutex.Wait();
				pictq_size--;
				SDL_CondSignal(pictq_cond);
				pictq_mutex.Release();

				pOuter->m_pHostIface->EndVideoFrame();
			}
		}
	}

	double compute_target_time(double frame_current_pts)
	{
		double delay, sync_threshold, diff;

		/* compute nominal delay */
		delay = frame_current_pts - frame_last_pts;
		if (delay <= 0 || delay >= 10.0) {
			/* if incorrect delay, use previous one */
			delay = frame_last_delay;
		} else {
			frame_last_delay = delay;
		}
		frame_last_pts = frame_current_pts;

		/* update delay to follow master synchronisation source */
		if (((av_sync_type == AV_SYNC_AUDIO_MASTER && audio_ctx) ||
			av_sync_type == AV_SYNC_EXTERNAL_MASTER)) {
				/* if video is slave, we try to correct big delays by
				duplicating or deleting a frame */
				diff = get_video_clock() - get_master_clock();

				/* skip or repeat frame. We take into account the
				delay to compute the threshold. I still don't know
				if it is the best guess */
				sync_threshold = FFMAX(AV_SYNC_THRESHOLD, delay);
				if (fabs(diff) < AV_NOSYNC_THRESHOLD) {
					if (diff <= -sync_threshold)
						delay = 0;
					else if (diff >= sync_threshold)
						delay = 2 * delay;
				}
		}
		frame_timer += delay;

		return frame_timer;
	}

	void audio_request(unsigned int req_len)
	{
		int len, audio_size = 0;
		double pts;

		while (req_len > 0) {
			if (audio_buf_index >= audio_buf_size) {
				/* We have already sent all our data; get more */
				audio_size = audio_decode_frame(&pts);
				if (audio_size < 0) {
					/* If error, output silence */
					audio_buf_size = 1024;
					audio_buf = NULL;
				} else {
					audio_size = synchronize_audio(audio_buf, audio_size);
					audio_buf_size = audio_size;
				}
				audio_buf_index = 0;
			}

			len = audio_buf_size - audio_buf_index;
			if (len > (int)req_len)
				len = req_len;

			if (audio_buf && len > 0)
				audio_buf_index += pOuter->m_pHostIface->QueueAudioData(audio_buf + audio_buf_index, len);

			req_len -= len;
		}
	}
private:
	int stream_component_open(int stream_index);

	/* Add or subtract samples to get a better sync, return new
	audio buffer size */
	int synchronize_audio(uint8_t *samples, int samples_size)
	{
		int n;
		double ref_clock;

		n = FFmpeg::av_get_bytes_per_sample(out_audio_fmt) * audio_ctx->ch_layout.nb_channels;

		if (av_sync_type != AV_SYNC_AUDIO_MASTER) {
			double diff, avg_diff;
			int wanted_size, min_size, max_size;

			ref_clock = get_master_clock();
			diff = get_audio_clock() - ref_clock;
			if (diff < AV_NOSYNC_THRESHOLD) {
				// accumulate the diffs
				audio_diff_cum = diff + audio_diff_avg_coef
					* audio_diff_cum;
				if (audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
					audio_diff_avg_count++;
				} else {
					avg_diff = audio_diff_cum * (1.0 - audio_diff_avg_coef);
					if (fabs(avg_diff) >= audio_diff_threshold) {
						wanted_size = samples_size + ((int)(diff * audio_ctx->sample_rate) * n);
						min_size = samples_size * ((100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100);
						max_size = samples_size * ((100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100);
						if (wanted_size < min_size) {
							wanted_size = min_size;
						} else if (wanted_size > max_size) {
							wanted_size = max_size;
						}
						if (wanted_size < samples_size) {
							/* remove samples */
							samples_size = wanted_size;
						} else if (wanted_size > samples_size) {
							uint8_t *samples_end, *q;
							int nb;
							/* add samples by copying final sample*/
							nb = (samples_size - wanted_size);
							samples_end = samples + samples_size - n;
							q = samples_end + n;
							while (nb > 0) {
								memcpy(q, samples_end, n);
								q += n;
								nb -= n;
							}
							samples_size = wanted_size;
						}
					}
				}
			} else {
				/* difference is TOO big; reset diff stuff */
				audio_diff_avg_count = 0;
				audio_diff_cum = 0;
			}
		}
		return samples_size;
	}

	int audio_decode_frame(double *pts_ptr)
	{
		int data_size, n, got_frame, sent_packet = -1, status, failed_frames = 0;
		double pts;

		for (;;) {

			while (!sent_packet) {
				got_frame = 0;
				status = FFmpeg::avcodec_receive_frame(audio_ctx, audio_frame);
				if (status == 0)
					got_frame = 1;

#ifdef SIMULATE_SLOW_CPU
				Sleep(AUDIO_DECODE_STALL);
#endif

				if (status == 0 || status == AVERROR(EAGAIN)) {
					status = FFmpeg::avcodec_send_packet(audio_ctx, audio_pkt);
					if (status >= 0 || status == AVERROR(EAGAIN))
						sent_packet = 1;
					else
						break;
				} else {
					/* if error, skip frame */
					// try to avoid inifinite loop if something goes wrong
					if (++failed_frames > 512)
						return -1;
					break;
				}

				if (!got_frame) {
					/* No data yet, get more frames */
					continue;
				}

				if (audio_ctx->sample_fmt != out_audio_fmt && !avr_context) {
					AVChannelLayout src_channel_layout = audio_frame->ch_layout;
					AVChannelLayout dst_channel_layout;

					if (!FFmpeg::av_channel_layout_check(&src_channel_layout))
						FFmpeg::av_channel_layout_default(&src_channel_layout, audio_frame->ch_layout.nb_channels);
					FFmpeg::av_channel_layout_default(&dst_channel_layout, out_audio_nb_ch);

					if (FFmpeg::swr_alloc_set_opts2(&avr_context,
						&dst_channel_layout, out_audio_fmt, audio_frame->sample_rate,
						&src_channel_layout, audio_ctx->sample_fmt, audio_frame->sample_rate,
						0, NULL) == 0) {
						if (FFmpeg::swr_init(avr_context) < 0)
							FFmpeg::swr_free(&avr_context);
					}
				}

				if (avr_context) {
					int sample_size = FFmpeg::av_get_bytes_per_sample(out_audio_fmt) * out_audio_nb_ch; // 8/16/32 bit, mono/stereo
					int sample_count = audio_frame->nb_samples;
					data_size = sample_size * audio_frame->nb_samples;

					avr_buffer = (uint8_t *) FFmpeg::av_malloc(data_size);
					if (!avr_buffer)
						return -1;

					audio_buf = avr_buffer;
					uint8_t* lines[] = { audio_buf };

					if (sample_count != FFmpeg::swr_convert(avr_context, lines, sample_count,
						(const uint8_t **) audio_frame->data, sample_count))
						data_size = 0;
				} else {
					data_size = FFmpeg::av_samples_get_buffer_size(NULL, audio_frame->ch_layout.nb_channels,
						audio_frame->nb_samples, audio_ctx->sample_fmt, 1);
					audio_buf = audio_frame->data[0];
				}

				pts = audio_clock;
				*pts_ptr = pts;
				n = FFmpeg::av_get_bytes_per_sample(out_audio_fmt) * audio_ctx->ch_layout.nb_channels;
				audio_clock += (double)data_size / (double)(n * audio_ctx->sample_rate);

				/* We have data, return it and come back for more later */
				return data_size;
			}

			/* free the current packet */
			FFmpeg::av_packet_unref(audio_pkt);
			sent_packet = 0;

			if (quit) {
				return -1;
			}

			/* read next packet */
			if (audioq.Get(audio_pkt) < 0) {
				return -1;
			}

			/* if update, update the audio clock w/pts */
			if (audio_pkt->pts != AV_NOPTS_VALUE) {
				audio_clock = av_q2d(audio_ctx->time_base)*audio_pkt->pts;
			}
		}
	}
};


///////////////////////////////////////////////////////////////////////////////
//
// DecodeThread
//

class DecodeThread : public cWorkerThread
{
public:
	DecodeThread(VideoState *is) : is(is) { };

protected:
	virtual DWORD ThreadProc()
	{
		AVPacket *pkt;
		int eof = 0;

		pkt = FFmpeg::av_packet_alloc();
		if (!pkt) {
			is->quit = 1;
			is->DecodeFinished();
			return -1;
		}

		// main decode loop
		for (;;) {
			if (is->quit) {
				break;
			}

			// if the queue are full, no need to read more
			if ((is->audioq.size > MIN_AUDIOQ_SIZE || is->audioStream < 0)
				&& (is->videoq.nb_packets > MIN_FRAMES || is->videoStream < 0)) {
					Sleep(10);
					continue;
			}

			if (eof) {
				// wait for queues to run empty
				Sleep(10);
				if(is->audioq.size + is->videoq.size == 0) {
					break;
				}
				continue;
			}

			if (FFmpeg::av_read_frame(is->pFormatCtx, pkt) < 0) {
				eof = 1;
				// signal queues that nothing more is coming
				is->videoq.Depleted();
				is->audioq.Depleted();
				continue;
			}
			// Is this a packet from the video stream?
			if (pkt->stream_index == is->videoStream) {
				is->videoq.Put(pkt);
			} else if (pkt->stream_index == is->audioStream) {
				is->audioq.Put(pkt);
			} else {
				FFmpeg::av_packet_unref(pkt);
			}
		}

		is->quit = 1;
		is->DecodeFinished();

		FFmpeg::av_packet_free(&pkt);

		return 0;
	}

private:
	VideoState *is;
};

#ifdef _WIN32
#pragma pack()
#endif


static uint64_t global_video_pkt_pts = AV_NOPTS_VALUE;

///////////////////////////////////////////////////////////////////////////////
//
// VideoThread
//

class VideoThread : public cWorkerThread
{
public:
	VideoThread(VideoState *is) : is(is) { }

protected:
	virtual DWORD ThreadProc()
	{
		AVPacket *pkt;
		AVFrame *pFrame;
		int status;
		double pts;
		int64_t pts_int;

		pkt = FFmpeg::av_packet_alloc();
		pFrame = FFmpeg::av_frame_alloc();
		if (!pkt || !pFrame) {
			if (pkt)
				FFmpeg::av_packet_free(&pkt);
			else if (pFrame)
				FFmpeg::av_frame_free(&pFrame);
			return -1;
		}

		for (;;) {
			if (is->videoq.Get(pkt) < 0) {
				// means we quit getting packets
				break;
			}

			pts_int = 0;

			// Save global pts to be stored in pFrame
			global_video_pkt_pts = pkt->pts;
			// Send video packet
			status = FFmpeg::avcodec_send_packet(is->video_ctx, pkt);

			while (status >= 0) {
				// Receive video frame
				status = FFmpeg::avcodec_receive_frame(is->video_ctx, pFrame);

#ifdef SIMULATE_SLOW_CPU
				Sleep(VIDEO_DECODE_STALL);
#endif

				// Did we get a video frame?
				if (status >= 0) {
					pts_int = pFrame->best_effort_timestamp;

					if (pts_int == AV_NOPTS_VALUE) {
						pts_int = 0;
					}
					pts = (double)pts_int * av_q2d(is->video_ctx->time_base);

					pts = synchronize_video(pFrame, pts);

					is->skip_frames_index += 1;
					if (is->skip_frames_index >= is->skip_frames) {
						is->skip_frames_index -= FFMAX(is->skip_frames, 1.0f);

						if (queue_picture(pFrame, pts, pkt->pos) < 0) {
							status = -1;
							break;
						}
					} else {
						Warning(("ffmpeg: dropped decoded frame sfi:%g sf:%g pts:%g pktpts:%g", is->skip_frames_index, is->skip_frames, (double)pts, pkt->pts));
					}
				}
			}
			FFmpeg::av_packet_unref(pkt);
		}
		FFmpeg::av_packet_free(&pkt);
		FFmpeg::av_frame_free(&pFrame);

		return 0;
	}

private:
	double synchronize_video(AVFrame *src_frame, double pts)
	{
		double frame_delay;

		if (pts != 0) {
			/* if we have pts, set video clock to it */
			is->video_clock = pts;
		} else {
			/* if we aren't given a pts, set it to the clock */
			pts = is->video_clock;
		}
		/* update the video clock */
		frame_delay = av_q2d(is->video_ctx->time_base);
		/* if we are repeating a frame, adjust clock accordingly */
		frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
		is->video_clock += frame_delay;
		return pts;
	}

	int queue_picture(AVFrame *pFrame, double pts, int64_t pos)
	{
		VideoState::VideoPicture *vp;

		// wait until we have space for a new pic
		is->pictq_mutex.Wait();
		if (is->pictq_size>=VIDEO_PICTURE_QUEUE_SIZE && !is->refresh)
			is->skip_frames= FFMAX(1.0f - FRAME_SKIP_FACTOR, is->skip_frames * (1.0f-FRAME_SKIP_FACTOR));
		while (is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE && !is->quit) {
			SDL_CondWait(is->pictq_cond, is->pictq_mutex);
		}
		is->pictq_mutex.Release();

		if (is->quit)
			return -1;

		// windex is set to 0 initially
		vp = &is->pictq[is->pictq_windex];

		ILGVideoDecoderHost::sLockResult lock;
		if ( !is->pOuter->m_pHostIface->LockBuffer(vp->bmp, lock) )
			return -1;

		ILGVideoDecoderHost::sFrameFormat fmt;
		is->pOuter->m_pHostIface->GetFrameFormat(fmt);

		uint8_t *data[] = { (uint8_t*)lock.buffer, NULL, NULL };
		int stride[] = {lock.pitch, 0, 0};

		is->img_convert_ctx = FFmpeg::sws_getCachedContext(is->img_convert_ctx,
			is->video_ctx->width, is->video_ctx->height, is->video_ctx->pix_fmt,
			fmt.width, fmt.height, is->pict_pix_fmt,
			sws_flags, NULL, NULL, NULL);

		FFmpeg::sws_scale(is->img_convert_ctx, pFrame->data,
			pFrame->linesize, 0,
			is->video_ctx->height, data, stride);

		is->pOuter->m_pHostIface->UnlockBuffer(vp->bmp);

		vp->pts = pts;
		vp->pos = pos;

		// now we inform our display thread that we have a pic ready
		if (++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
			is->pictq_windex = 0;
		}
		is->pictq_mutex.Wait();
		vp->target_clock = is->compute_target_time(vp->pts);
		is->pictq_size++;
		is->pictq_mutex.Release();

		return 0;
	}

	VideoState *is;
};


///////////////////////////////////////////////////////////////////////////////
//
// VideoState non-inline functions
//

BOOL VideoState::Play()
{
	videoq.Finished(0);
	audioq.Finished(0);
	videoq.Quit(0);
	audioq.Quit(0);
	quit = 0;

	global_video_pkt_pts = AV_NOPTS_VALUE;

	audio_clock = 0;
	audio_diff_cum = 0;
	frame_last_pts = 0;
	video_clock = 0;
	video_current_pts = 0;
	pictq_size = 0;
	pictq_rindex = 0;
	pictq_windex = 0;

	audio_buf_size = 0;
	audio_buf_index = 0;

	/* averaging filter for audio sync */
	audio_diff_avg_coef = exp(log(0.01 / AUDIO_DIFF_AVG_NB));
	audio_diff_avg_count = 0;
	/* Correct audio only if larger error than this */
	if (audio_ctx)
		audio_diff_threshold = 2.0 * AUDIO_BUFFER_SIZE / (double)audio_ctx->sample_rate;
	else
		audio_diff_threshold = 0;

	FFmpeg::av_packet_unref(audio_pkt);

	const int64_t curtime = FFmpeg::av_gettime();

	frame_timer = (double)curtime / 1000000.0;
	frame_last_delay = 40e-3;
	video_current_pts_drift = 0;
	video_current_pos = 0;
	video_current_pts_time = curtime;

	external_clock_time = curtime;
	external_clock = 0;
	skip_frames = 0;
	skip_frames_index = 0;

	Assert_(!video_tid);
	video_tid = new (std::nothrow) VideoThread(this);
	if (video_tid && !video_tid->Create()) {
		AssertMsg(FALSE, "VideoThread::Create");
	}

	Assert_(!parse_tid);
	parse_tid = new (std::nothrow) DecodeThread(this);
	if (parse_tid && !parse_tid->Create()) {
		AssertMsg(FALSE, "DecodeThread::Create");
	}

	return (parse_tid != 0);
}

int VideoState::stream_component_open(int stream_index)
{
	const AVCodec *codec;
	AVCodecContext *codecCtx;

	if (stream_index < 0 || stream_index >= (int)pFormatCtx->nb_streams) {
		return -1;
	}

	codec = FFmpeg::avcodec_find_decoder(pFormatCtx->streams[stream_index]->codecpar->codec_id);
	if (!codec) {
		return -1;
	}

	// Get a pointer to the codec context for the video stream
	codecCtx = FFmpeg::avcodec_alloc_context3(codec);
	if (!codecCtx || FFmpeg::avcodec_parameters_to_context(codecCtx, pFormatCtx->streams[stream_index]->codecpar) < 0) {
		return -1;
	}

#ifdef DEBUG
	codecCtx->debug = FF_DEBUG_BUGS|FF_DEBUG_ER/*|FF_DEBUG_SKIP|FF_DEBUG_PICT_INFO*/;
#endif

	if (FFmpeg::avcodec_open2(codecCtx, codec, NULL) < 0) {
		AssertMsg(FALSE, "Unsupported codec!\n");
		return -1;
	}

	pFormatCtx->streams[stream_index]->discard = AVDISCARD_DEFAULT;

	switch(codecCtx->codec_type)
	{
	case AVMEDIA_TYPE_AUDIO:
		{
			if ( !pOuter->m_pHostIface->CreateAudioBuffer(codecCtx->sample_rate, out_audio_nb_ch, AUDIO_BUFFER_SIZE) )
				break;

			audioStream = stream_index;
			audio_ctx = codecCtx;
			audio_ctx->time_base = pFormatCtx->streams[stream_index]->time_base;

			FFmpeg::av_packet_unref(audio_pkt);
		}
		break;

	case AVMEDIA_TYPE_VIDEO:
		videoStream = stream_index;
		video_ctx = codecCtx;
		video_ctx->time_base = pFormatCtx->streams[stream_index]->time_base;
		break;

	default:
		break;
	}

	return 0;
}


void VideoState::Stop()
{
	quit = 1;

	videoq.Quit();
	audioq.Quit();

	SDL_CondSignal(pictq_cond);

	if (video_tid) {
		video_tid->WaitForClose();
		delete video_tid;
		video_tid = NULL;
	}

	if (parse_tid) {
		parse_tid->WaitForClose();
		delete parse_tid;
		parse_tid = NULL;
	}
}


///////////////////////////////////////////////////////////////////////////////
//
// cLGVideoDecoder implementaion
//

cLGVideoDecoder::cLGVideoDecoder(ILGVideoDecoderHost *pHostIface)
	: m_pHostIface(pHostIface),
	is(NULL)
{
}

cLGVideoDecoder::~cLGVideoDecoder()
{
}

STDMETHODIMP_(void) cLGVideoDecoder::Destroy()
{
	Stop();

	delete this;
}

STDMETHODIMP_(BOOL) cLGVideoDecoder::Start()
{
	if (!is)
		return FALSE;

	is->av_sync_type = VideoState::DEFAULT_AV_SYNC_TYPE;

	return is->Play();
}

STDMETHODIMP_(BOOL) cLGVideoDecoder::IsFinished()
{
	return is ? is->quit : TRUE;
}

STDMETHODIMP_(BOOL) cLGVideoDecoder::IsVideoFrameAvailable()
{
	is->refresh = 1;

	if (is->video_ctx && is->pictq_size)
	{
		// TODO: should we check if it's still to early to display frame and return FALSE?
		return TRUE;
	}

	return FALSE;
}

STDMETHODIMP_(void) cLGVideoDecoder::RequestVideoFrame()
{
	if (is && is->refresh)
	{
		is->video_refresh_timer();
		is->refresh = 0;
	}
}

STDMETHODIMP_(void) cLGVideoDecoder::RequestAudio(unsigned int len)
{
	if (is)
		is->audio_request(len);
}

STDMETHODIMP_(size_t) cLGVideoDecoder::GetCurrentPlaybackTime()
{
	if (is)
		return (size_t)(is->get_master_clock() * 1000.0);

	return 0;
}

//

void cLGVideoDecoder::Stop()
{
	if (is)
	{
		is->Stop();
		is->Close();

		delete is;
		is = NULL;
	}

	FFmpeg::Shutdown();
}

BOOL cLGVideoDecoder::Init(const char *filename)
{
	char buf[16] = {0,};

	sws_flags = SWS_BICUBIC;
	if ( m_pHostIface->GetConfigValue("movie_sw_scale_quality", buf, sizeof(buf)) )
	{
		sws_flags = atoi(buf);

		if (sws_flags < 0)
			sws_flags = 0;
		else if (sws_flags > 6)
			sws_flags = 6;

		switch (sws_flags)
		{
		case 0: sws_flags = SWS_POINT; break;
		case 1: sws_flags = SWS_FAST_BILINEAR; break;
		case 2: sws_flags = SWS_BILINEAR; break;
		case 3: sws_flags = SWS_BICUBLIN; break;
		case 4: sws_flags = SWS_BICUBIC; break;
		case 5: sws_flags = SWS_SPLINE; break;
		case 6: sws_flags = SWS_SINC; break;
		default:
			sws_flags = SWS_BICUBIC;
		}
	}

	out_audio_fmt = AV_SAMPLE_FMT_S16;
	memset(buf, 0, sizeof(buf));
	if ( m_pHostIface->GetConfigValue("movie_max_sample_depth", buf, sizeof(buf)) )
	{
		switch (atoi(buf))
		{
		case 8:  out_audio_fmt = AV_SAMPLE_FMT_U8; break;
		case 16: out_audio_fmt = AV_SAMPLE_FMT_S16; break;
		case 24:
		case 32: out_audio_fmt = AV_SAMPLE_FMT_S32; break;
		default:
			out_audio_fmt = AV_SAMPLE_FMT_S16;
		}
	}

	out_audio_nb_ch = 2;
	memset(buf, 0, sizeof(buf));
	if ( m_pHostIface->GetConfigValue("movie_channels", buf, sizeof(buf)) )
	{
		switch (atoi(buf))
		{
		case 1: out_audio_nb_ch = 1; break;
		case 2:
		default:
			out_audio_nb_ch = 2;
		}
	}

	if ( !FFmpeg::Init(this) )
		return FALSE;

	is = new (std::nothrow) VideoState(this);
	if (!is)
		return FALSE;

	//

	ILGVideoDecoderHost::sFrameFormat fmt;
	m_pHostIface->GetFrameFormat(fmt);

	AVPixelFormat pixformat = AV_PIX_FMT_NONE;

	if (fmt.bpp == 32)
	{
		if (fmt.gmask == 0xFF0000) // && bmask.alpha == 0xFF
		{
			if (fmt.rmask == 0xFF00  && fmt.bmask == 0xFF000000)
				pixformat = AV_PIX_FMT_ARGB;
			else if (fmt.rmask == 0xFF000000 && fmt.bmask == 0xFF00)
				pixformat = AV_PIX_FMT_ABGR;
		}
		else if (fmt.gmask == 0xFF00) // && bmask.alpha == 0xFF000000
		{
			if (fmt.rmask == 0xFF && fmt.bmask == 0xFF0000 )
				pixformat = AV_PIX_FMT_RGBA;
			else if (fmt.rmask == 0xFF0000 && fmt.bmask == 0xFF)
				pixformat = AV_PIX_FMT_BGRA;
		}
	}
	else if (fmt.bpp == 16)
	{
		if (fmt.gmask == 0x7E0)
		{
			if (fmt.bmask == 0x1F)
				pixformat = AV_PIX_FMT_RGB565LE;
			else
				pixformat = AV_PIX_FMT_BGR565LE;
		}
	}

	if (pixformat == AV_PIX_FMT_NONE)
	{
		// failed to find suitable pixel format (probably in 8-bit mode)
		Assert_(pixformat != AV_PIX_FMT_NONE);
		Stop();
		return FALSE;
	}

	if (!is->Open(filename, pixformat))
	{
		Stop();
		return FALSE;
	}

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////
//
// DLL interface
//

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif
#ifndef __cdecl
#define __cdecl
#endif

extern "C" DLLEXPORT ILGVideoDecoder2* __cdecl CreateLGVideoDecoder2(ILGVideoDecoderHost *pHostIface, const char *filename)
{
	if (!pHostIface || !filename)
		return NULL;

	cLGVideoDecoder *p = new (std::nothrow) cLGVideoDecoder(pHostIface);

	if (p && !p->Init(filename))
	{
		delete p;
		return NULL;
	}

	return p;
}

extern "C" DLLEXPORT ILGVideoDecoder* __cdecl CreateLGVideoDecoder(ILGVideoDecoderHost *pHostIface, const char *filename)
{
	return CreateLGVideoDecoder2(pHostIface, filename);
}
