/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#include "gif_animation.h"

#include <base/math.h>
#include <base/system/debug.h>
#include <base/system/mem.h>
#include <base/system/thread.h>

// textures kept around the frame being played; the frames beyond that are
// decoded again when the animation loops
static const int GIF_TEXTURE_RING = 16;

#if defined(CONF_GIFLIB)
#include <gif_lib.h>

// avatars are drawn at a few dozen pixels, so bigger images only waste
// memory and decoding time; Steam's animated avatars are 184x184
static const int MAX_GIF_SIZE = 512;
// a broken file must not keep the decoder busy forever
static const int MAX_GIF_FRAMES = 4096;
// frames decoded ahead of the one being played, so a slow frame does not
// show up as a hitch
static const int GIF_PREFETCH = 4;

/*
	Decodes one GIF frame at a time from the compressed file.

	giflib can hand out the frames incrementally, which is what keeps long
	animations cheap: the canvas of the animation is composited frame by
	frame and only that canvas is moved around. The file itself is copied,
	because the engine drops its download once the menus no longer draw the
	friend.
*/
class CGifStream
{
public:
	CGifStream(const unsigned char *pData, int DataSize);
	~CGifStream();

	bool IsOk() const { return m_pGif != 0; }
	int Width() const { return m_Width; }
	int Height() const { return m_Height; }

	// Starts the animation over; the canvas and the disposal state are reset.
	bool Rewind();

	// Composite of the next frame in the canvas, or 0 at the end of the file.
	const unsigned char *NextFrame(int *pDelayMs);

private:
	static int ReadCallback(GifFileType *pFile, GifByteType *pBuffer, int Size);

	bool Open();
	void Close();
	void ResetCanvas();

	array<unsigned char> m_lData;
	int m_ReadPos;
	GifFileType *m_pGif;
	int m_Width;
	int m_Height;
	int m_Frames;

	unsigned char *m_pCanvas;
	// the canvas goes through its disposal after a frame was drawn, so the
	// frame handed out is a copy taken before that
	unsigned char *m_pFrame;
	unsigned char *m_pPrevious;
	int m_CanvasSize;

	// state of the frame about to be drawn, from its graphics control block
	int m_DisposalMode;
	int m_TransparentColor;
	int m_DelayMs;

	array<int> m_lRowMap;
	array<unsigned char> m_lLine;
};

CGifStream::CGifStream(const unsigned char *pData, int DataSize)
{
	m_ReadPos = 0;
	m_pGif = 0;
	m_Width = 0;
	m_Height = 0;
	m_Frames = 0;
	m_pCanvas = 0;
	m_pFrame = 0;
	m_pPrevious = 0;
	m_CanvasSize = 0;
	m_DisposalMode = DISPOSE_DO_NOT;
	m_TransparentColor = NO_TRANSPARENT_COLOR;
	// a file without a graphics control block uses the browser default
	m_DelayMs = 0;

	m_lData.set_size(DataSize);
	mem_copy(m_lData.base_ptr(), pData, DataSize);
	Open();
}

CGifStream::~CGifStream()
{
	Close();
	mem_free(m_pCanvas);
	m_pCanvas = 0;
	mem_free(m_pFrame);
	m_pFrame = 0;
	mem_free(m_pPrevious);
	m_pPrevious = 0;
}

int CGifStream::ReadCallback(GifFileType *pFile, GifByteType *pBuffer, int Size)
{
	CGifStream *pStream = static_cast<CGifStream *>(pFile->UserData);
	if(!pStream || Size <= 0)
		return 0;

	const int Available = pStream->m_lData.size() - pStream->m_ReadPos;
	if(Available <= 0)
		return 0;
	if(Size > Available)
		Size = Available;
	mem_copy(pBuffer, pStream->m_lData.base_ptr() + pStream->m_ReadPos, Size);
	pStream->m_ReadPos += Size;
	return Size;
}

bool CGifStream::Open()
{
	Close();
	m_ReadPos = 0;
	m_Frames = 0;

	int Error = 0;
	m_pGif = DGifOpen(this, ReadCallback, &Error);
	if(!m_pGif)
	{
		dbg_msg("gif", "failed to open gif: %s", GifErrorString(Error));
		return false;
	}

	m_Width = m_pGif->SWidth;
	m_Height = m_pGif->SHeight;
	if(m_Width <= 0 || m_Height <= 0 || m_Width > MAX_GIF_SIZE || m_Height > MAX_GIF_SIZE)
	{
		dbg_msg("gif", "unsupported gif size: %dx%d", m_Width, m_Height);
		Close();
		return false;
	}

	m_CanvasSize = m_Width * m_Height * 4;
	if(!m_pCanvas)
		m_pCanvas = static_cast<unsigned char *>(mem_alloc(m_CanvasSize));
	if(!m_pFrame)
		m_pFrame = static_cast<unsigned char *>(mem_alloc(m_CanvasSize));
	if(!m_pCanvas || !m_pFrame)
	{
		Close();
		return false;
	}
	ResetCanvas();
	return true;
}

void CGifStream::Close()
{
	if(m_pGif)
	{
		int Error = 0;
		DGifCloseFile(m_pGif, &Error);
	}
	m_pGif = 0;
}

void CGifStream::ResetCanvas()
{
	mem_zero(m_pCanvas, m_CanvasSize);
	m_DisposalMode = DISPOSE_DO_NOT;
	m_TransparentColor = NO_TRANSPARENT_COLOR;
	m_DelayMs = 0;
}

bool CGifStream::Rewind()
{
	if(!m_lData.size())
		return false;
	if(!Open())
		return false;
	return true;
}

const unsigned char *CGifStream::NextFrame(int *pDelayMs)
{
	if(!m_pGif)
		return 0;

	// a file this long is broken rather than an animation
	if(m_Frames >= MAX_GIF_FRAMES)
		return 0;

	for(;;)
	{
		GifRecordType Type;
		if(DGifGetRecordType(m_pGif, &Type) == GIF_ERROR)
		{
			dbg_msg("gif", "failed to read gif: %s", GifErrorString(m_pGif->Error));
			return 0;
		}

		if(Type == TERMINATE_RECORD_TYPE)
			return 0;

		if(Type == EXTENSION_RECORD_TYPE)
		{
			int ExtCode = 0;
			GifByteType *pExt = 0;
			if(DGifGetExtension(m_pGif, &ExtCode, &pExt) == GIF_ERROR)
				return 0;

			// the graphics control block carries delay, transparency and how
			// the canvas is restored after this frame
			if(pExt && ExtCode == GRAPHICS_EXT_FUNC_CODE && pExt[0] >= 4)
			{
				m_DisposalMode = (pExt[1] >> 2) & 0x7;
				m_TransparentColor = (pExt[1] & 0x1) ? pExt[4] : NO_TRANSPARENT_COLOR;
				m_DelayMs = pExt[2] | (pExt[3] << 8);
			}

			while(pExt)
			{
				if(DGifGetExtensionNext(m_pGif, &pExt) == GIF_ERROR)
					return 0;
			}
			continue;
		}

		if(Type != IMAGE_DESC_RECORD_TYPE)
			continue;

		if(DGifGetImageDesc(m_pGif) == GIF_ERROR)
		{
			dbg_msg("gif", "failed to read gif frame: %s", GifErrorString(m_pGif->Error));
			return 0;
		}

		const GifImageDesc &Desc = m_pGif->Image;
		const ColorMapObject *pColorMap = Desc.ColorMap ? Desc.ColorMap : m_pGif->SColorMap;
		if(!pColorMap || Desc.Width <= 0 || Desc.Height <= 0)
			return 0;

		// DISPOSE_PREVIOUS restores the canvas from before this frame
		if(m_DisposalMode == DISPOSE_PREVIOUS)
		{
			if(!m_pPrevious)
				m_pPrevious = static_cast<unsigned char *>(mem_alloc(m_CanvasSize));
			if(m_pPrevious)
				mem_copy(m_pPrevious, m_pCanvas, m_CanvasSize);
		}

		// interlaced gifs store their rows in four passes instead of top down
		m_lRowMap.set_size(Desc.Height);
		if(Desc.Interlace)
		{
			int Row = 0;
			for(int Pass = 0; Pass < 4; Pass++)
			{
				const int Start = Pass == 0 ? 0 : Pass == 1 ? 4 :
							  Pass == 2         ? 2 :
									      1;
				const int Step = Pass <= 1 ? 8 : Pass == 2 ? 4 :
									     2;
				for(int y = Start; y < Desc.Height; y += Step)
					m_lRowMap[Row++] = y;
			}
		}
		else
		{
			for(int y = 0; y < Desc.Height; y++)
				m_lRowMap[y] = y;
		}

		m_lLine.set_size(Desc.Width);
		for(int Row = 0; Row < Desc.Height; Row++)
		{
			if(DGifGetLine(m_pGif, m_lLine.base_ptr(), Desc.Width) == GIF_ERROR)
				return 0;

			const int DstY = Desc.Top + m_lRowMap[Row];
			if(DstY < 0 || DstY >= m_Height)
				continue;

			for(int x = 0; x < Desc.Width; x++)
			{
				const int DstX = Desc.Left + x;
				if(DstX < 0 || DstX >= m_Width)
					continue;

				const int ColorIndex = m_lLine[x];
				if(m_TransparentColor != NO_TRANSPARENT_COLOR && ColorIndex == m_TransparentColor)
					continue;
				if(ColorIndex < 0 || ColorIndex >= pColorMap->ColorCount)
					continue;

				const GifColorType &Color = pColorMap->Colors[ColorIndex];
				unsigned char *pDst = m_pCanvas + ((size_t) DstY * m_Width + DstX) * 4;
				pDst[0] = Color.Red;
				pDst[1] = Color.Green;
				pDst[2] = Color.Blue;
				pDst[3] = 255;
			}
		}

		m_Frames++;
		m_DelayMs = m_DelayMs * 10;
		// A delay of zero would spin through the frames; browsers use 100 ms.
		if(m_DelayMs <= 0)
			m_DelayMs = 100;
		*pDelayMs = m_DelayMs;

		// The frame behind the canvas is what gets drawn; the canvas itself
		// is then prepared for the next frame with its disposal.
		mem_copy(m_pFrame, m_pCanvas, m_CanvasSize);

		if(m_DisposalMode == DISPOSE_BACKGROUND)
		{
			for(int y = maximum(0, Desc.Top); y < minimum(m_Height, Desc.Top + Desc.Height); y++)
			{
				for(int x = maximum(0, Desc.Left); x < minimum(m_Width, Desc.Left + Desc.Width); x++)
					mem_zero(m_pCanvas + ((size_t) y * m_Width + x) * 4, 4);
			}
		}
		else if(m_DisposalMode == DISPOSE_PREVIOUS && m_pPrevious)
			mem_copy(m_pCanvas, m_pPrevious, m_CanvasSize);

		m_DisposalMode = DISPOSE_DO_NOT;
		m_TransparentColor = NO_TRANSPARENT_COLOR;
		m_DelayMs = 0;

		return m_pFrame;
	}
}

/*
	A frame that was composed and waits for the menu to upload it. It only
	lives between the decoder and the menu, nothing else owns it.
*/
class CGifFrame
{
public:
	int m_Frame;
	int m_DelayMs;
	array<unsigned char> m_lData;
};

/*
	One stream the worker decodes. The fields are guarded by CGifDecoder::m_Lock;
	composing a frame happens without the lock so that the menu never waits for
	the worker.
*/
class CGifJob
{
public:
	CGifAnimation *m_pAnimation;
	CGifStream *m_pStream;
	int m_Width;
	int m_Height;
	int m_FirstFrame;
	int m_NextFrame;
	array<CGifFrame *> m_lFrames;
	bool m_bBusy;
	bool m_bStopped;
	bool m_bBroken;

	CGifJob() :
		m_pAnimation(0), m_pStream(0), m_Width(0), m_Height(0), m_FirstFrame(0), m_NextFrame(0), m_bBusy(false), m_bStopped(false), m_bBroken(false) {}

	~CGifJob()
	{
		delete m_pStream;
		m_lFrames.delete_all();
	}

	CGifFrame *DecodeNextFrame();
};

CGifFrame *CGifJob::DecodeNextFrame()
{
	int DelayMs = 100;
	const unsigned char *pData = m_pStream->NextFrame(&DelayMs);
	if(!pData)
	{
		// The animation is over, start it over. A file with a single frame is
		// a still image, not an animation; looping it would only burn space.
		if(m_NextFrame == m_FirstFrame || !m_pStream->Rewind())
			return 0;

		pData = m_pStream->NextFrame(&DelayMs);
		if(!pData)
			return 0;
	}

	CGifFrame *pFrame = new CGifFrame();
	pFrame->m_Frame = m_NextFrame++;
	pFrame->m_DelayMs = DelayMs;
	pFrame->m_lData.set_size(m_Width * m_Height * 4);
	mem_copy(pFrame->m_lData.base_ptr(), pData, m_Width * m_Height * 4);
	return pFrame;
}

CGifDecoder::CGifDecoder()
{
	m_pThread = 0;
	m_Lock = lock_create();
	m_Semaphore = 0;
	m_Shutdown = false;

	sphore_init(&m_Semaphore);
	m_pThread = thread_init(ThreadEntry, this);
	if(!m_pThread)
		dbg_msg("gif", "failed to start the decoder thread, animated avatars stay still");
}

CGifDecoder::~CGifDecoder()
{
	lock_wait(m_Lock);
	m_Shutdown = true;
	lock_unlock(m_Lock);
	sphore_signal(&m_Semaphore);

	if(m_pThread)
	{
		thread_wait(m_pThread);
		m_pThread = 0;
	}

	for(int i = 0; i < m_lJobs.size(); i++)
		delete m_lJobs[i];
	m_lJobs.clear();

	sphore_destroy(&m_Semaphore);
	lock_destroy(m_Lock);
}

void CGifDecoder::ThreadEntry(void *pUser)
{
	static_cast<CGifDecoder *>(pUser)->Run();
}

void CGifDecoder::Run()
{
	for(;;)
	{
		lock_wait(m_Lock);
		if(m_Shutdown)
		{
			lock_unlock(m_Lock);
			return;
		}

		// compose one frame of the first stream that has room for it
		CGifJob *pJob = 0;
		for(int i = 0; i < m_lJobs.size(); i++)
		{
			CGifJob *pCandidate = m_lJobs[i];
			if(pCandidate->m_bBusy || pCandidate->m_bBroken)
				continue;
			if(pCandidate->m_lFrames.size() >= GIF_PREFETCH)
				continue;
			pJob = pCandidate;
			pJob->m_bBusy = true;
			break;
		}
		lock_unlock(m_Lock);

		if(!pJob)
		{
			sphore_wait(&m_Semaphore);
			continue;
		}

		CGifFrame *pFrame = pJob->DecodeNextFrame();

		bool Stopped = false;
		lock_wait(m_Lock);
		pJob->m_bBusy = false;
		Stopped = pJob->m_bStopped;
		if(!Stopped)
		{
			if(pFrame)
				pJob->m_lFrames.add(pFrame);
			else
				pJob->m_bBroken = true;
		}
		lock_unlock(m_Lock);

		if(Stopped)
		{
			// Stop() removed the job from the list and left it to us, because
			// this thread was still composing its frame.
			delete pFrame;
			delete pJob;
		}
	}
}

CGifJob *CGifDecoder::FindJob(CGifAnimation *pAnimation)
{
	for(int i = 0; i < m_lJobs.size(); i++)
	{
		if(m_lJobs[i]->m_pAnimation == pAnimation)
			return m_lJobs[i];
	}
	return 0;
}

void CGifDecoder::Start(CGifAnimation *pAnimation, CGifStream *pStream, int Width, int Height, int FirstFrame)
{
	// a stream that was never stopped belongs to the same animation
	lock_wait(m_Lock);
	const bool Exists = FindJob(pAnimation) != 0;
	lock_unlock(m_Lock);
	if(Exists)
		Stop(pAnimation);

	if(!m_pThread)
	{
		delete pStream;
		return;
	}

	CGifJob *pJob = new CGifJob();
	pJob->m_pAnimation = pAnimation;
	pJob->m_pStream = pStream;
	pJob->m_Width = Width;
	pJob->m_Height = Height;
	pJob->m_FirstFrame = FirstFrame;
	pJob->m_NextFrame = FirstFrame;

	lock_wait(m_Lock);
	m_lJobs.add(pJob);
	lock_unlock(m_Lock);

	sphore_signal(&m_Semaphore);
}

void CGifDecoder::Stop(CGifAnimation *pAnimation)
{
	CGifJob *pJob = 0;
	bool Busy = false;

	lock_wait(m_Lock);
	pJob = FindJob(pAnimation);
	if(pJob)
	{
		// Taking it out of the list right away keeps the worker from picking it
		// up again and a new animation at the same address from finding it.
		m_lJobs.remove(pJob);
		pJob->m_bStopped = true;
		Busy = pJob->m_bBusy;
	}
	lock_unlock(m_Lock);

	// A busy worker is composing a frame with the job right now; it drops the
	// job once it is done, so the menu never waits for the decoder here. That
	// wait used to sleep for a millisecond or more and showed up as a hitch
	// when a new avatar pushed an old one out of the cache.
	if(pJob && !Busy)
		delete pJob;
}

void CGifDecoder::Notify()
{
	if(m_pThread)
		sphore_signal(&m_Semaphore);
}

CGifFrame *CGifDecoder::PopFrame(CGifAnimation *pAnimation)
{
	CGifFrame *pFrame = 0;
	lock_wait(m_Lock);
	CGifJob *pJob = FindJob(pAnimation);
	if(pJob && pJob->m_lFrames.size())
	{
		pFrame = pJob->m_lFrames[0];
		pJob->m_lFrames.remove_index(0);
	}
	lock_unlock(m_Lock);
	return pFrame;
}

#else

CGifDecoder::CGifDecoder()
{
	m_pThread = 0;
	m_Lock = 0;
	m_Semaphore = 0;
	m_Shutdown = false;
}

CGifDecoder::~CGifDecoder() {}

void CGifDecoder::Start([[maybe_unused]] CGifAnimation *pAnimation, [[maybe_unused]] CGifStream *pStream, [[maybe_unused]] int Width, [[maybe_unused]] int Height, [[maybe_unused]] int FirstFrame) {}

void CGifDecoder::Stop([[maybe_unused]] CGifAnimation *pAnimation) {}

void CGifDecoder::Notify() {}

void CGifDecoder::ThreadEntry([[maybe_unused]] void *pUser) {}

void CGifDecoder::Run() {}

CGifJob *CGifDecoder::FindJob([[maybe_unused]] CGifAnimation *pAnimation) { return 0; }

CGifFrame *CGifDecoder::PopFrame([[maybe_unused]] CGifAnimation *pAnimation) { return 0; }

#endif

CGifAnimation::CGifAnimation(CGifDecoder *pDecoder)
{
	m_pDecoder = pDecoder;
	m_Width = 0;
	m_Height = 0;
	m_CurrentFrame = -1;
	m_DecodedFrame = -1;
	m_bAnimated = false;
	m_FrameTime = 0.0f;
}

CGifAnimation::~CGifAnimation()
{
	if(m_pDecoder)
		m_pDecoder->Stop(this);
}

void CGifAnimation::Reset(IGraphics *pGraphics)
{
	if(m_pDecoder)
		m_pDecoder->Stop(this);

	for(int i = 0; i < m_lTextures.size(); i++)
	{
		if(m_lTextures[i].IsValid())
			pGraphics->UnloadTexture(&m_lTextures[i]);
	}
	m_lTextures.clear();
	m_lDelaysMs.clear();
	m_lFrameIndices.clear();

	m_Width = 0;
	m_Height = 0;
	m_CurrentFrame = -1;
	m_DecodedFrame = -1;
	m_bAnimated = false;
	m_FrameTime = 0.0f;
}

void CGifAnimation::UploadTexture(IGraphics *pGraphics, int Frame, int DelayMs, const unsigned char *pData)
{
	const int Slot = Frame % GIF_TEXTURE_RING;
	if(m_lTextures[Slot].IsValid())
		pGraphics->UnloadTexture(&m_lTextures[Slot]);

	m_lTextures[Slot] = pGraphics->LoadTextureRaw(m_Width, m_Height, 1, CImageInfo::FORMAT_RGBA, pData, CImageInfo::FORMAT_RGBA, 0);
	m_lDelaysMs[Slot] = DelayMs;
	m_lFrameIndices[Slot] = Frame;
}

bool CGifAnimation::UploadDecodedFrames([[maybe_unused]] IGraphics *pGraphics)
{
#if defined(CONF_GIFLIB)
	if(!m_pDecoder)
		return false;

	bool Uploaded = false;
	CGifFrame *pFrame;
	// Stay GIF_PREFETCH frames ahead of the one being played: uploading more
	// would fill the texture ring and overwrite the frame the playback needs.
	while(m_DecodedFrame - m_CurrentFrame < GIF_PREFETCH && (pFrame = m_pDecoder->PopFrame(this)))
	{
		// the second frame turns the image into an animation
		if(pFrame->m_Frame > 0)
			m_bAnimated = true;
		UploadTexture(pGraphics, pFrame->m_Frame, pFrame->m_DelayMs, pFrame->m_lData.base_ptr());
		m_DecodedFrame = pFrame->m_Frame;
		delete pFrame;
		Uploaded = true;
	}
	// room for the worker to decode ahead again
	if(Uploaded)
		m_pDecoder->Notify();
	return Uploaded;
#else
	return false;
#endif
}

bool CGifAnimation::LoadRGBA(IGraphics *pGraphics, int Width, int Height, const unsigned char *pData)
{
	Reset(pGraphics);
	if(!pData || Width <= 0 || Height <= 0)
		return false;

	m_Width = Width;
	m_Height = Height;
	for(int i = 0; i < GIF_TEXTURE_RING; i++)
	{
		m_lTextures.add(IGraphics::CTextureHandle());
		m_lDelaysMs.add(1000);
		m_lFrameIndices.add(-1);
	}

	UploadTexture(pGraphics, 0, 1000, pData);
	m_CurrentFrame = 0;
	m_DecodedFrame = 0;
	return m_lTextures[0].IsValid();
}

bool CGifAnimation::LoadGIF(IGraphics *pGraphics, [[maybe_unused]] const unsigned char *pData, [[maybe_unused]] int DataSize)
{
	Reset(pGraphics);
#if defined(CONF_GIFLIB)
	if(!pData || DataSize < 10)
		return false;
	if(pData[0] != 'G' || pData[1] != 'I' || pData[2] != 'F')
		return false;

	CGifStream *pStream = new CGifStream(pData, DataSize);
	if(!pStream->IsOk())
	{
		delete pStream;
		return false;
	}

	m_Width = pStream->Width();
	m_Height = pStream->Height();
	for(int i = 0; i < GIF_TEXTURE_RING; i++)
	{
		m_lTextures.add(IGraphics::CTextureHandle());
		m_lDelaysMs.add(100);
		m_lFrameIndices.add(-1);
	}

	// The first frame is composed right here: the caller has to know whether
	// the file is usable, and it covers the still avatar while the worker
	// decodes the rest.
	int DelayMs = 100;
	const unsigned char *pFrame = pStream->NextFrame(&DelayMs);
	if(!pFrame)
	{
		delete pStream;
		Reset(pGraphics);
		return false;
	}

	UploadTexture(pGraphics, 0, DelayMs, pFrame);
	m_CurrentFrame = 0;
	m_DecodedFrame = 0;
	m_FrameTime = 0.0f;

	if(m_pDecoder)
		m_pDecoder->Start(this, pStream, m_Width, m_Height, 1);
	else
		delete pStream;
	return true;
#else
	return false;
#endif
}

void CGifAnimation::Update([[maybe_unused]] IGraphics *pGraphics, float Seconds)
{
	UploadDecodedFrames(pGraphics);

	if(!m_bAnimated || !m_lTextures.size() || m_CurrentFrame < 0)
		return;

	m_FrameTime += Seconds * 1000.0f;

	// A long stall must not spin through the whole animation; after a full
	// window the playback simply picks up at the current frame.
	int Guard = 0;
	while(m_FrameTime >= m_lDelaysMs[m_CurrentFrame % GIF_TEXTURE_RING] && Guard++ < GIF_TEXTURE_RING)
	{
		const int NextFrame = m_CurrentFrame + 1;

		// The worker may not have composed it yet; showing the current frame
		// for another tick is better than skipping ahead or drawing nothing.
		if(m_lFrameIndices[NextFrame % GIF_TEXTURE_RING] != NextFrame)
			break;

		m_FrameTime -= m_lDelaysMs[m_CurrentFrame % GIF_TEXTURE_RING];
		m_CurrentFrame = NextFrame;
	}

	if(Guard >= GIF_TEXTURE_RING)
		m_FrameTime = 0.0f;
}

IGraphics::CTextureHandle CGifAnimation::Texture() const
{
	if(m_CurrentFrame < 0 || !m_lTextures.size())
		return IGraphics::CTextureHandle();

	const int Slot = m_CurrentFrame % GIF_TEXTURE_RING;
	if(m_lFrameIndices[Slot] != m_CurrentFrame)
		return IGraphics::CTextureHandle();
	return m_lTextures[Slot];
}