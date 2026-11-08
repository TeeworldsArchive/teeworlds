/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#include "gif_animation.h"

#include <base/math.h>
#include <base/system/debug.h>
#include <base/system/mem.h>

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
#endif

CGifAnimation::CGifAnimation()
{
	m_pStream = 0;
	m_Width = 0;
	m_Height = 0;
	m_CurrentFrame = -1;
	m_DecodedFrame = -1;
	m_bAnimated = false;
	m_FrameTime = 0.0f;
}

CGifAnimation::~CGifAnimation()
{
#if defined(CONF_GIFLIB)
	delete m_pStream;
#endif
	m_pStream = 0;
}

void CGifAnimation::Reset(IGraphics *pGraphics)
{
	for(int i = 0; i < m_lTextures.size(); i++)
	{
		if(m_lTextures[i].IsValid())
			pGraphics->UnloadTexture(&m_lTextures[i]);
	}
	m_lTextures.clear();
	m_lDelaysMs.clear();
	m_lFrameIndices.clear();

#if defined(CONF_GIFLIB)
	delete m_pStream;
#endif
	m_pStream = 0;

	m_Width = 0;
	m_Height = 0;
	m_CurrentFrame = -1;
	m_DecodedFrame = -1;
	m_bAnimated = false;
	m_FrameTime = 0.0f;
}

bool CGifAnimation::RewindStream()
{
#if defined(CONF_GIFLIB)
	return m_pStream && m_pStream->Rewind();
#else
	return false;
#endif
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

bool CGifAnimation::DecodeNextFrame([[maybe_unused]] IGraphics *pGraphics)
{
#if defined(CONF_GIFLIB)
	if(!m_pStream)
		return false;

	int DelayMs = 100;
	const unsigned char *pData = m_pStream->NextFrame(&DelayMs);
	if(!pData)
		return false;

	const int Frame = m_DecodedFrame + 1;
	UploadTexture(pGraphics, Frame, DelayMs, pData);
	m_DecodedFrame = Frame;
	return true;
#else
	return false;
#endif
}

void CGifAnimation::Prefetch([[maybe_unused]] IGraphics *pGraphics)
{
#if defined(CONF_GIFLIB)
	const int Target = m_CurrentFrame + GIF_PREFETCH;
	while(m_DecodedFrame < Target)
	{
		// the ring must not overwrite a frame that is about to be played
		if(m_DecodedFrame - m_CurrentFrame >= GIF_TEXTURE_RING - 1)
			break;

		if(DecodeNextFrame(pGraphics))
			continue;

		// the animation is over, start it over
		if(!RewindStream() || !DecodeNextFrame(pGraphics))
			break;
	}
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

	m_pStream = new CGifStream(pData, DataSize);
	if(!m_pStream->IsOk())
	{
		Reset(pGraphics);
		return false;
	}

	m_Width = m_pStream->Width();
	m_Height = m_pStream->Height();
	for(int i = 0; i < GIF_TEXTURE_RING; i++)
	{
		m_lTextures.add(IGraphics::CTextureHandle());
		m_lDelaysMs.add(100);
		m_lFrameIndices.add(-1);
	}

	if(!DecodeNextFrame(pGraphics))
	{
		Reset(pGraphics);
		return false;
	}

	// a second frame tells an animation from a gif that is one image
	if(DecodeNextFrame(pGraphics))
		m_bAnimated = true;

	m_CurrentFrame = 0;
	m_FrameTime = 0.0f;
	return true;
#else
	return false;
#endif
}

void CGifAnimation::Update([[maybe_unused]] IGraphics *pGraphics, float Seconds)
{
	if(!m_bAnimated || !m_lTextures.size() || m_CurrentFrame < 0)
		return;

	m_FrameTime += Seconds * 1000.0f;

	// A long stall must not spin through the whole animation; after a full
	// window the playback simply picks up at the current frame.
	int Guard = 0;
	while(m_FrameTime >= m_lDelaysMs[m_CurrentFrame % GIF_TEXTURE_RING] && Guard++ < GIF_TEXTURE_RING)
	{
		m_FrameTime -= m_lDelaysMs[m_CurrentFrame % GIF_TEXTURE_RING];
		const int NextFrame = m_CurrentFrame + 1;

		if(m_DecodedFrame < NextFrame)
		{
			if(!DecodeNextFrame(pGraphics))
			{
				// loop, and keep the numbering running so the ring slots of
				// the previous run are recognised as stale
				if(!RewindStream() || !DecodeNextFrame(pGraphics))
				{
					m_FrameTime = 0.0f;
					return;
				}
			}
		}

		m_CurrentFrame = NextFrame;
		Prefetch(pGraphics);
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