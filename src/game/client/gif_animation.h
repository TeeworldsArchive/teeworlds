/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_GIF_ANIMATION_H
#define GAME_CLIENT_GIF_ANIMATION_H

#include <base/system/lock.h>
#include <base/system/semaphore.h>
#include <base/tl/array.h>

#include <engine/graphics.h>

class CGifAnimation;
class CGifFrame;
class CGifJob;
class CGifStream;

/*
	Decodes the frames of animated avatars in the background.

	Compositing a frame walks the canvas pixel by pixel and copies it, and the
	friends list draws a dozen animations at once, so decoding while the menu
	is rendered shows up as a hitch. One worker thread decodes the frames of
	all avatars ahead of their playback; the main thread only uploads the
	finished frames, which it has to do itself because it creates the textures.

	Only available when the client was built with giflib (CONF_GIFLIB).
*/
class CGifDecoder
{
public:
	CGifDecoder();
	~CGifDecoder();

	// Decodes the stream from the given frame on; ownership moves here.
	void Start(CGifAnimation *pAnimation, CGifStream *pStream, int Width, int Height, int FirstFrame);
	// Stops decoding for the animation and drops its stream.
	void Stop(CGifAnimation *pAnimation);
	// Wakes the worker after room for another frame was made.
	void Notify();

private:
	friend class CGifAnimation;

	// Next decoded frame, or 0 while none is ready; the caller owns the frame.
	CGifFrame *PopFrame(CGifAnimation *pAnimation);
	CGifJob *FindJob(CGifAnimation *pAnimation);

	array<CGifJob *> m_lJobs;
	void *m_pThread;
	LOCK m_Lock;
	SEMAPHORE m_Semaphore;
	volatile bool m_Shutdown;

	static void ThreadEntry(void *pUser);
	void Run();
};

/*
	Plays a GIF as a rolling window of textures.

	Steam's animated avatars can have hundreds of frames, and keeping all of
	them as textures would cost tens of megabytes per friend. The compressed
	file is kept instead, the frames are decoded as they are played and only a
	small window of textures around the current frame is resident, so long
	animations are streamed like a video and repeating them is cheap.

	The frames themselves are composited by CGifDecoder; the textures are
	created and uploaded here, on the main thread.

	Only available when the client was built with giflib (CONF_GIFLIB).
*/
class CGifAnimation
{
public:
	explicit CGifAnimation(CGifDecoder *pDecoder = 0);
	~CGifAnimation();

	// owns textures and a decoder, copying one would double free both
	CGifAnimation(const CGifAnimation &) = delete;
	CGifAnimation &operator=(const CGifAnimation &) = delete;

	void Reset(IGraphics *pGraphics);
	bool LoadRGBA(IGraphics *pGraphics, int Width, int Height, const unsigned char *pData);
	bool LoadGIF(IGraphics *pGraphics, const unsigned char *pData, int DataSize);

	// Advances the playback; call once per frame while the image is visible.
	// Frames decoded in the background are uploaded from here.
	void Update(IGraphics *pGraphics, float Seconds);
	IGraphics::CTextureHandle Texture() const;

	int Width() const { return m_Width; }
	int Height() const { return m_Height; }
	bool IsValid() const { return m_lTextures.size() > 0; }
	bool IsAnimated() const { return m_bAnimated; }

private:
	CGifDecoder *m_pDecoder;

	// ring of textures around the frame being played
	array<IGraphics::CTextureHandle> m_lTextures;
	array<int> m_lDelaysMs;
	// absolute frame each slot holds, -1 while unused; the frame number keeps
	// counting across loops so a slot can tell an old frame from a new one
	array<int> m_lFrameIndices;

	int m_Width;
	int m_Height;
	int m_CurrentFrame;
	int m_DecodedFrame;
	bool m_bAnimated;
	float m_FrameTime;

	bool UploadDecodedFrames(IGraphics *pGraphics);
	void UploadTexture(IGraphics *pGraphics, int Frame, int DelayMs, const unsigned char *pData);
};

#endif
