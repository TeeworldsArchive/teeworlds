/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <base/detect.h>
#include <base/math.h>
#include <base/tl/threading.h>

#include <base/system.h>

#include <spng.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <math.h> // cosf, sinf

#include "graphics_threaded.h"
#include "graphics_threaded_null.h"

static CVideoMode g_aFakeModes[] = {
	{320, 200}, {320, 240}, {400, 300},
	{512, 384}, {640, 400}, {640, 480},
	{720, 400}, {768, 576}, {800, 600},
	{1024, 600}, {1024, 768}, {1152, 864},
	{1280, 600}, {1280, 720}, {1280, 768},
	{1280, 800}, {1280, 960}, {1280, 1024},
	{1360, 768}, {1366, 768}, {1368, 768},
	{1400, 1050}, {1440, 900}, {1440, 1050},
	{1600, 900}, {1600, 1000}, {1600, 1200},
	{1680, 1050}, {1792, 1344}, {1800, 1440},
	{1856, 1392}, {1920, 1080}, {1920, 1200},
	{1920, 1440}, {1920, 2400}, {2048, 1536}};

static unsigned char Sample(int w, int h, const unsigned char *pData, int u, int v, int Offset, int ScaleW, int ScaleH, int Bpp)
{
	int Sum = 0;
	for(int x = 0; x < ScaleW; x++)
		for(int y = 0; y < ScaleH; y++)
			Sum += pData[((v + y) * w + (u + x)) * Bpp + Offset];
	return Sum / (ScaleW * ScaleH);
}

void *RescaleImage(int Width, int Height, int NewWidth, int NewHeight, int Format, const unsigned char *pData)
{
	int ScaleW = Width / NewWidth;
	int ScaleH = Height / NewHeight;

	if(ScaleW == 1 && ScaleH == 1)
		return (void *) pData;
	int Bpp = 3;
	if(Format == CCommandBuffer::TEXFORMAT_RGBA)
		Bpp = 4;
	else if(Format == CCommandBuffer::TEXFORMAT_RG)
		Bpp = 2;

	unsigned char *pTmpData = (unsigned char *) mem_alloc(NewWidth * NewHeight * Bpp);

	for(int y = 0; y < NewHeight; y++)
		for(int x = 0; x < NewWidth; x++)
			for(int b = 0; b < Bpp; b++)
				pTmpData[(NewWidth * y + x) * Bpp + b] = Sample(Width, Height, pData, x * ScaleW, y * ScaleH, b, ScaleW, ScaleH, Bpp);

	return pTmpData;
}

void CGraphics_Threaded::FlushVertices()
{
	if(m_NumVertices == 0)
		return;

	int NumVerts = m_NumVertices;
	m_NumVertices = 0;

	CCommandBuffer::CRenderCommand Cmd;
	// the vertices were recorded under this state, not necessarily the current one
	Cmd.m_State = m_PendingState;

	if(m_PendingPrimType == DRAWING_QUADS)
	{
		Cmd.m_PrimType = CCommandBuffer::PRIMTYPE_QUADS;
		Cmd.m_PrimCount = NumVerts / 4;
	}
	else if(m_PendingPrimType == DRAWING_LINES)
	{
		Cmd.m_PrimType = CCommandBuffer::PRIMTYPE_LINES;
		Cmd.m_PrimCount = NumVerts / 2;
	}
	else
	{
		dbg_assert(0, "flushing vertices without a pending primitive type");
		return;
	}

	Cmd.m_pVertices = (CCommandBuffer::CVertex *) m_pCommandBuffer->AllocData(sizeof(CCommandBuffer::CVertex) * NumVerts);
	if(Cmd.m_pVertices == 0x0)
	{
		// kick command buffer and try again
		KickCommandBuffer();

		Cmd.m_pVertices = (CCommandBuffer::CVertex *) m_pCommandBuffer->AllocData(sizeof(CCommandBuffer::CVertex) * NumVerts);
		if(Cmd.m_pVertices == 0x0)
		{
			dbg_msg("graphics", "failed to allocate data for vertices");
			return;
		}
	}

	// check if we have enough free memory in the commandbuffer
	if(!m_pCommandBuffer->AddCommand(Cmd))
	{
		// kick command buffer and try again
		KickCommandBuffer();

		Cmd.m_pVertices = (CCommandBuffer::CVertex *) m_pCommandBuffer->AllocData(sizeof(CCommandBuffer::CVertex) * NumVerts);
		if(Cmd.m_pVertices == 0x0)
		{
			dbg_msg("graphics", "failed to allocate data for vertices");
			return;
		}

		if(!m_pCommandBuffer->AddCommand(Cmd))
		{
			dbg_msg("graphics", "failed to allocate memory for render command");
			return;
		}
	}

	mem_copy(Cmd.m_pVertices, m_aVertices, sizeof(CCommandBuffer::CVertex) * NumVerts);
	m_RenderCommandCount++;
}

void CGraphics_Threaded::ReserveVertices(int Count)
{
	if(m_NumVertices + Count > CCommandBuffer::MAX_VERTICES)
		FlushVertices();

	// the first vertices of a batch fix the state it will be flushed with
	if(m_NumVertices == 0)
		m_PendingState = m_State;
}

void CGraphics_Threaded::AddVertices(int Count)
{
	m_NumVertices += Count;
	dbg_assert(m_NumVertices <= CCommandBuffer::MAX_VERTICES, "vertex buffer overflow");
	if(m_NumVertices >= CCommandBuffer::MAX_VERTICES)
		FlushVertices();
}

void CGraphics_Threaded::FlushPendingVerticesOnStateChange()
{
	if(m_NumVertices > 0)
		FlushVertices();
}

void CGraphics_Threaded::FlushPendingVertices()
{
	if(m_NumVertices > 0)
		FlushVertices();
}

void CGraphics_Threaded::ClearTextSDF()
{
	if(!m_State.m_IsSDF)
		return;

	FlushPendingVerticesOnStateChange();
	m_State.m_IsSDF = false;
	m_State.m_SDFGain = 1.0f;
	m_State.m_SDFOutlineOffset = 0.0f;
	mem_zero(&m_State.m_SDFOutlineColor, sizeof(m_State.m_SDFOutlineColor));
}

void CGraphics_Threaded::Rotate4(const CCommandBuffer::CPoint &rCenter, CCommandBuffer::CVertex *pPoints)
{
	float c = cosf(m_Rotation);
	float s = sinf(m_Rotation);
	float x, y;
	int i;

	for(i = 0; i < 4; i++)
	{
		x = pPoints[i].m_Pos.x - rCenter.x;
		y = pPoints[i].m_Pos.y - rCenter.y;
		pPoints[i].m_Pos.x = x * c - y * s + rCenter.x;
		pPoints[i].m_Pos.y = x * s + y * c + rCenter.y;
	}
}

CGraphics_Threaded::CGraphics_Threaded()
{
	m_State.m_ScreenTL.x = 0;
	m_State.m_ScreenTL.y = 0;
	m_State.m_ScreenBR.x = 0;
	m_State.m_ScreenBR.y = 0;
	m_State.m_ClipEnable = false;
	m_State.m_ClipX = 0;
	m_State.m_ClipY = 0;
	m_State.m_ClipW = 0;
	m_State.m_ClipH = 0;
	m_State.m_Texture = -1;
	m_State.m_BlendMode = CCommandBuffer::BLEND_NONE;
	m_State.m_WrapModeU = WRAP_REPEAT;
	m_State.m_WrapModeV = WRAP_REPEAT;
	m_State.m_IsStainedOnly = false;

	m_State.m_IsSDF = false;
	m_State.m_SDFGain = 1.0f;
	m_State.m_SDFOutlineOffset = 0.0f;
	mem_zero(&m_State.m_SDFOutlineColor, sizeof(m_State.m_SDFOutlineColor));

	m_CurrentCommandBuffer = 0;
	m_pCommandBuffer = 0x0;
	m_apCommandBuffers[0] = 0x0;
	m_apCommandBuffers[1] = 0x0;

	m_NumVertices = 0;
	m_PendingPrimType = 0;
	m_SDFArmed = false;
	m_RenderCommandCount = 0;
	m_RenderedFrameCount = 0;

	m_ScreenWidth = -1;
	m_ScreenHeight = -1;

	m_Rotation = 0;
	m_Drawing = 0;
	m_GlobalAlpha = 1.0f;

	m_TextureMemoryUsage = 0;

	m_RenderEnable = true;
	m_DoScreenshot = false;
}

void CGraphics_Threaded::ClipEnable(int x, int y, int w, int h)
{
	if(x < 0)
		w += x;
	if(y < 0)
		h += y;

	x = clamp(x, 0, ScreenWidth());
	y = clamp(y, 0, ScreenHeight());
	w = clamp(w, 0, ScreenWidth() - x);
	h = clamp(h, 0, ScreenHeight() - y);

	const int ClipY = ScreenHeight() - (y + h);
	if(!m_State.m_ClipEnable || m_State.m_ClipX != x || m_State.m_ClipY != ClipY ||
		m_State.m_ClipW != w || m_State.m_ClipH != h)
	{
		FlushPendingVerticesOnStateChange();
		m_State.m_ClipEnable = true;
		m_State.m_ClipX = x;
		m_State.m_ClipY = ClipY;
		m_State.m_ClipW = w;
		m_State.m_ClipH = h;
	}
}

void CGraphics_Threaded::ClipDisable()
{
	if(!m_State.m_ClipEnable)
		return;

	FlushPendingVerticesOnStateChange();
	m_State.m_ClipEnable = false;
}

void CGraphics_Threaded::BlendNone()
{
	if(m_State.m_BlendMode == CCommandBuffer::BLEND_NONE)
		return;

	FlushPendingVerticesOnStateChange();
	m_State.m_BlendMode = CCommandBuffer::BLEND_NONE;
}

void CGraphics_Threaded::BlendNormal()
{
	if(m_State.m_BlendMode == CCommandBuffer::BLEND_ALPHA)
		return;

	FlushPendingVerticesOnStateChange();
	m_State.m_BlendMode = CCommandBuffer::BLEND_ALPHA;
}

void CGraphics_Threaded::WrapNormal()
{
	WrapMode(IGraphics::WRAP_REPEAT, IGraphics::WRAP_REPEAT);
}

void CGraphics_Threaded::WrapClamp()
{
	WrapMode(WRAP_CLAMP, WRAP_CLAMP);
}

void CGraphics_Threaded::WrapMode(int WrapU, int WrapV)
{
	if(m_State.m_WrapModeU == WrapU && m_State.m_WrapModeV == WrapV)
		return;

	FlushPendingVerticesOnStateChange();
	m_State.m_WrapModeU = WrapU;
	m_State.m_WrapModeV = WrapV;
}

int CGraphics_Threaded::MemoryUsage() const
{
	return m_pBackend->MemoryUsage();
}

int CGraphics_Threaded::MaxTextureSize() const
{
	return m_pBackend->MaxTextureSize();
}

int CGraphics_Threaded::TakeRenderCommandCount()
{
	const int Count = m_RenderCommandCount;
	m_RenderCommandCount = 0;
	return Count;
}

int CGraphics_Threaded::TakeRenderedFrameCount()
{
	const int Count = m_RenderedFrameCount;
	m_RenderedFrameCount = 0;
	return Count;
}

int64 CGraphics_Threaded::TakeRenderThreadTime()
{
	return m_pBackend->TakeRenderThreadTime();
}

void CGraphics_Threaded::StainedOnly(bool Flag)
{
	if(m_State.m_IsStainedOnly == Flag)
		return;

	FlushPendingVerticesOnStateChange();
	m_State.m_IsStainedOnly = Flag;
}

void CGraphics_Threaded::SetTextSDF(const CTextSDFParams &Params)
{
	// re-issuing parameters already in effect must not split the batch
	if(m_State.m_IsSDF == Params.m_Enable &&
		m_State.m_SDFGain == Params.m_Gain &&
		m_State.m_SDFOutlineOffset == Params.m_OutlineOffset &&
		m_State.m_SDFOutlineColor.r == Params.m_OutlineColor.r &&
		m_State.m_SDFOutlineColor.g == Params.m_OutlineColor.g &&
		m_State.m_SDFOutlineColor.b == Params.m_OutlineColor.b &&
		m_State.m_SDFOutlineColor.a == Params.m_OutlineColor.a)
	{
		m_SDFArmed = Params.m_Enable;
		return;
	}

	FlushPendingVerticesOnStateChange();

	m_State.m_IsSDF = Params.m_Enable;
	m_State.m_SDFGain = Params.m_Gain;
	m_State.m_SDFOutlineOffset = Params.m_OutlineOffset;
	m_State.m_SDFOutlineColor.r = Params.m_OutlineColor.r;
	m_State.m_SDFOutlineColor.g = Params.m_OutlineColor.g;
	m_State.m_SDFOutlineColor.b = Params.m_OutlineColor.b;
	m_State.m_SDFOutlineColor.a = Params.m_OutlineColor.a;

	m_SDFArmed = Params.m_Enable;
}

float CGraphics_Threaded::ScreenUIScale() const
{
	return m_pConfig->m_GfxUIScale == -1 ? m_ScreenUIScale : m_pConfig->m_GfxUIScale / 5.0f + 1.0f;
}

void CGraphics_Threaded::MapScreen(float TopLeftX, float TopLeftY, float BottomRightX, float BottomRightY)
{
	if(m_State.m_ScreenTL.x == TopLeftX && m_State.m_ScreenTL.y == TopLeftY &&
		m_State.m_ScreenBR.x == BottomRightX && m_State.m_ScreenBR.y == BottomRightY)
		return;

	FlushPendingVerticesOnStateChange();
	m_State.m_ScreenTL.x = TopLeftX;
	m_State.m_ScreenTL.y = TopLeftY;
	m_State.m_ScreenBR.x = BottomRightX;
	m_State.m_ScreenBR.y = BottomRightY;
}

void CGraphics_Threaded::GetScreen(float *pTopLeftX, float *pTopLeftY, float *pBottomRightX, float *pBottomRightY)
{
	*pTopLeftX = m_State.m_ScreenTL.x;
	*pTopLeftY = m_State.m_ScreenTL.y;
	*pBottomRightX = m_State.m_ScreenBR.x;
	*pBottomRightY = m_State.m_ScreenBR.y;
}

void CGraphics_Threaded::LinesBegin()
{
	dbg_assert(m_Drawing == 0, "called Graphics()->LinesBegin twice");

	// switching primitive type cannot be expressed in a single render command
	if(m_NumVertices > 0 && m_PendingPrimType != DRAWING_LINES)
		FlushVertices();

	m_Drawing = DRAWING_LINES;
	m_PendingPrimType = DRAWING_LINES;

	// lines never use the glyph shader path
	ClearTextSDF();
	m_SDFArmed = false;

	SetColor(1, 1, 1, 1);
}

void CGraphics_Threaded::LinesEnd()
{
	dbg_assert(m_Drawing == DRAWING_LINES, "called Graphics()->LinesEnd without begin");
	// see QuadsEnd(): the batch is kept alive until something changes
	m_Drawing = 0;
}

void CGraphics_Threaded::LinesDraw(const CLineItem *pArray, int Num)
{
	dbg_assert(m_Drawing == DRAWING_LINES, "called Graphics()->LinesDraw without begin");

	ReserveVertices(2 * Num);

	for(int i = 0; i < Num; ++i)
	{
		m_aVertices[m_NumVertices + 2 * i].m_Pos.x = pArray[i].m_X0;
		m_aVertices[m_NumVertices + 2 * i].m_Pos.y = pArray[i].m_Y0;
		m_aVertices[m_NumVertices + 2 * i].m_Tex = m_aTexture[0];
		m_aVertices[m_NumVertices + 2 * i].m_Color = m_aColor[0];

		m_aVertices[m_NumVertices + 2 * i + 1].m_Pos.x = pArray[i].m_X1;
		m_aVertices[m_NumVertices + 2 * i + 1].m_Pos.y = pArray[i].m_Y1;
		m_aVertices[m_NumVertices + 2 * i + 1].m_Tex = m_aTexture[1];
		m_aVertices[m_NumVertices + 2 * i + 1].m_Color = m_aColor[1];
	}

	AddVertices(2 * Num);
}

int CGraphics_Threaded::UnloadTexture(CTextureHandle *pIndex)
{
	if(pIndex->Id() == m_InvalidTexture.Id())
		return 0;

	if(!pIndex->IsValid())
		return 0;

	FlushPendingVertices();

	CCommandBuffer::CTextureDestroyCommand Cmd;
	Cmd.m_Slot = pIndex->Id();
	m_pCommandBuffer->AddCommand(Cmd);

	m_aTextureIndices[pIndex->Id()] = m_FirstFreeTexture;
	m_FirstFreeTexture = pIndex->Id();
	m_aTextureFormats[pIndex->Id()] = CCommandBuffer::TEXFORMAT_INVALID;

	pIndex->Invalidate();
	return 0;
}

static int ImageFormatToTexFormat(int Format)
{
	if(Format == CImageInfo::FORMAT_RGB)
		return CCommandBuffer::TEXFORMAT_RGB;
	if(Format == CImageInfo::FORMAT_RGBA)
		return CCommandBuffer::TEXFORMAT_RGBA;
	if(Format == CImageInfo::FORMAT_ALPHA)
		return CCommandBuffer::TEXFORMAT_ALPHA;
	if(Format == CImageInfo::FORMAT_RG)
		return CCommandBuffer::TEXFORMAT_RG;
	return CCommandBuffer::TEXFORMAT_RGBA;
}

int CGraphics_Threaded::LoadTextureRawSub(CTextureHandle TextureID, int x, int y, int z, int Width, int Height, int Format, const void *pData)
{
	if(!TextureID.IsValid())
		return 0;

	FlushPendingVertices();

	CCommandBuffer::CTextureUpdateCommand Cmd;
	Cmd.m_Slot = TextureID.Id();
	Cmd.m_X = x;
	Cmd.m_Y = y;
	Cmd.m_Z = z;
	Cmd.m_Width = Width;
	Cmd.m_Height = Height;
	Cmd.m_Format = ImageFormatToTexFormat(Format);
	m_aTextureFormats[TextureID.Id()] = Cmd.m_Format;

	// calculate memory usage
	const int MemSize = Width * Height * CImageInfo::GetPixelSize(Format);

	// copy texture data
	void *pTmpData = mem_alloc(MemSize);
	mem_copy(pTmpData, pData, MemSize);
	Cmd.m_pData = pTmpData;

	//
	m_pCommandBuffer->AddCommand(Cmd);
	return 0;
}

bool CGraphics_Threaded::TilemapShaderEnabled() const
{
	return true;
}

void CGraphics_Threaded::RenderTilemapTexture(CTextureHandle TileData, int Layer, int Width, int Height, int PassMode, bool ColorOpaque, const vec4 &Color)
{
	if(!TileData.IsValid() || Width <= 0 || Height <= 0)
		return;

	// the tilemap draw is its own command, so anything batched before it has to go out first
	FlushPendingVertices();
	// and it does not use the glyph shader path
	ClearTextSDF();
	m_SDFArmed = false;

	CCommandBuffer::CRenderTilemapTextureCommand Cmd;
	Cmd.m_State = m_State;
	Cmd.m_TileData = TileData.Id();
	Cmd.m_Layer = Layer;
	Cmd.m_Width = Width;
	Cmd.m_Height = Height;
	Cmd.m_PassMode = PassMode;
	Cmd.m_ColorOpaque = ColorOpaque;
	Cmd.m_Color.r = Color.r;
	Cmd.m_Color.g = Color.g;
	Cmd.m_Color.b = Color.b;
	Cmd.m_Color.a = Color.a;

	// the quad covers the screen; texcoords carry tile units for the fragment shader
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float Scale = 32.0f;
	const float aPos[8] = {ScreenX0, ScreenY0, ScreenX1, ScreenY0, ScreenX1, ScreenY1, ScreenX0, ScreenY1};
	const float aUV[8] = {ScreenX0 / Scale, ScreenY0 / Scale, ScreenX1 / Scale, ScreenY0 / Scale, ScreenX1 / Scale, ScreenY1 / Scale, ScreenX0 / Scale, ScreenY1 / Scale};
	for(int i = 0; i < 4; i++)
	{
		Cmd.m_aVertices[i].m_Pos.x = aPos[i * 2 + 0];
		Cmd.m_aVertices[i].m_Pos.y = aPos[i * 2 + 1];
		Cmd.m_aVertices[i].m_Tex.u = aUV[i * 2 + 0];
		Cmd.m_aVertices[i].m_Tex.v = aUV[i * 2 + 1];
		Cmd.m_aVertices[i].m_Tex.i = 0.0f;
		Cmd.m_aVertices[i].m_Color.r = Color.r;
		Cmd.m_aVertices[i].m_Color.g = Color.g;
		Cmd.m_aVertices[i].m_Color.b = Color.b;
		Cmd.m_aVertices[i].m_Color.a = Color.a;
	}

	if(!m_pCommandBuffer->AddCommand(Cmd))
	{
		KickCommandBuffer();
		if(!m_pCommandBuffer->AddCommand(Cmd))
			dbg_msg("graphics", "failed to allocate memory for tilemap render command");
	}
}

IGraphics::CTextureHandle CGraphics_Threaded::LoadTextureRaw(int Width, int Height, int Layers, int Format, const void *pData, int StoreFormat, int Flags)
{
	// don't waste memory on texture if we are stress testing
#ifdef CONF_DEBUG
	if(m_pConfig->m_DbgStress)
		return m_InvalidTexture;
#endif

	// grab texture
	int Tex = m_FirstFreeTexture;
	if(Tex < 0 || Tex >= MAX_TEXTURES)
	{
		dbg_msg("graphics", "texture pool exhausted, cannot allocate a %dx%dx%d texture", Width, Height, Layers);
		return IGraphics::CTextureHandle();
	}
	m_FirstFreeTexture = m_aTextureIndices[Tex];
	m_aTextureIndices[Tex] = -1;

	FlushPendingVertices();

	CCommandBuffer::CTextureCreateCommand Cmd;
	Cmd.m_Slot = Tex;
	Cmd.m_Width = Width;
	Cmd.m_Height = Height;
	Cmd.m_Layers = Layers;
	Cmd.m_PixelSize = CImageInfo::GetPixelSize(Format);
	Cmd.m_Format = ImageFormatToTexFormat(Format);
	Cmd.m_StoreFormat = ImageFormatToTexFormat(StoreFormat);
	m_aTextureFormats[Tex] = Cmd.m_Format;

	// flags
	Cmd.m_Flags = 0;
	if(Flags & IGraphics::TEXLOAD_NOMIPMAPS)
		Cmd.m_Flags |= CCommandBuffer::TEXFLAG_NOMIPMAPS;
	if(m_pConfig->m_GfxTextureQuality || Flags & TEXLOAD_NORESAMPLE)
		Cmd.m_Flags |= CCommandBuffer::TEXFLAG_QUALITY;
	if(Flags & TEXLOAD_NORESAMPLE)
		Cmd.m_Flags |= CCommandBuffer::TEXFLAG_NORESAMPLE;
	// copy texture data
	int MemSize = Width * Height * Layers * Cmd.m_PixelSize;
	unsigned char *pTmpData = (unsigned char *) mem_alloc(MemSize);
	if(Flags & IGraphics::TEXLOAD_TILEMAP)
	{
		const int TileWidth = Width / IGraphics::NUMTILES_DIMENSION;
		const int TileHeight = Height / IGraphics::NUMTILES_DIMENSION;
		const int Layers = IGraphics::NUMTILES_DIMENSION * IGraphics::NUMTILES_DIMENSION;

		Cmd.m_Width = TileWidth;
		Cmd.m_Height = TileHeight;
		Cmd.m_Layers = Layers;

		// allocate memory for 3D texture data
		const int TileSize = TileWidth * TileHeight * Cmd.m_PixelSize;
		const int TileRowSize = TileWidth * Cmd.m_PixelSize;

		// copy
		for(int i = 0; i < Layers; i++)
		{
			const int px = (i % IGraphics::NUMTILES_DIMENSION) * TileWidth;
			const int py = (i / IGraphics::NUMTILES_DIMENSION) * TileHeight;

			for(int y = 0; y < TileHeight; y++)
			{
				const int SrcOffset = ((py + y) * Width + px) * Cmd.m_PixelSize;
				const int DestOffset = (i * TileSize) + (y * TileRowSize);
				mem_copy(pTmpData + DestOffset, (char *) pData + SrcOffset, TileRowSize);
			}
		}
	}
	else
	{
		mem_copy(pTmpData, pData, MemSize);
	}
	Cmd.m_pData = pTmpData;

	//
	m_pCommandBuffer->AddCommand(Cmd);

	return CreateTextureHandle(Tex);
}

// simple uncompressed RGBA loaders
IGraphics::CTextureHandle CGraphics_Threaded::LoadTexture(const char *pFilename, int StorageType, int StoreFormat, int Flags)
{
	int l = str_length(pFilename);
	IGraphics::CTextureHandle ID;
	CImageInfo Img;

	if(l < 3)
		return CTextureHandle();
	if(LoadPNG(&Img, pFilename, StorageType))
	{
		if(StoreFormat == CImageInfo::FORMAT_AUTO)
			StoreFormat = Img.m_Format;

		ID = LoadTextureRaw(Img.m_Width, Img.m_Height, 1, Img.m_Format, Img.m_pData, StoreFormat, Flags);
		mem_free(Img.m_pData);
		if(ID.Id() != m_InvalidTexture.Id() && m_pConfig->m_Debug)
			dbg_msg("graphics/texture", "loaded %s", pFilename);
		return ID;
	}

	return m_InvalidTexture;
}

int CGraphics_Threaded::LoadPNGRaw(CImageInfo *pImg, const unsigned char *pData, int Size, const char *pContext)
{
	spng_ctx *pPng = spng_ctx_new(SPNG_CTX_IGNORE_ADLER32);
	int Error = spng_set_png_buffer(pPng, pData, Size);
	if(Error)
	{
		dbg_msg("game/png", "failed to read data. context='%s', error='%s'", pContext, spng_strerror(Error));
		return 0;
	}

	spng_ihdr Info;
	Error = spng_get_ihdr(pPng, &Info);
	if(Error || Info.bit_depth != 8 || Info.width > (2 << 12) || Info.height > (2 << 12))
	{
		dbg_msg("game/png", "invalid format. context='%s', error='%s'", pContext, spng_strerror(Error));
		spng_ctx_free(pPng);
		return 0;
	}

	if(Info.color_type == SPNG_COLOR_TYPE_TRUECOLOR)
		pImg->m_Format = CImageInfo::FORMAT_RGB;
	else if(Info.color_type == SPNG_COLOR_TYPE_TRUECOLOR_ALPHA)
		pImg->m_Format = CImageInfo::FORMAT_RGBA;
	else
	{
		dbg_msg("game/png", "invalid format. context='%s', error='%s'", pContext, spng_strerror(Error));
		spng_ctx_free(pPng);
		return 0;
	}

	size_t ImageSize;
	spng_format Format = Info.color_type == SPNG_COLOR_TYPE_TRUECOLOR ? SPNG_FMT_RGB8 : SPNG_FMT_RGBA8;
	Error = spng_decoded_image_size(pPng, Format, &ImageSize);
	if(Error)
	{
		dbg_msg("game/png", "invalid size. context='%s', error='%s'", pContext, spng_strerror(Error));
		spng_ctx_free(pPng);
		return 0;
	}
	unsigned char *pBuffer = (unsigned char *) mem_alloc(ImageSize);
	Error = spng_decode_image(pPng, pBuffer, ImageSize, Format, 0);
	spng_ctx_free(pPng);
	if(Error)
	{
		dbg_msg("game/png", "failed to decode image. context='%s', error='%s'", pContext, spng_strerror(Error));
		return 0;
	}

	pImg->m_Width = Info.width;
	pImg->m_Height = Info.height;
	pImg->m_pData = pBuffer;
	return 1;
}

int CGraphics_Threaded::LoadPNG(CImageInfo *pImg, const char *pFilename, int StorageType)
{
	// open file for reading
	char aCompleteFilename[IO_MAX_PATH_LENGTH];
	IOHANDLE File = m_pStorage->OpenFile(pFilename, IOFLAG_READ, StorageType, aCompleteFilename, sizeof(aCompleteFilename));
	if(!File)
	{
		dbg_msg("game/png", "failed to open file. filename='%s'", pFilename);
		return 0;
	}
	unsigned char *pData;
	unsigned DataSize;
	io_read_all(File, (void **) &pData, &DataSize);

	int Result = LoadPNGRaw(pImg, pData, DataSize, aCompleteFilename);
	mem_free(pData);
	io_close(File);
	return Result;
}

void CGraphics_Threaded::KickCommandBuffer()
{
	m_pBackend->RunBuffer(m_pCommandBuffer);

	// swap buffer
	m_CurrentCommandBuffer ^= 1;
	m_pCommandBuffer = m_apCommandBuffers[m_CurrentCommandBuffer];
	m_pCommandBuffer->Reset();
}

void CGraphics_Threaded::ScreenshotDirect(const char *pFilename, const char *pThumbnail)
{
	FlushPendingVertices();

	// add swap command
	CImageInfo Image;
	mem_zero(&Image, sizeof(Image));

	CCommandBuffer::CScreenshotCommand Cmd;
	Cmd.m_pImage = &Image;
	Cmd.m_X = 0;
	Cmd.m_Y = 0;
	Cmd.m_W = -1;
	Cmd.m_H = -1;
	m_pCommandBuffer->AddCommand(Cmd);

	// kick the buffer and wait for the result
	KickCommandBuffer();
	WaitForIdle();

	if(Image.m_pData)
	{
		// find filename
		char aWholePath[IO_MAX_PATH_LENGTH];
		char aBuf[IO_MAX_PATH_LENGTH + 32];
		IOHANDLE File = m_pStorage->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE, aWholePath, sizeof(aWholePath));
		int Error = 0;
		uint8_t ColorType = static_cast<uint8_t>(Image.m_Format == CImageInfo::FORMAT_RGB ? SPNG_COLOR_TYPE_TRUECOLOR : SPNG_COLOR_TYPE_TRUECOLOR_ALPHA);
		if(File)
		{
			spng_ctx *pPng = spng_ctx_new(SPNG_CTX_ENCODER);
			Error = !pPng;
			if(!Error)
			{
				// save png
				spng_ihdr Info = {
					.width = (unsigned) Image.m_Width,
					.height = (unsigned) Image.m_Height,
					.bit_depth = 8,
					.color_type = ColorType,
					.compression_method = 0,
					.filter_method = 0,
					.interlace_method = 0};
				if(!(Error = spng_set_ihdr(pPng, &Info)))
				{
					spng_set_png_file(pPng, (FILE *) File);
					Error = spng_encode_image(pPng, Image.m_pData, Image.m_Height * Image.m_Width * CImageInfo::GetPixelSize(Image.m_Format), SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
					if(!Error)
					{
						str_format(aBuf, sizeof(aBuf), "saved screenshot to '%s'", aWholePath);
						io_close(File);
						File = 0;
						if(m_pfnScreenshot)
							m_pfnScreenshot(m_pScreenshotUser, pFilename);
					}
				}
				spng_ctx_free(pPng);
			}
			if(File)
				io_close(File);
		}

		if(Error)
		{
			str_format(aBuf, sizeof(aBuf), "failed to encode file '%s'", pFilename);
			if(m_pfnScreenshot)
				m_pfnScreenshot(m_pScreenshotUser, 0);
		}
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client/screenshot", aBuf);

		// thumbnail
		File = m_pStorage->OpenFile(pThumbnail, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(File)
		{
			int NewWidth = Image.m_Width / 4;
			int NewHeight = Image.m_Height / 4;
			void *pTmpData = RescaleImage(Image.m_Width, Image.m_Height, NewWidth, NewHeight, ImageFormatToTexFormat(Image.m_Format), static_cast<const unsigned char *>(Image.m_pData));
			if(pTmpData != Image.m_pData)
				mem_free(Image.m_pData);
			Image.m_pData = pTmpData;
			// save png
			spng_ctx *pPng = spng_ctx_new(SPNG_CTX_ENCODER);
			Error = !pPng;
			if(!Error)
			{
				// save png
				spng_ihdr Info = {
					.width = (unsigned) NewWidth,
					.height = (unsigned) NewHeight,
					.bit_depth = 8,
					.color_type = ColorType,
					.compression_method = 0,
					.filter_method = 0,
					.interlace_method = 0};
				if(!(Error = spng_set_ihdr(pPng, &Info)))
				{
					spng_set_png_file(pPng, (FILE *) File);
					spng_encode_image(pPng, Image.m_pData, NewWidth * NewHeight * CImageInfo::GetPixelSize(Image.m_Format), SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
					io_close(File);
					File = 0;
				}
				spng_ctx_free(pPng);
			}
			if(File)
				io_close(File);
		}
		mem_free(Image.m_pData);
		m_pfnScreenshot = 0;
	}
}

void CGraphics_Threaded::TextureSet(CTextureHandle TextureID)
{
	dbg_assert(m_Drawing == 0, "called Graphics()->TextureSet within begin");
	// re-binding the same texture must not end the pending batch
	if(m_State.m_Texture == TextureID.Id())
		return;

	FlushPendingVerticesOnStateChange();
	m_State.m_Texture = TextureID.Id();
}

void CGraphics_Threaded::Clear(float r, float g, float b)
{
	FlushPendingVertices();

	CCommandBuffer::CClearCommand Cmd;
	Cmd.m_Color.r = r;
	Cmd.m_Color.g = g;
	Cmd.m_Color.b = b;
	Cmd.m_Color.a = 0;
	m_pCommandBuffer->AddCommand(Cmd);
}

void CGraphics_Threaded::QuadsBegin()
{
	dbg_assert(m_Drawing == 0, "called Graphics()->QuadsBegin twice");

	// switching primitive type cannot be expressed in a single render command
	if(m_NumVertices > 0 && m_PendingPrimType != DRAWING_QUADS)
		FlushVertices();

	m_Drawing = DRAWING_QUADS;
	m_PendingPrimType = DRAWING_QUADS;

	QuadsSetSubset(0, 0, 1, 1, -1);
	QuadsSetRotation(0);
	StainedOnly(false);
	SetColor(1, 1, 1, 1);

	// the SDF path is re-armed per begin block, so it cannot leak into other quads
	if(!m_SDFArmed)
		ClearTextSDF();
	m_SDFArmed = false;
}

void CGraphics_Threaded::QuadsEnd()
{
	dbg_assert(m_Drawing == DRAWING_QUADS, "called Graphics()->QuadsEnd without begin");
	// keep the vertices buffered so consecutive draws with the same state batch
	m_Drawing = 0;
}

void CGraphics_Threaded::QuadsSetRotation(float Angle)
{
	dbg_assert(m_Drawing == DRAWING_QUADS, "called Graphics()->QuadsSetRotation without begin");
	m_Rotation = Angle;
}

void CGraphics_Threaded::SetColorVertex(const CColorVertex *pArray, int Num)
{
	dbg_assert(m_Drawing != 0, "called Graphics()->SetColorVertex without begin");

	// The backends premultiply RGBA textures and the shader outputs premultiplied
	// fragments, so a global fade has to scale the color channels as well, otherwise
	// the textures would brighten while fading. Alpha-only and RGB textures are the
	// exception: their shader/blend path already applies the vertex alpha to the
	// color, so only the alpha channel may be scaled there.
	const int Texture = m_State.m_Texture;
	const int TextureFormat = (Texture >= 0 && Texture < MAX_TEXTURES) ? m_aTextureFormats[Texture] : CCommandBuffer::TEXFORMAT_INVALID;
	const bool AlphaAffectsColor = TextureFormat == CCommandBuffer::TEXFORMAT_ALPHA || TextureFormat == CCommandBuffer::TEXFORMAT_RGB;
	const float ColorScale = AlphaAffectsColor ? 1.0f : m_GlobalAlpha;

	for(int i = 0; i < Num; ++i)
	{
		m_aColor[pArray[i].m_Index].r = pArray[i].m_R * ColorScale;
		m_aColor[pArray[i].m_Index].g = pArray[i].m_G * ColorScale;
		m_aColor[pArray[i].m_Index].b = pArray[i].m_B * ColorScale;
		m_aColor[pArray[i].m_Index].a = pArray[i].m_A * m_GlobalAlpha;
	}
}

void CGraphics_Threaded::SetColor(float r, float g, float b, float a)
{
	dbg_assert(m_Drawing != 0, "called Graphics()->SetColor without begin");
	CColorVertex Array[4] = {
		CColorVertex(0, r, g, b, a),
		CColorVertex(1, r, g, b, a),
		CColorVertex(2, r, g, b, a),
		CColorVertex(3, r, g, b, a)};
	SetColorVertex(Array, 4);
}

void CGraphics_Threaded::SetColor4(const vec4 &TopLeft, const vec4 &TopRight, const vec4 &BottomLeft, const vec4 &BottomRight)
{
	dbg_assert(m_Drawing != 0, "called Graphics()->SetColor without begin");
	CColorVertex Array[4] = {
		CColorVertex(0, TopLeft.r, TopLeft.g, TopLeft.b, TopLeft.a),
		CColorVertex(1, TopRight.r, TopRight.g, TopRight.b, TopRight.a),
		CColorVertex(2, BottomRight.r, BottomRight.g, BottomRight.b, BottomRight.a),
		CColorVertex(3, BottomLeft.r, BottomLeft.g, BottomLeft.b, BottomLeft.a)};
	SetColorVertex(Array, 4);
}

void CGraphics_Threaded::SetGlobalAlpha(float Alpha)
{
	m_GlobalAlpha = clamp(Alpha, 0.0f, 1.0f);
}

float CGraphics_Threaded::GetGlobalAlpha() const
{
	return m_GlobalAlpha;
}

void CGraphics_Threaded::QuadsSetSubset(float TlU, float TlV, float BrU, float BrV, int TextureIndex)
{
	dbg_assert(m_Drawing == DRAWING_QUADS, "called Graphics()->QuadsSetSubset without begin");

	m_aTexture[0].u = TlU;
	m_aTexture[1].u = BrU;
	m_aTexture[0].v = TlV;
	m_aTexture[1].v = TlV;

	m_aTexture[3].u = TlU;
	m_aTexture[2].u = BrU;
	m_aTexture[3].v = BrV;
	m_aTexture[2].v = BrV;

	m_aTexture[0].i = m_aTexture[1].i = m_aTexture[2].i = m_aTexture[3].i = TextureIndex;
}

void CGraphics_Threaded::QuadsSetSubsetFree(
	float x0, float y0, float x1, float y1,
	float x2, float y2, float x3, float y3, int TextureIndex)
{
	m_aTexture[0].u = x0;
	m_aTexture[0].v = y0;
	m_aTexture[1].u = x1;
	m_aTexture[1].v = y1;
	m_aTexture[2].u = x2;
	m_aTexture[2].v = y2;
	m_aTexture[3].u = x3;
	m_aTexture[3].v = y3;

	m_aTexture[0].i = m_aTexture[1].i = m_aTexture[2].i = m_aTexture[3].i = TextureIndex;
}

void CGraphics_Threaded::QuadsDraw(CQuadItem *pArray, int Num)
{
	for(int i = 0; i < Num; ++i)
	{
		pArray[i].m_X -= pArray[i].m_Width / 2;
		pArray[i].m_Y -= pArray[i].m_Height / 2;
	}

	QuadsDrawTL(pArray, Num);
}

void CGraphics_Threaded::SingleQuadDrawTL(const CQuadItem *pQuad)
{
	QuadsDrawTL(pQuad, 1);
}

void CGraphics_Threaded::QuadsDrawTL(const CQuadItem *pArray, int Num)
{
	CCommandBuffer::CPoint Center;

	dbg_assert(m_Drawing == DRAWING_QUADS, "called Graphics()->QuadsDrawTL without begin");

	ReserveVertices(4 * Num);

	for(int i = 0; i < Num; ++i)
	{
		m_aVertices[m_NumVertices + 4 * i].m_Pos.x = pArray[i].m_X;
		m_aVertices[m_NumVertices + 4 * i].m_Pos.y = pArray[i].m_Y;
		m_aVertices[m_NumVertices + 4 * i].m_Tex = m_aTexture[0];
		m_aVertices[m_NumVertices + 4 * i].m_Color = m_aColor[0];

		m_aVertices[m_NumVertices + 4 * i + 1].m_Pos.x = pArray[i].m_X + pArray[i].m_Width;
		m_aVertices[m_NumVertices + 4 * i + 1].m_Pos.y = pArray[i].m_Y;
		m_aVertices[m_NumVertices + 4 * i + 1].m_Tex = m_aTexture[1];
		m_aVertices[m_NumVertices + 4 * i + 1].m_Color = m_aColor[1];

		m_aVertices[m_NumVertices + 4 * i + 2].m_Pos.x = pArray[i].m_X + pArray[i].m_Width;
		m_aVertices[m_NumVertices + 4 * i + 2].m_Pos.y = pArray[i].m_Y + pArray[i].m_Height;
		m_aVertices[m_NumVertices + 4 * i + 2].m_Tex = m_aTexture[2];
		m_aVertices[m_NumVertices + 4 * i + 2].m_Color = m_aColor[2];

		m_aVertices[m_NumVertices + 4 * i + 3].m_Pos.x = pArray[i].m_X;
		m_aVertices[m_NumVertices + 4 * i + 3].m_Pos.y = pArray[i].m_Y + pArray[i].m_Height;
		m_aVertices[m_NumVertices + 4 * i + 3].m_Tex = m_aTexture[3];
		m_aVertices[m_NumVertices + 4 * i + 3].m_Color = m_aColor[3];

		if(m_Rotation != 0)
		{
			Center.x = pArray[i].m_X + pArray[i].m_Width / 2;
			Center.y = pArray[i].m_Y + pArray[i].m_Height / 2;

			Rotate4(Center, &m_aVertices[m_NumVertices + 4 * i]);
		}
	}

	AddVertices(4 * Num);
}

void CGraphics_Threaded::QuadsDrawTLWithUV(const CQuadItem *pArray, const vec4 *pUV, int Num, int TextureIndex)
{
	CCommandBuffer::CPoint Center;

	dbg_assert(m_Drawing == DRAWING_QUADS, "called Graphics()->QuadsDrawTLWithUV without begin");

	ReserveVertices(4 * Num);

	const float Layer = (float) TextureIndex;

	for(int i = 0; i < Num; ++i)
	{
		CCommandBuffer::CVertex *pVertices = &m_aVertices[m_NumVertices + 4 * i];
		const vec4 &rUV = pUV[i];

		pVertices[0].m_Pos.x = pArray[i].m_X;
		pVertices[0].m_Pos.y = pArray[i].m_Y;
		pVertices[0].m_Tex.u = rUV.x;
		pVertices[0].m_Tex.v = rUV.y;
		pVertices[0].m_Tex.i = Layer;
		pVertices[0].m_Color = m_aColor[0];

		pVertices[1].m_Pos.x = pArray[i].m_X + pArray[i].m_Width;
		pVertices[1].m_Pos.y = pArray[i].m_Y;
		pVertices[1].m_Tex.u = rUV.z;
		pVertices[1].m_Tex.v = rUV.y;
		pVertices[1].m_Tex.i = Layer;
		pVertices[1].m_Color = m_aColor[1];

		pVertices[2].m_Pos.x = pArray[i].m_X + pArray[i].m_Width;
		pVertices[2].m_Pos.y = pArray[i].m_Y + pArray[i].m_Height;
		pVertices[2].m_Tex.u = rUV.z;
		pVertices[2].m_Tex.v = rUV.w;
		pVertices[2].m_Tex.i = Layer;
		pVertices[2].m_Color = m_aColor[2];

		pVertices[3].m_Pos.x = pArray[i].m_X;
		pVertices[3].m_Pos.y = pArray[i].m_Y + pArray[i].m_Height;
		pVertices[3].m_Tex.u = rUV.x;
		pVertices[3].m_Tex.v = rUV.w;
		pVertices[3].m_Tex.i = Layer;
		pVertices[3].m_Color = m_aColor[3];

		if(m_Rotation != 0)
		{
			Center.x = pArray[i].m_X + pArray[i].m_Width / 2;
			Center.y = pArray[i].m_Y + pArray[i].m_Height / 2;

			Rotate4(Center, pVertices);
		}
	}

	AddVertices(4 * Num);
}

void CGraphics_Threaded::QuadsDrawFreeform(const CFreeformItem *pArray, int Num)
{
	dbg_assert(m_Drawing == DRAWING_QUADS, "called Graphics()->QuadsDrawFreeform without begin");

	ReserveVertices(4 * Num);

	for(int i = 0; i < Num; ++i)
	{
		m_aVertices[m_NumVertices + 4 * i].m_Pos.x = pArray[i].m_X0;
		m_aVertices[m_NumVertices + 4 * i].m_Pos.y = pArray[i].m_Y0;
		m_aVertices[m_NumVertices + 4 * i].m_Tex = m_aTexture[0];
		m_aVertices[m_NumVertices + 4 * i].m_Color = m_aColor[0];

		m_aVertices[m_NumVertices + 4 * i + 1].m_Pos.x = pArray[i].m_X1;
		m_aVertices[m_NumVertices + 4 * i + 1].m_Pos.y = pArray[i].m_Y1;
		m_aVertices[m_NumVertices + 4 * i + 1].m_Tex = m_aTexture[1];
		m_aVertices[m_NumVertices + 4 * i + 1].m_Color = m_aColor[1];

		m_aVertices[m_NumVertices + 4 * i + 2].m_Pos.x = pArray[i].m_X3;
		m_aVertices[m_NumVertices + 4 * i + 2].m_Pos.y = pArray[i].m_Y3;
		m_aVertices[m_NumVertices + 4 * i + 2].m_Tex = m_aTexture[3];
		m_aVertices[m_NumVertices + 4 * i + 2].m_Color = m_aColor[3];

		m_aVertices[m_NumVertices + 4 * i + 3].m_Pos.x = pArray[i].m_X2;
		m_aVertices[m_NumVertices + 4 * i + 3].m_Pos.y = pArray[i].m_Y2;
		m_aVertices[m_NumVertices + 4 * i + 3].m_Tex = m_aTexture[2];
		m_aVertices[m_NumVertices + 4 * i + 3].m_Color = m_aColor[2];
	}

	AddVertices(4 * Num);
}

void CGraphics_Threaded::QuadsText(float x, float y, float Size, const char *pText)
{
	float StartX = x;

	while(*pText)
	{
		char c = *pText;
		pText++;

		if(c == '\n')
		{
			x = StartX;
			y += Size;
		}
		else
		{
			QuadsSetSubset(
				(c % 16) / 16.0f,
				(c / 16) / 16.0f,
				(c % 16) / 16.0f + 1.0f / 16.0f,
				(c / 16) / 16.0f + 1.0f / 16.0f);

			CQuadItem QuadItem(x, y, Size, Size);
			QuadsDrawTL(&QuadItem, 1);
			x += Size / 2;
		}
	}
}

int CGraphics_Threaded::IssueInit()
{
	int Flags = 0;
	if(m_pConfig->m_GfxBorderless)
		Flags |= IGraphicsBackend::INITFLAG_BORDERLESS;
	if(m_pConfig->m_GfxFullscreen)
		Flags |= IGraphicsBackend::INITFLAG_FULLSCREEN;
	if(m_pConfig->m_GfxVsync)
		Flags |= IGraphicsBackend::INITFLAG_VSYNC;
	if(m_pConfig->m_GfxHighdpi)
		Flags |= IGraphicsBackend::INITFLAG_HIGHDPI;
	if(m_pConfig->m_DbgResizable)
		Flags |= IGraphicsBackend::INITFLAG_RESIZABLE;
	if(m_pConfig->m_GfxUseX11XRandRWM)
		Flags |= IGraphicsBackend::INITFLAG_X11XRANDR;

	return m_pBackend->Init("Teeworlds: Archive", &m_pConfig->m_GfxScreen, &m_pConfig->m_GfxScreenWidth,
		&m_pConfig->m_GfxScreenHeight, &m_ScreenWidth, &m_ScreenHeight, m_pConfig->m_GfxFsaaSamples,
		Flags, &m_DesktopScreenWidth, &m_DesktopScreenHeight);
}

int CGraphics_Threaded::InitWindow()
{
	if(IssueInit() == 0)
		return 0;

	// try disabling fsaa
	while(m_pConfig->m_GfxFsaaSamples)
	{
		m_pConfig->m_GfxFsaaSamples--;

		if(m_pConfig->m_GfxFsaaSamples)
			dbg_msg("gfx", "lowering FSAA to %d and trying again", m_pConfig->m_GfxFsaaSamples);
		else
			dbg_msg("gfx", "disabling FSAA and trying again");

		if(IssueInit() == 0)
			return 0;
	}

	// try lowering the resolution
	if(m_pConfig->m_GfxScreenWidth != 640 || m_pConfig->m_GfxScreenHeight != 480)
	{
		dbg_msg("gfx", "setting resolution to 640x480 and trying again");
		m_pConfig->m_GfxScreenWidth = 640;
		m_pConfig->m_GfxScreenHeight = 480;

		if(IssueInit() == 0)
			return 0;
	}

	dbg_msg("gfx", "out of ideas. failed to init graphics");

	return -1;
}

int CGraphics_Threaded::Init()
{
	// fetch pointers
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pConfig = Kernel()->RequestInterface<IConfigManager>()->Values();
	m_pConsole = Kernel()->RequestInterface<IConsole>();

	// init textures
	m_FirstFreeTexture = 0;
	for(int i = 0; i < MAX_TEXTURES - 1; i++)
		m_aTextureIndices[i] = i + 1;
	m_aTextureIndices[MAX_TEXTURES - 1] = -1;
	for(int i = 0; i < MAX_TEXTURES; i++)
		m_aTextureFormats[i] = CCommandBuffer::TEXFORMAT_INVALID;

	m_pBackend = CreateGraphicsBackend(m_pStorage, m_pConfig->m_GfxBackend == 1 ? 1 : 2);
	// SDL_GPU has no fallback settings to retry with, so only try once
	int InitResult = m_pConfig->m_GfxBackend == 1 ? InitWindow() : IssueInit();
	if(InitResult != 0)
	{
		if(m_pConfig->m_GfxBackend == 0)
		{
			dbg_msg("gfx", "SDL_GPU backend failed, falling back to OpenGL ES");
			delete m_pBackend;
			m_pBackend = CreateGraphicsBackend(m_pStorage, 1);
			InitResult = InitWindow();
		}
		if(InitResult != 0)
			return -1;
	}

	m_ScreenHiDPIScale = m_ScreenWidth / (float) m_pConfig->m_GfxScreenWidth;
	m_ScreenUIScale = (m_pConfig->m_GfxScreenHeight < 900.0f) ? 1.0f : (m_pConfig->m_GfxScreenHeight / 900.0f);

	// create command buffers
	for(int i = 0; i < NUM_CMDBUFFERS; i++)
		m_apCommandBuffers[i] = new CCommandBuffer(128 * 1024, 2 * 1024 * 1024);
	m_pCommandBuffer = m_apCommandBuffers[0];

	// create null texture, will get id=0
	unsigned char aNullTextureData[4 * 32 * 32];
	for(int x = 0; x < 32; ++x)
		for(int y = 0; y < 32; ++y)
		{
			if(x < 16)
			{
				if(y < 16)
				{
					aNullTextureData[4 * (y * 32 + x) + 0] = y * 8 + x * 8 + 15;
					aNullTextureData[4 * (y * 32 + x) + 1] = 0;
					aNullTextureData[4 * (y * 32 + x) + 2] = 0;
				}
				else
				{
					aNullTextureData[4 * (y * 32 + x) + 0] = 0;
					aNullTextureData[4 * (y * 32 + x) + 1] = y * 8 + x * 8 - 113;
					aNullTextureData[4 * (y * 32 + x) + 2] = 0;
				}
			}
			else
			{
				if(y < 16)
				{
					aNullTextureData[4 * (y * 32 + x) + 0] = 0;
					aNullTextureData[4 * (y * 32 + x) + 1] = 0;
					aNullTextureData[4 * (y * 32 + x) + 2] = y * 8 + x * 8 - 113;
				}
				else
				{
					aNullTextureData[4 * (y * 32 + x) + 0] = y * 8 + x * 8 - 496;
					aNullTextureData[4 * (y * 32 + x) + 1] = y * 8 + x * 8 - 496;
					aNullTextureData[4 * (y * 32 + x) + 2] = 0;
				}
			}
			aNullTextureData[4 * (y * 32 + x) + 3] = 255;
		}

	m_InvalidTexture = LoadTextureRaw(32, 32, 1, CImageInfo::FORMAT_RGBA, aNullTextureData, CImageInfo::FORMAT_RGBA, TEXLOAD_NORESAMPLE);
	return 0;
}

void CGraphics_Threaded::Shutdown()
{
	// shutdown the backend
	m_pBackend->Shutdown();
	delete m_pBackend;
	m_pBackend = 0x0;

	// delete the command buffers
	for(int i = 0; i < NUM_CMDBUFFERS; i++)
		delete m_apCommandBuffers[i];
}

int CGraphics_Threaded::GetNumScreens() const
{
	return m_pBackend->GetNumScreens();
}

void CGraphics_Threaded::Minimize()
{
	m_pBackend->Minimize();
}

void CGraphics_Threaded::Maximize()
{
	m_pBackend->Maximize();
}

bool CGraphics_Threaded::Fullscreen(bool State)
{
	return m_pBackend->Fullscreen(State);
}

void CGraphics_Threaded::SetWindowBordered(bool State)
{
	m_pBackend->SetWindowBordered(State);
}

bool CGraphics_Threaded::SetWindowScreen(int Index)
{
	if(m_pBackend->SetWindowScreen(Index))
	{
		// update resolution info
		m_pBackend->GetDesktopResolution(Index, &m_DesktopScreenWidth, &m_DesktopScreenHeight);
		return true;
	}
	return false;
}

int CGraphics_Threaded::GetWindowScreen()
{
	return m_pBackend->GetWindowScreen();
}

bool CGraphics_Threaded::WindowActive()
{
	return m_pBackend->WindowActive();
}

bool CGraphics_Threaded::WindowOpen()
{
	return m_pBackend->WindowOpen();
}

void CGraphics_Threaded::ReadBackbuffer(unsigned char **ppPixels, int x, int y, int w, int h)
{
	if(!ppPixels)
		return;

	FlushPendingVertices();

	// add swap command
	CImageInfo Image;
	mem_zero(&Image, sizeof(Image));

	CCommandBuffer::CScreenshotCommand Cmd;
	Cmd.m_pImage = &Image;
	Cmd.m_X = x;
	Cmd.m_Y = y;
	Cmd.m_W = w;
	Cmd.m_H = h;
	m_pCommandBuffer->AddCommand(Cmd);

	// kick the buffer and wait for the result
	KickCommandBuffer();
	WaitForIdle();

	*ppPixels = (unsigned char *) Image.m_pData; // take ownership!
}

void CGraphics_Threaded::TakeScreenshot(const char *pFilename, FScreenshotCallback pfnCallback, void *pUser)
{
	// TODO: screenshot support
	char aDate[20];
	str_timestamp(aDate, sizeof(aDate));
	str_format(m_aScreenshotName, sizeof(m_aScreenshotName), "screenshots/%s_%s.png", pFilename ? pFilename : "screenshot", aDate);
	str_format(m_aThumbnailName, sizeof(m_aThumbnailName), "screenshots/%s_%s_thumbnail.png", pFilename ? pFilename : "screenshot", aDate);
	m_DoScreenshot = true;
	m_pfnScreenshot = pfnCallback;
	m_pScreenshotUser = pUser;
}

void CGraphics_Threaded::Swap()
{
	// pending vertices must reach the command buffer before the swap command
	FlushPendingVertices();

	// TODO: screenshot support
	if(m_DoScreenshot)
	{
		if(WindowActive())
			ScreenshotDirect(m_aScreenshotName, m_aThumbnailName);
		m_DoScreenshot = false;
	}

	// add swap command
	CCommandBuffer::CSwapCommand Cmd;
	Cmd.m_Finish = m_pConfig->m_GfxFinish;
	m_pCommandBuffer->AddCommand(Cmd);
	m_RenderedFrameCount++;

	// kick the command buffer
	KickCommandBuffer();
}

bool CGraphics_Threaded::SetVSync(bool State)
{
	FlushPendingVertices();

	// add vsnc command
	bool RetOk = 0;
	CCommandBuffer::CVSyncCommand Cmd;
	Cmd.m_VSync = State ? 1 : 0;
	Cmd.m_pRetOk = &RetOk;
	m_pCommandBuffer->AddCommand(Cmd);

	// kick the command buffer
	KickCommandBuffer();
	WaitForIdle();
	return RetOk;
}

// syncronization
void CGraphics_Threaded::InsertSignal(semaphore *pSemaphore)
{
	FlushPendingVertices();

	CCommandBuffer::CSignalCommand Cmd;
	Cmd.m_pSemaphore = pSemaphore;
	m_pCommandBuffer->AddCommand(Cmd);
}

bool CGraphics_Threaded::IsIdle() const
{
	return m_pBackend->IsIdle();
}

void CGraphics_Threaded::WaitForIdle()
{
	m_pBackend->WaitForIdle();
}

bool CGraphics_Threaded::ResizeWindow(int Width, int Height)
{
	// jumped from fullscreen
	if(m_pConfig->m_GfxFullscreen)
	{
		m_pConfig->m_GfxFullscreen = false;
		Fullscreen(false);
	}

	return m_pBackend->ResizeWindow(Width, Height);
}

void CGraphics_Threaded::OnWindowResized(int Width, int Height)
{
	m_pConfig->m_GfxScreenWidth = Width;
	m_pConfig->m_GfxScreenHeight = Height;
	m_ScreenUIScale = (m_pConfig->m_GfxScreenHeight < 900.0f) ? 1.0f : (m_pConfig->m_GfxScreenHeight / 900.0f);

	FlushPendingVertices();

	// add window resize command
	CCommandBuffer::CWindowResizedCommand Cmd;
	Cmd.m_Width = Width;
	Cmd.m_Height = Height;
	m_pCommandBuffer->AddCommand(Cmd);
}

void CGraphics_Threaded::OnWindowPixelResized(int ScreenWidth, int ScreenHeight)
{
	m_ScreenWidth = ScreenWidth;
	m_ScreenHeight = ScreenHeight;
	m_ScreenHiDPIScale = m_ScreenWidth / (float) m_pConfig->m_GfxScreenWidth;
}

void *CGraphics_Threaded::GetWindowHandle()
{
	return m_pBackend->GetWindowHandle();
}

int CGraphics_Threaded::GetVideoModes(CVideoMode *pModes, int MaxModes, int Screen)
{
	if(m_pConfig->m_GfxDisplayAllModes)
	{
		int Count = minimum((int) (sizeof(g_aFakeModes) / sizeof(CVideoMode)), MaxModes);
		mem_copy(pModes, g_aFakeModes, sizeof(CVideoMode) * Count);
		return Count;
	}

	return m_pBackend->GetVideoModes(pModes, MaxModes, Screen);
}

extern IEngineGraphics *CreateEngineGraphicsThreaded()
{
#ifdef CONF_HEADLESS_CLIENT
	return new CGraphics_ThreadedNull();
#else
	return new CGraphics_Threaded();
#endif
}
