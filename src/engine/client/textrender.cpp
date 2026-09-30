/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#include <base/math.h>
#include <base/system.h>

#include <math.h>

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <engine/shared/jsonparser.h>
#include <engine/shared/config.h>

#include "textrender.h"

// false once the glyph map is gone; static cursors may still be destroyed later
static bool g_GlyphsAlive = false;

static void ReleaseScaledGlyph(const CScaledGlyph &rGlyph)
{
	if(g_GlyphsAlive && rGlyph.m_pGlyph)
		rGlyph.m_pGlyph->m_RefCount--;
}

// installed on a cursor so the glyph cache knows which records are referenced
static void ReleaseGlyphRefs(void *pUser, CScaledGlyph *pGlyphs, int NumGlyphs)
{
	(void) pUser;
	if(!g_GlyphsAlive)
		return;
	for(int i = 0; i < NumGlyphs; ++i)
	{
		if(pGlyphs[i].m_pGlyph)
			pGlyphs[i].m_pGlyph->m_RefCount--;
	}
}

static unsigned long long HashBytes(unsigned long long Hash, const void *pData, int Size)
{
	const unsigned char *pBytes = (const unsigned char *) pData;
	for(int i = 0; i < Size; ++i)
	{
		Hash ^= pBytes[i];
		Hash *= 1099511628211ull;
	}
	return Hash;
}

// Signed distance field generation. FreeType's own SDF renderer is far too slow
// to use per glyph, so the glyph is rendered supersampled and the field is
// derived from the coverage bitmap with an exact euclidean distance transform.
// ---------------------------------------------------------------------------

static const float SDF_DISTANCE_INFINITY = 1.0e20f;

// Felzenszwalb & Huttenlocher exact squared euclidean distance transform of one line
static void SdfDistanceTransform1D(const float *pF, float *pD, int N, int *pV, float *pZ)
{
	int k = 0;
	pV[0] = 0;
	pZ[0] = -SDF_DISTANCE_INFINITY;
	pZ[1] = SDF_DISTANCE_INFINITY;
	for(int q = 1; q < N; ++q)
	{
		float s = ((pF[q] + (float) (q * q)) - (pF[pV[k]] + (float) (pV[k] * pV[k]))) / (2.0f * q - 2.0f * pV[k]);
		while(s <= pZ[k])
		{
			k--;
			s = ((pF[q] + (float) (q * q)) - (pF[pV[k]] + (float) (pV[k] * pV[k]))) / (2.0f * q - 2.0f * pV[k]);
		}
		k++;
		pV[k] = q;
		pZ[k] = s;
		pZ[k + 1] = SDF_DISTANCE_INFINITY;
	}

	k = 0;
	for(int q = 0; q < N; ++q)
	{
		while(pZ[k + 1] < (float) q)
			k++;
		const int d = q - pV[k];
		pD[q] = (float) (d * d) + pF[pV[k]];
	}
}

// in place 2D transform; the scratch buffers need max(W, H) + 1 entries
static void SdfDistanceTransform2D(float *pGrid, int W, int H, float *pLine, float *pOut, int *pV, float *pZ)
{
	for(int y = 0; y < H; ++y)
	{
		float *pRow = pGrid + (size_t) y * W;
		SdfDistanceTransform1D(pRow, pLine, W, pV, pZ);
		mem_copy(pRow, pLine, sizeof(float) * W);
	}

	for(int x = 0; x < W; ++x)
	{
		for(int y = 0; y < H; ++y)
			pLine[y] = pGrid[(size_t) y * W + x];
		SdfDistanceTransform1D(pLine, pOut, H, pV, pZ);
		for(int y = 0; y < H; ++y)
			pGrid[(size_t) y * W + x] = pOut[y];
	}
}

static float SdfBilinear(const float *pGrid, int W, int H, float X, float Y)
{
	if(X < 0.0f)
		X = 0.0f;
	if(Y < 0.0f)
		Y = 0.0f;
	if(X > W - 1)
		X = W - 1;
	if(Y > H - 1)
		Y = H - 1;

	const int X0 = (int) X;
	const int Y0 = (int) Y;
	const int X1 = X0 + 1 < W ? X0 + 1 : X0;
	const int Y1 = Y0 + 1 < H ? Y0 + 1 : Y0;
	const float Fx = X - X0;
	const float Fy = Y - Y0;

	const float A = pGrid[(size_t) Y0 * W + X0];
	const float B = pGrid[(size_t) Y0 * W + X1];
	const float C = pGrid[(size_t) Y1 * W + X0];
	const float D = pGrid[(size_t) Y1 * W + X1];
	return (A * (1.0f - Fx) + B * Fx) * (1.0f - Fy) + (C * (1.0f - Fx) + D * Fx) * Fy;
}

// makes sure the scratch buffers can hold NumFloats floats and NumInts ints
static bool SdfScratchEnsure(CSdfScratch *pScratch, int NumFloats, int NumInts)
{
	if(pScratch->m_NumFloats < NumFloats)
	{
		if(pScratch->m_pFloats)
			mem_free(pScratch->m_pFloats);
		pScratch->m_pFloats = (float *) mem_alloc(sizeof(float) * NumFloats);
		pScratch->m_NumFloats = pScratch->m_pFloats ? NumFloats : 0;
	}
	if(pScratch->m_NumInts < NumInts)
	{
		if(pScratch->m_pInts)
			mem_free(pScratch->m_pInts);
		pScratch->m_pInts = (int *) mem_alloc(sizeof(int) * NumInts);
		pScratch->m_NumInts = pScratch->m_pInts ? NumInts : 0;
	}
	return pScratch->m_NumFloats >= NumFloats && pScratch->m_NumInts >= NumInts;
}

// Renders the glyph at PixelSize * SDF_SUPERSAMPLE and derives the distance field
// from the coverage bitmap. On success *ppData holds a *pWidth x *pHeight byte
// bitmap with SDF_SPREAD texels of padding, encoded like FreeType's SDF renderer.
static bool GenerateGlyphSDF(FT_Face pFace, int GlyphIndex, int PixelSize, int Spread,
	unsigned char **ppData, int *pWidth, int *pHeight, float *pLeft, float *pTop,
	CSdfScratch *pScratch)
{
	if(FT_Set_Pixel_Sizes(pFace, 0, PixelSize * SDF_SUPERSAMPLE))
		return false;

	// no hinting: the field is sampled at every display size and must stay
	// size independent
	if(FT_Load_Glyph(pFace, GlyphIndex, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING))
		return false;

	if(FT_Render_Glyph(pFace->glyph, FT_RENDER_MODE_NORMAL))
		return false;

	const FT_Bitmap *pBitmap = &pFace->glyph->bitmap;
	const int SrcW = pBitmap->width;
	const int SrcH = pBitmap->rows;
	if(SrcW <= 0 || SrcH <= 0 || pBitmap->pixel_mode != FT_PIXEL_MODE_GRAY)
		return false;

	// the distance is also needed outside the glyph, so the padding is part of the grid
	const int Pad = Spread * SDF_SUPERSAMPLE + SDF_SUPERSAMPLE;
	const int GridW = SrcW + Pad * 2;
	const int GridH = SrcH + Pad * 2;
	const size_t GridSize = (size_t) GridW * GridH;
	const int LineMax = (GridW > GridH ? GridW : GridH) + 1;

	const size_t NumFloats = GridSize * 3 + (size_t) LineMax * 3;
	if(NumFloats > (size_t) INT_MAX / 2 || !SdfScratchEnsure(pScratch, (int) NumFloats, LineMax))
		return false;

	float *pDistOut = pScratch->m_pFloats;
	float *pDistIn = pDistOut + GridSize;
	float *pSigned = pDistIn + GridSize;
	float *pLine = pSigned + GridSize;
	float *pOut = pLine + LineMax;
	float *pZ = pOut + LineMax;
	int *pV = pScratch->m_pInts;

	for(int y = 0; y < GridH; ++y)
	{
		for(int x = 0; x < GridW; ++x)
		{
			const int SrcX = x - Pad;
			const int SrcY = y - Pad;
			float Coverage = 0.0f;
			if(SrcX >= 0 && SrcX < SrcW && SrcY >= 0 && SrcY < SrcH)
				Coverage = pBitmap->buffer[(size_t) SrcY * pBitmap->pitch + SrcX] * (1.0f / 255.0f);
			const bool Inside = Coverage >= 0.5f;

			// pDistOut is zero outside the glyph, pDistIn inside; their root
			// difference is the signed distance
			pDistOut[(size_t) y * GridW + x] = Inside ? SDF_DISTANCE_INFINITY : 0.0f;
			pDistIn[(size_t) y * GridW + x] = Inside ? 0.0f : SDF_DISTANCE_INFINITY;
			// the coverage refines the outline below
			pSigned[(size_t) y * GridW + x] = Coverage;
		}
	}

	SdfDistanceTransform2D(pDistOut, GridW, GridH, pLine, pOut, pV, pZ);
	SdfDistanceTransform2D(pDistIn, GridW, GridH, pLine, pOut, pV, pZ);

	for(size_t i = 0; i < GridSize; ++i)
	{
		float Distance = sqrtf(pDistOut[i]) - sqrtf(pDistIn[i]);

		// the transform measures between pixel centres, so shift the outline half
		// a pixel to give the field a unit gradient
		if(Distance > 0.0f)
			Distance -= 0.5f;
		else if(Distance < 0.0f)
			Distance += 0.5f;

		// pixels the outline crosses know their coverage; use it for sub-texel accuracy
		const float Coverage = pSigned[i];
		if(Coverage > 0.0f && Coverage < 1.0f)
			Distance = Coverage - 0.5f;

		pSigned[i] = Distance;
	}

	const float Supersample = (float) SDF_SUPERSAMPLE;
	const int Width = (SrcW + SDF_SUPERSAMPLE - 1) / SDF_SUPERSAMPLE + Spread * 2;
	const int Height = (SrcH + SDF_SUPERSAMPLE - 1) / SDF_SUPERSAMPLE + Spread * 2;

	unsigned char *pData = (unsigned char *) mem_alloc((size_t) Width * Height);
	if(!pData)
		return false;

	for(int y = 0; y < Height; ++y)
	{
		const float SampleY = ((float) (y - Spread) + 0.5f) * Supersample - 0.5f + Pad;
		for(int x = 0; x < Width; ++x)
		{
			const float SampleX = ((float) (x - Spread) + 0.5f) * Supersample - 0.5f + Pad;
			const float Distance = SdfBilinear(pSigned, GridW, GridH, SampleX, SampleY) / Supersample;
			int Value = (int) (128.0f + 128.0f * Distance / (float) Spread + 0.5f);
			if(Value < 0)
				Value = 0;
			if(Value > 255)
				Value = 255;
			pData[(size_t) y * Width + x] = (unsigned char) Value;
		}
	}

	*ppData = pData;
	*pWidth = Width;
	*pHeight = Height;
	*pLeft = (float) pFace->glyph->bitmap_left / Supersample;
	*pTop = (float) pFace->glyph->bitmap_top / Supersample;
	return true;
}

int CGlyphMap::CAtlas::TrySection(int Index, int Width, int Height)
{
	ivec3 Section = m_Sections[Index];
	int CurX = Section.x;
	int CurY = Section.y;

	int FitWidth = Width;

	if(CurX + Width > m_Width - 1)
		return -1;

	for(int i = Index; i < m_Sections.size(); ++i)
	{
		if(FitWidth <= 0)
			break;

		Section = m_Sections[i];
		if(Section.y > CurY)
			CurY = Section.y;
		if(CurY + Height > m_Height - 1)
			return -1;
		FitWidth -= Section.l;
	}

	return CurY;
}

void CGlyphMap::CAtlas::Init(int Index, int X, int Y, int Width, int Height)
{
	m_Offset.x = X;
	m_Offset.y = Y;
	m_Width = Width;
	m_Height = Height;

	m_ID = Index;
	m_Sections.clear();

	ivec3 Section;
	Section.x = 1;
	Section.y = 1;
	Section.l = m_Width - 2;
	m_Sections.add(Section);

	m_IsEmpty = true;
	// m_Dirty stays untouched: Init() also recycles pages, and the pending clear
	// upload has to survive it
}

void CGlyphMap::CAtlas::MarkDirty(int X0, int Y0, int X1, int Y1)
{
	// coordinates are relative to the page
	if(!m_Dirty)
	{
		m_DirtyX0 = X0;
		m_DirtyY0 = Y0;
		m_DirtyX1 = X1;
		m_DirtyY1 = Y1;
		m_Dirty = true;
		return;
	}
	m_DirtyX0 = minimum(m_DirtyX0, X0);
	m_DirtyY0 = minimum(m_DirtyY0, Y0);
	m_DirtyX1 = maximum(m_DirtyX1, X1);
	m_DirtyY1 = maximum(m_DirtyY1, Y1);
}

ivec2 CGlyphMap::CAtlas::Add(int Width, int Height)
{
	m_IsEmpty = false;
	int BestHeight = m_Height;
	int BestWidth = m_Width;
	int BestSectionIndex = -1;

	ivec2 Position;

	for(int i = 0; i < m_Sections.size(); ++i)
	{
		int y = TrySection(i, Width, Height);
		if(y >= 0)
		{
			ivec3 Section = m_Sections[i];
			int NewHeight = y + Height;
			if((NewHeight < BestHeight) || ((NewHeight == BestHeight) && (Section.l > 0 && Section.l < BestWidth)))
			{
				BestHeight = NewHeight;
				BestWidth = Section.l;
				BestSectionIndex = i;
				Position.x = Section.x;
				Position.y = y;
			}
		}
	}

	if(BestSectionIndex < 0)
	{
		Position.x = -1;
		Position.y = -1;
		return Position;
	}

	ivec3 NewSection;
	NewSection.x = Position.x;
	NewSection.y = Position.y + Height;
	NewSection.l = Width;
	m_Sections.insert(NewSection, m_Sections.all().slice(BestSectionIndex, BestSectionIndex + 1));

	for(int i = BestSectionIndex + 1; i < m_Sections.size(); ++i)
	{
		ivec3 *Section = &m_Sections[i];
		ivec3 *Previous = &m_Sections[i - 1];

		if(Section->x >= Previous->x + Previous->l)
			break;

		int Shrink = Previous->x + Previous->l - Section->x;
		Section->x += Shrink;
		Section->l -= Shrink;
		if(Section->l > 0)
			break;

		m_Sections.remove_index(i);
		i -= 1;
	}

	for(int i = 0; i < m_Sections.size() - 1; ++i)
	{
		ivec3 *Section = &m_Sections[i];
		ivec3 *Next = &m_Sections[i + 1];
		if(Section->y == Next->y)
		{
			Section->l += Next->l;
			m_Sections.remove_index(i + 1);
			i -= 1;
		}
	}

	m_Access++;
	return Position + m_Offset;
}

static int AdjustOutlineThicknessToFontSize(int OutlineThickness, int FontSize)
{
	if(FontSize > 64)
		OutlineThickness *= 5;
	else if(FontSize > 40)
		OutlineThickness *= 4;
	else if(FontSize >= 18)
		OutlineThickness *= 2;
	return OutlineThickness;
}

static void InvalidateGlyphs(CGlyph *&pGlyph, void *pUser)
{
	pGlyph->m_Rendered = false;
	pGlyph->m_Pending = false;
}

void CGlyphMap::InitTexture(int Width, int Height)
{
	m_TextureSize = Width;
	m_PageSize = Width / NUM_PAGES_PER_DIM;

	for(int y = 0; y < NUM_PAGES_PER_DIM; ++y)
	{
		for(int x = 0; x < NUM_PAGES_PER_DIM; ++x)
		{
			CAtlas &Page = m_aAtlasPages[y * NUM_PAGES_PER_DIM + x];
			Page.Init(m_NumTotalPages++, x * m_PageSize, y * m_PageSize, m_PageSize, m_PageSize);
			// the whole texture is recreated below, so nothing is pending
			Page.m_Dirty = false;
		}
	}
	m_ActiveAtlasIndex = 0;

	// invalidate all glyphs
	m_Glyphs.for_each(InvalidateGlyphs, 0);

	// queued glyphs have to be rasterized again as well
	m_PendingGlyphs.clear_size();
	m_PendingBudgetSpent = false;

	// single channel, single layer distance field atlas
	int TextureSize = Width * Height;

	// the mirror has to match the (runtime chosen) atlas size
	if(m_pStaging)
		mem_free(m_pStaging);
	m_pStaging = (unsigned char *) mem_alloc(TextureSize);
	mem_zero(m_pStaging, TextureSize);

	if(m_Texture.IsValid())
		m_pGraphics->UnloadTexture(&m_Texture);

	// the zeroed mirror doubles as the initial texture data
	m_Texture = m_pGraphics->LoadTextureRaw(Width, Height, 1, CImageInfo::FORMAT_ALPHA, m_pStaging, CImageInfo::FORMAT_ALPHA, IGraphics::TEXLOAD_NOMIPMAPS);
	dbg_msg("textrender", "atlas: %dx%d (%d pages of %d), memory usage: %d", Width, Height, NUM_PAGES_PER_DIM * NUM_PAGES_PER_DIM, m_PageSize, TextureSize);
}

void CGlyphMap::UploadGlyph(int TextureIndex, int PosX, int PosY, int Width, int Height, const unsigned char *pData)
{
	// blit into the CPU mirror; FlushUploads() uploads one batch per page
	for(int y = 0; y < Height; ++y)
		mem_copy(m_pStaging + (PosY + y) * m_TextureSize + PosX, pData + (size_t) y * Width, Width);

	const int PageX = PosX / m_PageSize;
	const int PageY = PosY / m_PageSize;
	if(PageX >= 0 && PageX < NUM_PAGES_PER_DIM && PageY >= 0 && PageY < NUM_PAGES_PER_DIM)
	{
		const int PageIndex = PageY * NUM_PAGES_PER_DIM + PageX;
		const int PageOffsetX = PageX * m_PageSize;
		const int PageOffsetY = PageY * m_PageSize;
		m_aAtlasPages[PageIndex].MarkDirty(PosX - PageOffsetX, PosY - PageOffsetY, PosX - PageOffsetX + Width, PosY - PageOffsetY + Height);
	}
	(void) TextureIndex;
}

void CGlyphMap::FlushUploads()
{
	if(!m_pStaging)
		return;

	// rasterize queued glyphs first so they are uploaded with the rest
	ProcessPendingGlyphs();

	for(int i = 0; i < NUM_PAGES_PER_DIM * NUM_PAGES_PER_DIM; ++i)
	{
		CAtlas &Page = m_aAtlasPages[i];
		if(!Page.m_Dirty)
			continue;

		const int X = Page.m_Offset.x + Page.m_DirtyX0;
		const int Y = Page.m_Offset.y + Page.m_DirtyY0;
		const int W = Page.m_DirtyX1 - Page.m_DirtyX0;
		const int H = Page.m_DirtyY1 - Page.m_DirtyY0;
		Page.m_Dirty = false;
		if(W <= 0 || H <= 0)
			continue;

		// pack the dirty region: LoadTextureRawSub assumes pitch == width
		unsigned char *pData = (unsigned char *) mem_alloc((size_t) W * H);
		for(int y = 0; y < H; ++y)
			mem_copy(pData + (size_t) y * W, m_pStaging + (size_t) (Y + y) * m_TextureSize + X, W);

		m_pGraphics->LoadTextureRawSub(m_Texture, X, Y, 0, W, H, CImageInfo::FORMAT_ALPHA, pData);
		mem_free(pData);
	}
}

int CGlyphMap::FitGlyph(int Width, int Height, ivec2 *pPosition)
{
	const int NumPages = NUM_PAGES_PER_DIM * NUM_PAGES_PER_DIM;

	// try the active page first
	*pPosition = m_aAtlasPages[m_ActiveAtlasIndex].Add(Width, Height);
	if(pPosition->x >= 0 && pPosition->y >= 0)
		return m_ActiveAtlasIndex;

	// look for any empty page before evicting a used one
	for(int i = 0; i < NumPages; ++i)
	{
		if(i == m_ActiveAtlasIndex || !m_aAtlasPages[i].m_IsEmpty)
			continue;

		*pPosition = m_aAtlasPages[i].Add(Width, Height);
		if(pPosition->x >= 0 && pPosition->y >= 0)
		{
			m_ActiveAtlasIndex = i;
			return m_ActiveAtlasIndex;
		}
	}

	// out of space: drop the least recently used page, skipping pages that were
	// touched this frame (their glyphs may already have been drawn)
	int LeastAccess = INT_MAX;
	int Atlas = -1;
	for(int i = 0; i < NumPages; ++i)
	{
		if(m_ActiveAtlasIndex == i || m_aAtlasPages[i].m_IsEmpty)
			continue;
		if(m_aAtlasPages[i].m_Access > 0)
			continue;

		int PageAccess = m_aAtlasPages[i].m_LastFrameAccess;
		if(PageAccess < LeastAccess)
		{
			LeastAccess = PageAccess;
			Atlas = i;
		}
	}

	if(Atlas < 0)
	{
		// all pages were used this frame, so fail and retry next frame
		pPosition->x = -1;
		pPosition->y = -1;
		return -1;
	}

	int X = m_aAtlasPages[Atlas].m_Offset.x;
	int Y = m_aAtlasPages[Atlas].m_Offset.y;
	int W = m_aAtlasPages[Atlas].m_Width;
	int H = m_aAtlasPages[Atlas].m_Height;

	m_aAtlasPages[Atlas].Init(m_NumTotalPages++, X, Y, W, H);

	// clear the recycled page in the staging mirror (after Init())
	unsigned char *pMem = (unsigned char *) mem_alloc(W * H);
	mem_zero(pMem, W * H);
	UploadGlyph(0, X, Y, W, H, pMem);
	mem_free(pMem);

	*pPosition = m_aAtlasPages[Atlas].Add(Width, Height);
	if(pPosition->x < 0 || pPosition->y < 0)
		return -1;

	m_ActiveAtlasIndex = Atlas;

	m_NumPageRecycles++;
	// recycling is normal, so throttle the log message
	if(m_NumPageRecycles == 1 || m_NumPageRecycles % 128 == 0)
		dbg_msg("textrender", "atlas is full, dropped atlas %d (%d recycles), total pages: %u", Atlas, m_NumPageRecycles, m_NumTotalPages);
	return Atlas;
}

bool CGlyphMap::SetFaceByName(FT_Face *pFace, const char *pFamilyName)
{
	FT_Face Face = NULL;
	char aFamilyStyleName[FONT_NAME_SIZE];

	if(pFamilyName != NULL)
	{
		for(int i = 0; i < m_NumFtFaces; ++i)
		{
			str_format(aFamilyStyleName, FONT_NAME_SIZE, "%s %s", m_aFtFaces[i]->family_name, m_aFtFaces[i]->style_name);
			if(str_comp(pFamilyName, aFamilyStyleName) == 0)
			{
				Face = m_aFtFaces[i];
				break;
			}

			if(!Face && str_comp(pFamilyName, m_aFtFaces[i]->family_name) == 0)
			{
				Face = m_aFtFaces[i];
			}
		}
	}

	if(Face)
	{
		*pFace = Face;
		return true;
	}
	return false;
}

CGlyphMap::CGlyphMap(IGraphics *pGraphics, FT_Library FtLibrary)
{
	(void) FtLibrary;
	m_pGraphics = pGraphics;

	m_DefaultFace = NULL;
	m_VariantFace = NULL;

	mem_zero(m_aFallbackFaces, sizeof(m_aFallbackFaces));
	mem_zero(m_aFtFaces, sizeof(m_aFtFaces));

	m_NumFtFaces = 0;
	m_NumFallbackFaces = 0;
	m_NumTotalPages = 0;
	m_NumPageRecycles = 0;
	m_NumRasterized = 0;
	m_pStaging = NULL;
	m_TextureSize = TEXTURE_SIZE;
	m_PageSize = TEXTURE_SIZE / NUM_PAGES_PER_DIM;
	m_Frame = 0;
	m_PendingFrame = -1;
	m_PendingBudgetSpent = false;
	mem_zero(&m_SdfScratch, sizeof(m_SdfScratch));
	g_GlyphsAlive = true;

	// use the largest square the device supports; area bounds the glyph count
	int AtlasSize = TEXTURE_SIZE;
	if(m_pGraphics)
	{
		const int DeviceMax = m_pGraphics->MaxTextureSize();
		while(AtlasSize * 2 <= MAX_TEXTURE_SIZE && AtlasSize * 2 <= DeviceMax)
			AtlasSize *= 2;
	}

	InitTexture(AtlasSize, AtlasSize);
}

CGlyphMap::~CGlyphMap()
{
	// free all glyph records; cursors may outlive the map (see CTextRender::Shutdown)
	m_Glyphs.for_each(FreeGlyphCallback, this);
	m_Glyphs.clear_size();

	if(m_pStaging)
		mem_free(m_pStaging);

	if(m_SdfScratch.m_pFloats)
		mem_free(m_SdfScratch.m_pFloats);
	if(m_SdfScratch.m_pInts)
		mem_free(m_SdfScratch.m_pInts);

	g_GlyphsAlive = false;
}

void CGlyphMap::FreeGlyphCallback(CGlyph *&pGlyph, void *pUser)
{
	mem_free(pGlyph);
	(void) pUser;
}

CGlyph *CGlyphMap::AllocateGlyph()
{
	CGlyph *pGlyph = (CGlyph *) mem_alloc(sizeof(CGlyph));
	mem_zero(pGlyph, sizeof(CGlyph));
	return pGlyph;
}

void CGlyphMap::FreeGlyph(CGlyph *pGlyph)
{
	mem_free(pGlyph);
}

void CGlyphMap::EvictScanCallback(CGlyph *&pGlyph, void *pUser)
{
	CEvictContext *pContext = (CEvictContext *) pUser;
	// never evict a glyph a cursor still references
	if(pGlyph->m_RefCount > 0)
		return;
	if(!pContext->m_pVictim || pGlyph->m_LastAccess < pContext->m_pVictim->m_LastAccess)
	{
		pContext->m_pVictim = pGlyph;
		pContext->m_VictimID = pGlyph->m_ID;
	}
}

void CGlyphMap::EvictGlyphs()
{
	if(m_Glyphs.size() < MAX_GLYPHS)
		return;

	// find the least recently used glyph that no cursor references anymore
	CEvictContext Context;
	Context.m_pVictim = NULL;
	Context.m_VictimID = -1;
	m_Glyphs.for_each(EvictScanCallback, &Context);

	if(Context.m_pVictim)
	{
		CGlyphIndex Index;
		Index.m_ID = Context.m_VictimID;
		m_Glyphs.remove(Index);
		FreeGlyph(Context.m_pVictim);
	}
}

int CGlyphMap::GetCharGlyph(int Chr, FT_Face *pFace)
{
	*pFace = m_DefaultFace;
	if(!m_DefaultFace)
		return 0;

	int GlyphIndex = FT_Get_Char_Index(m_DefaultFace, (FT_ULong) Chr);
	if(GlyphIndex)
		return GlyphIndex;

	if(m_VariantFace)
	{
		GlyphIndex = FT_Get_Char_Index(m_VariantFace, (FT_ULong) Chr);
		if(GlyphIndex)
		{
			*pFace = m_VariantFace;
			return GlyphIndex;
		}
	}

	for(int i = 0; i < m_NumFallbackFaces; ++i)
	{
		if(m_aFallbackFaces[i])
		{
			GlyphIndex = FT_Get_Char_Index(m_aFallbackFaces[i], (FT_ULong) Chr);
			if(GlyphIndex)
			{
				*pFace = m_aFallbackFaces[i];
				return GlyphIndex;
			}
		}
	}

	return 0;
}

int CGlyphMap::AddFace(FT_Face Face)
{
	if(m_NumFtFaces == MAX_FACES)
		return -1;

	m_aFtFaces[m_NumFtFaces++] = Face;
	if(!m_DefaultFace)
		m_DefaultFace = Face;

	return 0;
}

void CGlyphMap::SetDefaultFaceByName(const char *pFamilyName)
{
	SetFaceByName(&m_DefaultFace, pFamilyName);
}

void CGlyphMap::AddFallbackFaceByName(const char *pFamilyName)
{
	if(m_NumFallbackFaces == MAX_FACES)
		return;

	FT_Face Face = NULL;
	if(SetFaceByName(&Face, pFamilyName))
	{
		m_aFallbackFaces[m_NumFallbackFaces++] = Face;
	}
}

void CGlyphMap::SetVariantFaceByName(const char *pFamilyName)
{
	FT_Face Face = NULL;
	SetFaceByName(&Face, pFamilyName);
	if(m_VariantFace != Face)
	{
		m_VariantFace = Face;
		InitTexture(m_TextureSize, m_TextureSize);
	}
}

bool CGlyphMap::LoadGlyphMetrics(CGlyph *pGlyph)
{
	FT_Face GlyphFace;
	const int GlyphIndex = GetCharGlyph(pGlyph->m_ID, &GlyphFace);
	if(!GlyphFace)
		return false;

	// take the advance at the reference size and unhinted, so all glyphs of a
	// face share one scale and the spacing stays linear
	if(FT_Set_Pixel_Sizes(GlyphFace, 0, SDF_BASE_SIZE))
		return false;

	if(FT_Load_Glyph(GlyphFace, GlyphIndex, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING))
	{
		dbg_msg("textrender", "error loading glyph %d", pGlyph->m_ID);
		return false;
	}

	const float Scale = 1.0f / SDF_BASE_SIZE;
	pGlyph->m_Face = GlyphFace;
	pGlyph->m_GlyphIndex = GlyphIndex;
	pGlyph->m_AdvanceX = (GlyphFace->glyph->advance.x >> 6) * Scale;
	return true;
}

void CGlyphMap::QueueGlyph(CGlyph *pGlyph)
{
	if(pGlyph->m_Pending || IsGlyphValid(pGlyph))
		return;

	// glyphs without a bitmap (spaces) are done once their metrics are known
	if(pGlyph->m_Rendered && pGlyph->m_AtlasIndex < 0)
		return;

	pGlyph->m_Pending = true;
	m_PendingGlyphs.add(pGlyph->m_ID);
}

void CGlyphMap::ProcessPendingGlyphs(bool Unlimited)
{
	if(m_PendingGlyphs.size() == 0)
		return;

	// the budget is per frame, not per call
	if(m_PendingFrame != m_Frame)
	{
		m_PendingFrame = m_Frame;
		m_PendingBudgetSpent = false;
	}

	if(m_PendingBudgetSpent && !Unlimited)
		return;

	const int64 StartTime = time_get();
	const int64 Budget = (int64) time_freq() * SDF_RASTERIZE_BUDGET_MS / 1000;

	int NumProcessed = 0;
	while(m_PendingGlyphs.size() > 0)
	{
		if(!Unlimited && NumProcessed >= SDF_MIN_GLYPHS_PER_FRAME && time_get() - StartTime >= Budget)
		{
			m_PendingBudgetSpent = true;
			break;
		}

		CGlyphIndex Index;
		Index.m_ID = m_PendingGlyphs[0];
		CGlyph **ppGlyph = m_Glyphs[Index];
		if(ppGlyph)
			RenderGlyph(*ppGlyph, true);

		// the order does not matter, so swap the last entry in
		m_PendingGlyphs.remove_index_fast(0);
		NumProcessed++;
	}
}

bool CGlyphMap::RenderGlyph(CGlyph *pGlyph, bool Render)
{
	if(Render && pGlyph->m_Rendered)
	{
		if(pGlyph->m_AtlasIndex < 0)
		{
			// nothing to rasterize, this glyph has no bitmap
			pGlyph->m_Pending = false;
			return false;
		}

		if(m_aAtlasPages[pGlyph->m_AtlasIndex].m_ID == pGlyph->m_PageID)
		{
			TouchPage(pGlyph->m_AtlasIndex);
			pGlyph->m_Pending = false;
			return false;
		}
	}

	if(!pGlyph->m_Face && !LoadGlyphMetrics(pGlyph))
	{
		pGlyph->m_Pending = false;
		return false;
	}

	unsigned char *pSdfData = NULL;
	int SdfWidth = 0;
	int SdfHeight = 0;
	float SdfLeft = 0.0f;
	float SdfTop = 0.0f;

	m_NumRasterized++;
	if(!GenerateGlyphSDF(pGlyph->m_Face, pGlyph->m_GlyphIndex, SDF_BASE_SIZE, SDF_SPREAD,
		   &pSdfData, &SdfWidth, &SdfHeight, &SdfLeft, &SdfTop, &m_SdfScratch))
	{
		// no coverage bitmap (a space); mark it done so it is not queued again
		pGlyph->m_AtlasIndex = -1;
		pGlyph->m_PageID = -1;
		pGlyph->m_Rendered = Render;
		pGlyph->m_Pending = false;
		return true;
	}

	// a little padding keeps shadow samples from reaching the next glyph
	const int Spacing = 2;
	const int Width = SdfWidth + Spacing * 2;
	const int Height = SdfHeight + Spacing * 2;

	int AtlasIndex = -1;
	int Page = -1;

	if(Render)
	{
		ivec2 Position = ivec2(0, 0);
		AtlasIndex = FitGlyph(Width, Height, &Position);
		if(AtlasIndex >= 0)
		{
			Page = m_aAtlasPages[AtlasIndex].m_ID;

			UploadGlyph(0, Position.x + Spacing, Position.y + Spacing, SdfWidth, SdfHeight, pSdfData);

			TouchPage(AtlasIndex);

			const float UVScale = 1.0f / m_TextureSize;
			pGlyph->m_aUvCoords[0] = (Position.x + Spacing) * UVScale;
			pGlyph->m_aUvCoords[1] = (Position.y + Spacing) * UVScale;
			pGlyph->m_aUvCoords[2] = pGlyph->m_aUvCoords[0] + SdfWidth * UVScale;
			pGlyph->m_aUvCoords[3] = pGlyph->m_aUvCoords[1] + SdfHeight * UVScale;
		}
	}

	mem_free(pSdfData);

	const float Scale = 1.0f / SDF_BASE_SIZE;
	pGlyph->m_AtlasIndex = AtlasIndex;
	pGlyph->m_PageID = Page;
	pGlyph->m_Width = SdfWidth * Scale;
	pGlyph->m_Height = SdfHeight * Scale;
	// the bitmap carries SDF_SPREAD texels of padding on every side
	pGlyph->m_BearingX = (SdfLeft - SDF_SPREAD) * Scale;
	pGlyph->m_BearingY = (SDF_BASE_SIZE - (SdfTop + SDF_SPREAD)) * Scale;
	// leaving m_Rendered false when the atlas was full retries next frame
	pGlyph->m_Rendered = Render && AtlasIndex >= 0;
	pGlyph->m_Pending = false;

	return true;
}

CGlyph *CGlyphMap::GetGlyph(int Chr, bool Render)
{
	CGlyphIndex Index;
	Index.m_ID = Chr;

	CGlyph **ppMatch = m_Glyphs[Index];
	// couldn't find glyph, create a new one
	if(!ppMatch)
	{
		// make room before inserting, evicting only unreferenced glyphs
		EvictGlyphs();

		CGlyph *pGlyph = AllocateGlyph();
		pGlyph->m_Rendered = false;
		pGlyph->m_ID = Chr;
		pGlyph->m_RefCount = 0;
		pGlyph->m_LastAccess = m_Frame;
		pGlyph->m_AtlasIndex = -1;
		pGlyph->m_PageID = -1;
		// only the metrics are loaded here, rasterization is queued
		if(!LoadGlyphMetrics(pGlyph))
		{
			FreeGlyph(pGlyph);
			return NULL;
		}
		m_Glyphs.set(Index, pGlyph);
		if(Render)
			QueueGlyph(pGlyph);
		return pGlyph;
	}

	CGlyph *pGlyph = *ppMatch;
	pGlyph->m_LastAccess = m_Frame;
	if(Render)
		QueueGlyph(pGlyph);
	return pGlyph;
}

vec2 CGlyphMap::Kerning(CGlyph *pLeft, CGlyph *pRight, int PixelSize)
{
	FT_Vector Kerning = {0, 0};

	vec2 Vec(0, 0);
	if(pLeft && pRight && pLeft->m_Face == pRight->m_Face)
	{
		CGlyphKerning CacheSearch;
		CacheSearch.m_PixelSize = PixelSize;
		CacheSearch.m_LeftID = pLeft->m_ID;
		CacheSearch.m_RightID = pRight->m_ID;

		vec2 *pMatch = m_Kernings[CacheSearch];
		if(!pMatch)
		{
			// only drop the cache when a new pair has to be inserted
			if(m_Kernings.size() > MAX_KERNING_CACHE)
				m_Kernings.clear_size();

			FT_Set_Pixel_Sizes(pLeft->m_Face, 0, PixelSize);
			// FT_Get_Kerning takes glyph indices; unfitted keeps sub-pixel precision
			FT_Get_Kerning(pLeft->m_Face, pLeft->m_GlyphIndex, pRight->m_GlyphIndex, FT_KERNING_UNFITTED, &Kerning);

			Vec.x = Kerning.x / 64.0f;
			Vec.y = Kerning.y / 64.0f;
			m_Kernings.set(CacheSearch, Vec);
		}
		else
		{
			Vec = *pMatch;
		}
	}

	return Vec;
}

void CGlyphMap::TouchPage(int Index)
{
	m_aAtlasPages[Index].m_Access++;
}

void CGlyphMap::PagesAccessReset()
{
	for(int i = 0; i < NUM_PAGES_PER_DIM * NUM_PAGES_PER_DIM; ++i)
	{
		m_aAtlasPages[i].m_LastFrameAccess = m_aAtlasPages[i].m_Access;
		m_aAtlasPages[i].m_Access = 0;
	}
	m_Frame++;
}

CWordWidthHint CTextRender::MakeWord(CTextCursor *pCursor, const char *pText, const char *pEnd, float Size, int PixelSize, vec2 ScreenScale)
{
	bool Render = !(pCursor->m_Flags & TEXTFLAG_NO_RENDER);
	bool BreakWord = !(pCursor->m_Flags & TEXTFLAG_WORD_WRAP);
	bool AllowNewline = pCursor->m_Flags & TEXTFLAG_ALLOW_NEWLINE;
	CWordWidthHint Hint;
	const char *pCur = pText;
	int NextChr = str_utf8_decode(&pCur);
	CGlyph *pNextGlyph = NULL;
	if(NextChr > 0)
	{
		if(NextChr == '\n' || NextChr == '\t')
			pNextGlyph = m_pGlyphMap->GetGlyph(' ', Render);
		else
			pNextGlyph = m_pGlyphMap->GetGlyph(NextChr, Render);
	}

	float Scale = 1.0f / PixelSize;
	float MaxWidth = pCursor->m_MaxWidth;
	if(MaxWidth < 0)
		MaxWidth = INFINITY;

	float WordStartAdvanceX = pCursor->m_Advance.x;

	Hint.m_CharCount = 0;
	Hint.m_GlyphCount = 0;
	Hint.m_EffectiveAdvanceX = pCursor->m_Advance.x;
	Hint.m_EndsWithNewline = false;
	Hint.m_IsBroken = false;

	if(*pText == '\0' || pCur > pEnd)
	{
		Hint.m_CharCount = -1;
		return Hint;
	}

	while(true)
	{
		int Chr = NextChr;
		CGlyph *pGlyph = pNextGlyph;
		int CharCount = pCur - pText;
		int NumChars = CharCount - Hint.m_CharCount;
		Hint.m_CharCount = CharCount;

		if(Chr == 0 || pCur > pEnd)
		{
			Hint.m_CharCount--;
			break;
		}

		if(!pGlyph || Chr < 0)
		{
			Hint.m_CharCount = -1;
			return Hint;
		}

		NextChr = str_utf8_decode(&pCur);
		pNextGlyph = NULL;
		if(NextChr > 0)
		{
			if(NextChr == '\n' || NextChr == '\t')
				pNextGlyph = m_pGlyphMap->GetGlyph(' ', Render);
			else
				pNextGlyph = m_pGlyphMap->GetGlyph(NextChr, Render);
		}

		vec2 Kerning = m_pGlyphMap->Kerning(pGlyph, pNextGlyph, PixelSize) * Scale;
		float AdvanceX = (pGlyph->m_AdvanceX + Kerning.x) * Size;

		bool IsSpace = Chr == '\n' || Chr == '\t' || Chr == ' ';
		bool CanBreak = !IsSpace && (BreakWord || pCursor->m_StartOfLine);
		if(Hint.m_EffectiveAdvanceX - WordStartAdvanceX > MaxWidth || (CanBreak && pCursor->m_Advance.x + AdvanceX > MaxWidth))
		{
			Hint.m_CharCount -= NumChars;
			Hint.m_IsBroken = true;
			break;
		}

		if(Render)
		{
			CScaledGlyph &Scaled = pCursor->m_Glyphs.emplace();
			Scaled.m_pGlyph = pGlyph;
			Scaled.m_Advance = pCursor->m_Advance;
			Scaled.m_Size = Size;
			Scaled.m_Line = pCursor->m_LineCount - 1;
			Scaled.m_TextColorIndex = CTextCursor::ColorIndex(pCursor->m_TextColors, m_TextColor);
			Scaled.m_SecondaryColorIndex = CTextCursor::ColorIndex(pCursor->m_SecondaryColors, m_TextSecondaryColor);
			Scaled.m_NumChars = NumChars;

			// keep the glyph cache record alive while this cursor uses it
			pGlyph->m_RefCount++;
			pCursor->m_pfnReleaseGlyphs = ReleaseGlyphRefs;
			pCursor->m_pReleaseGlyphsUser = NULL;
		}

		pCursor->m_Advance.x += AdvanceX;
		Hint.m_GlyphCount++;

		if(IsSpace || Chr == 0)
		{
			Hint.m_EndsWithNewline = Chr == '\n';
			if(AllowNewline && Hint.m_EndsWithNewline)
			{
				// remove redundant space
				Hint.m_GlyphCount--;
				if(Render)
				{
					ReleaseScaledGlyph(pCursor->m_Glyphs[pCursor->m_Glyphs.size() - 1]);
					pCursor->m_Glyphs.remove_index_fast(pCursor->m_Glyphs.size() - 1);
				}
			}
			break;
		}

		Hint.m_EffectiveAdvanceX = pCursor->m_Advance.x;

		// break every char on non latin/greek characters
		if(!IsWestern(Chr))
			break;
	}

	return Hint;
}

void CTextRender::TextRefreshGlyphs(CTextCursor *pCursor)
{
	const int NumGlyphs = pCursor->m_Glyphs.size();
	if(NumGlyphs <= 0)
		return;

	// requeue glyphs whose atlas page was recycled; they are rasterized within
	// the frame budget, otherwise DrawText() skips them for this frame
	for(int i = 0; i < NumGlyphs; ++i)
	{
		CGlyph *pGlyph = pCursor->m_Glyphs[i].m_pGlyph;
		if(pGlyph && !m_pGlyphMap->IsGlyphValid(pGlyph))
			m_pGlyphMap->QueueGlyph(pGlyph);
	}

	pCursor->m_PageCountWhenDrawn = m_pGlyphMap->NumTotalPages();
}

int CTextRender::LoadFontCollection(const char *pFilename, const void *pBuf, unsigned FileSize)
{
	FT_Face FtFace;

	if(FT_New_Memory_Face(m_FTLibrary, (FT_Byte *) pBuf, (FT_Long) FileSize, -1, &FtFace))
		return -1;

	int NumFaces = FtFace->num_faces;
	FT_Done_Face(FtFace);

	int i;
	for(i = 0; i < NumFaces; ++i)
	{
		if(FT_New_Memory_Face(m_FTLibrary, (FT_Byte *) pBuf, (FT_Long) FileSize, i, &FtFace))
		{
			FT_Done_Face(FtFace);
			break;
		}

		if(m_pGlyphMap->AddFace(FtFace))
		{
			FT_Done_Face(FtFace);
			break;
		}
	}

	dbg_msg("textrender", "loaded %d faces from font file '%s'", i, (char *) pFilename);

	return 0;
}

CTextRender::CTextRender()
{
	m_pGraphics = 0;

	m_TextColor = vec4(1.0f, 1.0f, 1.0f, 1.0f);
	m_TextSecondaryColor = vec4(0.0f, 0.0f, 0.0f, 0.3f);

	m_pGlyphMap = 0;
	m_NumVariants = 0;
	m_CurrentVariant = -1;
	m_pVariants = 0;

	mem_zero(m_apFontData, sizeof(m_apFontData));

	mem_zero(m_aLayoutCache, sizeof(m_aLayoutCache));
	m_LayoutCacheTick = 0;

	m_pConfig = 0;
	m_StatFrame = 0;
	m_StatDrawPasses = 0;
	m_StatDrawCalls = 0;
	m_StatGlyphs = 0;
	m_StatQuadPixels = 0;
	m_StatLayoutCalls = 0;
	m_StatCacheHits = 0;
	m_StatCacheMisses = 0;
	m_StatRasterized = 0;
	m_StatPageRecycles = 0;
	m_StatLayoutTime = 0;
	m_StatDrawTime = 0;
	m_StatSetupTime = 0;
	m_StatLoopTime = 0;
	m_StatEndTime = 0;
	m_StatLastFlushTime = 0;
}

void CTextRender::Init()
{
	m_pGraphics = Kernel()->RequestInterface<IGraphics>();
	IConfigManager *pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	m_pConfig = pConfigManager ? pConfigManager->Values() : NULL;
	FT_Init_FreeType(&m_FTLibrary);
	m_pGlyphMap = new CGlyphMap(m_pGraphics, m_FTLibrary);
}

void CTextRender::Update()
{
	if(m_pGlyphMap)
		m_pGlyphMap->PagesAccessReset();

	// dump the accumulated statistics once per second when dbg_text_stats is set
	m_StatFrame++;
	if(m_StatFrame < 60)
		return;
	if(!m_pConfig || !m_pConfig->m_DbgTextStats)
	{
		// keep the counters from growing while disabled
		m_StatFrame = 0;
		m_StatDrawPasses = m_StatDrawCalls = m_StatGlyphs = m_StatLayoutCalls = 0;
		m_StatCacheHits = m_StatCacheMisses = 0;
		m_StatQuadPixels = 0;
		m_StatLayoutTime = m_StatDrawTime = 0;
		m_StatSetupTime = m_StatLoopTime = m_StatEndTime = 0;
		if(m_pGlyphMap)
		{
			m_StatPageRecycles = m_pGlyphMap->NumPageRecycles();
			m_StatRasterized = m_pGlyphMap->NumRasterized();
		}
		return;
	}

	const double Freq = (double) time_freq();
	const int64 Now = time_get();
	const double Interval = m_StatLastFlushTime ? (Now - m_StatLastFlushTime) / Freq : 0.0;
	m_StatLastFlushTime = Now;

	// per frame numbers use presented frames, since the main loop can run several
	// iterations per presented frame
	int Frames = Graphics() ? Graphics()->TakeRenderedFrameCount() : 0;
	if(Frames <= 0)
		Frames = m_StatFrame;
	const double Fps = Interval > 0.0 ? Frames / Interval : 0.0;
	const int Commands = Graphics() ? Graphics()->TakeRenderCommandCount() : 0;
	const int64 RenderThreadTime = Graphics() ? Graphics()->TakeRenderThreadTime() : 0;
	const int Recycles = m_pGlyphMap ? m_pGlyphMap->NumPageRecycles() - m_StatPageRecycles : 0;
	const int Rasterized = m_pGlyphMap ? m_pGlyphMap->NumRasterized() - m_StatRasterized : 0;
	if(m_pGlyphMap)
	{
		m_StatPageRecycles = m_pGlyphMap->NumPageRecycles();
		m_StatRasterized = m_pGlyphMap->NumRasterized();
	}

	dbg_msg("textstats",
		"fps=%.0f frames=%d drawcalls=%d/frame passes=%.1f/frame textdraws=%.1f/frame glyphs=%.0f/frame quadpx=%.2fM/frame "
		"layoutcalls=%.1f/frame cachehit=%.0f%% layout=%.3fms draw=%.3fms (setup=%.3f loop=%.3f end=%.3f) "
		"rasterized=%.1f/frame recycles=%d renderthread=%.3fms", 
		Fps, Frames, Commands / Frames, (double) m_StatDrawPasses / Frames,
		(double) m_StatDrawCalls / Frames, (double) m_StatGlyphs / Frames, (double) m_StatQuadPixels / Frames / 1.0e6,
		(double) m_StatLayoutCalls / Frames,
		(m_StatCacheHits + m_StatCacheMisses) > 0 ? 100.0 * m_StatCacheHits / (m_StatCacheHits + m_StatCacheMisses) : 0.0,
		(double) m_StatLayoutTime / Freq * 1000.0 / Frames,
		(double) m_StatDrawTime / Freq * 1000.0 / Frames,
		(double) m_StatSetupTime / Freq * 1000.0 / Frames,
		(double) m_StatLoopTime / Freq * 1000.0 / Frames,
		(double) m_StatEndTime / Freq * 1000.0 / Frames,
		(double) Rasterized / Frames, Recycles,
		(double) RenderThreadTime / Freq * 1000.0 / Frames);

	m_StatFrame = 0;
	m_StatDrawPasses = m_StatDrawCalls = m_StatGlyphs = m_StatLayoutCalls = 0;
	m_StatCacheHits = m_StatCacheMisses = 0;
	m_StatQuadPixels = 0;
	m_StatLayoutTime = m_StatDrawTime = 0;
	m_StatSetupTime = m_StatLoopTime = m_StatEndTime = 0;
}

void CTextRender::Shutdown()
{
	// drop cached layouts while the glyph map is still alive, so their glyph
	// references are released
	ClearLayoutCache();

	delete m_pGlyphMap;
	m_pGlyphMap = 0;

	FT_Done_FreeType(m_FTLibrary);

	if(m_pVariants)
		mem_free(m_pVariants);

	for(int i = 0; i < MAX_FACES; ++i)
		if(m_apFontData[i])
			mem_free(m_apFontData[i]);
}

void CTextRender::LoadFonts(IStorage *pStorage, IConsole *pConsole)
{
	CJsonParser JsonParser;
	const json_value *pJsonData = JsonParser.ParseFile("fonts/index.json", pStorage);
	if(pJsonData == 0)
	{
		pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "textrender", JsonParser.Error());
		return;
	}

	// extract font file definitions
	const json_value &rFiles = (*pJsonData)["font files"];
	if(rFiles.type == json_array)
	{
		for(unsigned i = 0; i < rFiles.u.array.length && i < MAX_FACES; ++i)
		{
			char aFontName[IO_MAX_PATH_LENGTH];
			str_format(aFontName, sizeof(aFontName), "fonts/%s", (const char *) rFiles[i]);
			unsigned FileSize;
			if(pStorage->ReadFile(aFontName, IStorage::TYPE_ALL, &m_apFontData[i], &FileSize))
			{
				if(LoadFontCollection(aFontName, m_apFontData[i], FileSize))
				{
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "failed to load font. filename='%s'", aFontName);
					pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "textrender", aBuf);
				}
			}
		}
	}

	// extract default family name
	const json_value &rDefaultFace = (*pJsonData)["default"];
	if(rDefaultFace.type == json_string)
	{
		m_pGlyphMap->SetDefaultFaceByName((const char *) rDefaultFace);
	}

	// extract fallback family names
	const json_value &rFallbackFaces = (*pJsonData)["fallbacks"];
	if(rFallbackFaces.type == json_array)
	{
		for(unsigned i = 0; i < rFallbackFaces.u.array.length; ++i)
		{
			m_pGlyphMap->AddFallbackFaceByName((const char *) rFallbackFaces[i]);
		}
	}

	// extract language variant family names
	const json_value &rVariant = (*pJsonData)["language variants"];
	if(rVariant.type == json_object)
	{
		m_NumVariants = rVariant.u.object.length;
		json_object_entry *Entries = rVariant.u.object.values;
		m_pVariants = (CFontLanguageVariant *) mem_alloc(sizeof(CFontLanguageVariant) * m_NumVariants);
		for(int i = 0; i < m_NumVariants; ++i)
		{
			char aFileName[128];
			str_format(aFileName, sizeof(aFileName), "languages/%s.json", (const char *) Entries[i].name);
			str_copy(m_pVariants[i].m_aLanguageFile, aFileName, sizeof(m_pVariants[i].m_aLanguageFile));

			json_value *pFamilyName = rVariant.u.object.values[i].value;
			if(pFamilyName->type == json_string)
				str_copy(m_pVariants[i].m_aFamilyName, pFamilyName->u.string.ptr, sizeof(m_pVariants[i].m_aFamilyName));
			else
				m_pVariants[i].m_aFamilyName[0] = 0;
		}
	}
}

void CTextRender::SetFontLanguageVariant(const char *pLanguageFile)
{
	if(!m_pGlyphMap)
		return;

	char *pFamilyName = NULL;

	if(m_pVariants)
	{
		for(int i = 0; i < m_NumVariants; ++i)
		{
			if(str_comp_filenames(pLanguageFile, m_pVariants[i].m_aLanguageFile) == 0)
			{
				pFamilyName = m_pVariants[i].m_aFamilyName;
				m_CurrentVariant = i;
				break;
			}
		}
	}

	m_pGlyphMap->SetVariantFaceByName(pFamilyName);

	PrebakeGlyphs();
}

void CTextRender::PrebakeGlyphs()
{
	if(!m_pGlyphMap->GetDefaultFace())
		return;

	for(int Chr = 0x20; Chr <= 0x7E; ++Chr)
		m_pGlyphMap->GetGlyph(Chr, true);

	// no frame budget while loading, so the first frame that shows text is complete
	m_pGlyphMap->ProcessPendingGlyphs(true);
}

float CTextRender::TextWidth(float FontSize, const char *pText, int Length)
{
	static CTextCursor s_Cursor;
	s_Cursor.m_FontSize = FontSize;
	s_Cursor.m_Flags = TEXTFLAG_NO_RENDER;
	// reset the configuration so a previous width limit does not leak in
	s_Cursor.ResetLayout();
	TextDeferred(&s_Cursor, pText, Length);
	return s_Cursor.m_Width;
}

void CTextRender::TextDeferred(CTextCursor *pCursor, const char *pText, int Length)
{
	if(pCursor->m_Truncated || pCursor->m_SkipTextRender)
		return;

	if(!m_pGlyphMap->GetDefaultFace())
		return;

	// only time the layout when the statistics are actually requested
	const bool TimeLayout = m_pConfig && m_pConfig->m_DbgTextStats;
	const int64 LayoutStart = TimeLayout ? time_get() : 0;

	m_StatLayoutCalls++;

	bool Render = !(pCursor->m_Flags & TEXTFLAG_NO_RENDER);

	// Alignment
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	int ScreenWidth = Graphics()->ScreenWidth();
	int ScreenHeight = Graphics()->ScreenHeight();
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	vec2 ScreenScale = vec2(ScreenWidth / (ScreenX1 - ScreenX0), ScreenHeight / (ScreenY1 - ScreenY0));
	float Size = pCursor->m_FontSize;
	int PixelSize = (int) (Size * ScreenScale.y);
	Size = PixelSize / ScreenScale.y;
	// remember the physical size the layout is built for; drawing derives the
	// SDF scale from it instead of querying the screen again
	pCursor->m_PixelSize = PixelSize;

	// Cursor current states
	int Flags = pCursor->m_Flags;
	float MaxWidth = pCursor->m_MaxWidth;
	if(MaxWidth < 0)
		MaxWidth = INFINITY;
	int MaxLines = pCursor->m_MaxLines;
	if(MaxLines < 0)
		MaxLines = (ScreenY1 - ScreenY0) / pCursor->m_FontSize;

	if(Length < 0)
		Length = str_length(pText);

	const char *pCur = (char *) pText;
	const char *pEnd = (char *) pText + Length;

	float NextAdvanceY = pCursor->m_Advance.y + pCursor->m_FontSize;
	NextAdvanceY = (int) (NextAdvanceY * ScreenScale.y) / ScreenScale.y;
	pCursor->m_NextLineAdvanceY = maximum(NextAdvanceY, pCursor->m_NextLineAdvanceY);

	while(pCur < pEnd && !pCursor->m_Truncated)
	{
		const float WordStartAdvanceX = pCursor->m_Advance.x;
		CWordWidthHint WordWidth = MakeWord(pCursor, pCur, pEnd, Size, PixelSize, ScreenScale);
		pCursor->m_StartOfLine = false;
		if(WordWidth.m_CharCount < 0)
			break;

		// word wrapping
		if(WordWidth.m_EffectiveAdvanceX > MaxWidth)
		{
			const int NumGlyphs = pCursor->m_Glyphs.size();
			// do not let space create new line.
			if(WordWidth.m_GlyphCount == 1 && (*pCur == ' ' || *pCur == '\n' || *pCur == '\t'))
			{
				if(Render)
				{
					ReleaseScaledGlyph(pCursor->m_Glyphs[NumGlyphs - 1]);
					pCursor->m_Glyphs.remove_index_fast(NumGlyphs - 1);
				}
				pCursor->m_Advance.x = WordStartAdvanceX;
			}
			else
			{
				if(pCursor->m_LineCount < MaxLines)
				{
					pCursor->m_LineCount++;
					float AdvanceY = pCursor->m_Advance.y;
					pCursor->m_Advance.y = pCursor->m_LineSpacing + pCursor->m_NextLineAdvanceY;
					pCursor->m_Advance.x -= WordStartAdvanceX;

					float NextAdvanceY = pCursor->m_Advance.y + pCursor->m_FontSize;
					NextAdvanceY = (int) (NextAdvanceY * ScreenScale.y) / ScreenScale.y;
					pCursor->m_NextLineAdvanceY = maximum(NextAdvanceY, pCursor->m_NextLineAdvanceY);

					if(Render)
					{
						const int WordStartGlyphIndex = NumGlyphs - WordWidth.m_GlyphCount;
						for(int i = NumGlyphs - 1; i >= WordStartGlyphIndex; --i)
						{
							pCursor->m_Glyphs[i].m_Advance.x -= WordStartAdvanceX;
							pCursor->m_Glyphs[i].m_Advance.y += pCursor->m_Advance.y - AdvanceY;
							pCursor->m_Glyphs[i].m_Line = pCursor->m_LineCount - 1;
						}
					}

					pCursor->m_StartOfLine = false;
				}
				else
				{
					pCursor->m_Truncated = true;
				}
			}
		}

		pCursor->m_Width = maximum(pCursor->m_Advance.x, pCursor->m_Width);

		// newline \n
		bool ForceNewLine = WordWidth.m_EndsWithNewline && (Flags & TEXTFLAG_ALLOW_NEWLINE);
		if(ForceNewLine || WordWidth.m_IsBroken)
		{
			if(pCursor->m_LineCount < MaxLines)
			{
				pCursor->m_LineCount++;
				pCursor->m_Advance.y = pCursor->m_LineSpacing + pCursor->m_NextLineAdvanceY;
				pCursor->m_Advance.x = 0;
				pCursor->m_StartOfLine = true;

				float NextAdvanceY = pCursor->m_Advance.y + pCursor->m_FontSize;
				NextAdvanceY = (int) (NextAdvanceY * ScreenScale.y) / ScreenScale.y;
				pCursor->m_NextLineAdvanceY = maximum(NextAdvanceY, pCursor->m_NextLineAdvanceY);
			}
			else
			{
				pCursor->m_Truncated = true;
			}
		}

		pCur += WordWidth.m_CharCount;
	}

	pCursor->m_Height = pCursor->m_NextLineAdvanceY + 0.35f * Size;
	pCursor->m_CharCount = pCur - pText;

	// insert ellipsis at the end
	if(pCursor->m_Truncated && (pCursor->m_Flags & TEXTFLAG_ELLIPSIS))
	{
		const char aEllipsis[] = "…";
		const int NumTextGlyphs = pCursor->m_Glyphs.size();

		// start the ellipsis right after the last glyph that was laid out
		if(NumTextGlyphs > 0)
		{
			const CScaledGlyph &rLastGlyph = pCursor->m_Glyphs[NumTextGlyphs - 1];
			pCursor->m_Advance.x = rLastGlyph.m_Advance.x + rLastGlyph.m_pGlyph->m_AdvanceX * rLastGlyph.m_Size;
			pCursor->m_Advance.y = rLastGlyph.m_Advance.y;
		}
		const float EllipsisStartX = pCursor->m_Advance.x;

		// lay the ellipsis out unlimited, then shrink the text until it fits
		const int OldMaxWidth = pCursor->m_MaxWidth;
		pCursor->m_MaxWidth = -1;
		CWordWidthHint EllipsisWidth = MakeWord(pCursor, aEllipsis, aEllipsis + sizeof(aEllipsis), Size, PixelSize, ScreenScale);
		pCursor->m_MaxWidth = OldMaxWidth;

		const float EllipsisAdvance = EllipsisWidth.m_EffectiveAdvanceX - EllipsisStartX;

		// the ellipsis is not part of the source string, so keep it out of the
		// character count
		for(int i = NumTextGlyphs; i < pCursor->m_Glyphs.size(); ++i)
			pCursor->m_Glyphs[i].m_NumChars = 0;

		// drop trailing glyphs until the ellipsis ends inside MaxWidth
		int KeepGlyphs = NumTextGlyphs;
		float NewStartX = EllipsisStartX;
		while(KeepGlyphs > 0)
		{
			const CScaledGlyph &rLastGlyph = pCursor->m_Glyphs[KeepGlyphs - 1];
			const float GlyphEndX = rLastGlyph.m_Advance.x + rLastGlyph.m_pGlyph->m_AdvanceX * rLastGlyph.m_Size;
			if(GlyphEndX + EllipsisAdvance <= MaxWidth)
			{
				NewStartX = GlyphEndX;
				break;
			}
			KeepGlyphs--;
		}

		if(KeepGlyphs < NumTextGlyphs)
		{
			for(int i = NumTextGlyphs - 1; i >= KeepGlyphs; --i)
			{
				pCursor->m_CharCount -= pCursor->m_Glyphs[i].m_NumChars;
				ReleaseScaledGlyph(pCursor->m_Glyphs[i]);
				pCursor->m_Glyphs.remove_index(i);
			}

			// move the ellipsis glyphs to the new end of the text
			const float Shift = NewStartX - EllipsisStartX;
			for(int i = KeepGlyphs; i < pCursor->m_Glyphs.size(); ++i)
				pCursor->m_Glyphs[i].m_Advance.x += Shift;
			pCursor->m_Advance.x = NewStartX + EllipsisAdvance;
		}
		else
		{
			pCursor->m_Advance.x = EllipsisWidth.m_EffectiveAdvanceX;
		}

		pCursor->m_Width = maximum(pCursor->m_Width, pCursor->m_Advance.x);
	}

	if(TimeLayout)
		m_StatLayoutTime += time_get() - LayoutStart;
}

void CTextRender::ReleaseGlyphsOf(CScaledGlyph *pGlyphs, int NumGlyphs)
{
	if(!g_GlyphsAlive)
		return;
	for(int i = 0; i < NumGlyphs; ++i)
	{
		if(pGlyphs[i].m_pGlyph)
			pGlyphs[i].m_pGlyph->m_RefCount--;
	}
}

void CTextRender::FreeLayoutEntry(SLayoutCacheEntry *pEntry)
{
	if(!pEntry->m_Valid)
		return;

	ReleaseGlyphsOf(pEntry->m_pGlyphs, pEntry->m_NumGlyphs);
	if(pEntry->m_pGlyphs)
		mem_free(pEntry->m_pGlyphs);
	if(pEntry->m_pTextColors)
		mem_free(pEntry->m_pTextColors);
	if(pEntry->m_pSecondaryColors)
		mem_free(pEntry->m_pSecondaryColors);
	mem_zero(pEntry, sizeof(*pEntry));
}

void CTextRender::ClearLayoutCache()
{
	for(int i = 0; i < LAYOUT_CACHE_SIZE; ++i)
		FreeLayoutEntry(&m_aLayoutCache[i]);
	m_LayoutCacheIndex.clear();
}

void CTextRender::TextDeferredCached(CTextCursor *pCursor, const char *pText, int Length)
{
	if(pCursor->m_Truncated || pCursor->m_SkipTextRender)
		return;
	if(!m_pGlyphMap->GetDefaultFace())
		return;

	// drop any leftover layout so its glyph references are not leaked
	if(pCursor->m_Glyphs.size() > 0)
	{
		pCursor->ReleaseGlyphs();
		pCursor->m_Glyphs.clear_size();
		pCursor->m_TextColors.clear_size();
		pCursor->m_SecondaryColors.clear_size();
	}

	if(Length < 0)
		Length = str_length(pText);

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	int ScreenWidth = Graphics()->ScreenWidth();
	int ScreenHeight = Graphics()->ScreenHeight();
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	vec2 ScreenScale = vec2(ScreenWidth / (ScreenX1 - ScreenX0), ScreenHeight / (ScreenY1 - ScreenY0));
	int PixelSize = (int) (pCursor->m_FontSize * ScreenScale.y);
	if(PixelSize < 1)
		PixelSize = 1;
	// also recorded on a cache hit, the cached glyphs were laid out for it
	pCursor->m_PixelSize = PixelSize;

	// Everything that influences the layout has to be part of the key.
	struct SLayoutKey
	{
		float m_FontSize;
		float m_MaxWidth;
		float m_LineSpacing;
		int m_MaxLines;
		int m_Flags;
		int m_PixelSize;
		vec4 m_TextColor;
		vec4 m_SecondaryColor;
	};
	SLayoutKey Key;
	mem_zero(&Key, sizeof(Key));
	Key.m_FontSize = pCursor->m_FontSize;
	Key.m_MaxWidth = pCursor->m_MaxWidth;
	Key.m_LineSpacing = pCursor->m_LineSpacing;
	Key.m_MaxLines = pCursor->m_MaxLines;
	Key.m_Flags = pCursor->m_Flags;
	Key.m_PixelSize = PixelSize;
	Key.m_TextColor = m_TextColor;
	Key.m_SecondaryColor = m_TextSecondaryColor;

	unsigned long long Hash = 1469598103934665603ull;
	Hash = HashBytes(Hash, pText, Length);
	Hash = HashBytes(Hash, &Key, sizeof(Key));

	CLayoutCacheKey CacheKey;
	CacheKey.m_Hash = Hash;

	int *pCachedSlot = m_LayoutCacheIndex[CacheKey];
	if(pCachedSlot)
	{
		m_StatCacheHits++;
		SLayoutCacheEntry *pEntry = &m_aLayoutCache[*pCachedSlot];
		pEntry->m_LastAccess = ++m_LayoutCacheTick;

		pCursor->m_Glyphs.set_size(pEntry->m_NumGlyphs);
		if(pEntry->m_NumGlyphs > 0)
			mem_copy(pCursor->m_Glyphs.base_ptr(), pEntry->m_pGlyphs, sizeof(CScaledGlyph) * pEntry->m_NumGlyphs);
		pCursor->m_TextColors.set_size(pEntry->m_NumTextColors);
		if(pEntry->m_NumTextColors > 0)
			mem_copy(pCursor->m_TextColors.base_ptr(), pEntry->m_pTextColors, sizeof(vec4) * pEntry->m_NumTextColors);
		pCursor->m_SecondaryColors.set_size(pEntry->m_NumSecondaryColors);
		if(pEntry->m_NumSecondaryColors > 0)
			mem_copy(pCursor->m_SecondaryColors.base_ptr(), pEntry->m_pSecondaryColors, sizeof(vec4) * pEntry->m_NumSecondaryColors);

		pCursor->m_Width = pEntry->m_Width;
		pCursor->m_Height = pEntry->m_Height;
		pCursor->m_NextLineAdvanceY = pEntry->m_NextLineAdvanceY;
		pCursor->m_Advance = pEntry->m_Advance;
		pCursor->m_LineCount = pEntry->m_LineCount;
		pCursor->m_CharCount = pEntry->m_CharCount;
		pCursor->m_PageCountWhenDrawn = pEntry->m_PageCountWhenDrawn;
		pCursor->m_Truncated = pEntry->m_Truncated;

		// the cursor takes its own references to the glyphs
		for(int g = 0; g < pEntry->m_NumGlyphs; ++g)
			pEntry->m_pGlyphs[g].m_pGlyph->m_RefCount++;
		pCursor->m_pfnReleaseGlyphs = ReleaseGlyphRefs;
		pCursor->m_pReleaseGlyphsUser = NULL;
		return;
	}

	// miss: lay out normally, then remember the result
	m_StatCacheMisses++;
	TextDeferred(pCursor, pText, Length);

	SLayoutCacheEntry *pEntry = NULL;
	for(int i = 0; i < LAYOUT_CACHE_SIZE; ++i)
	{
		if(!m_aLayoutCache[i].m_Valid)
		{
			pEntry = &m_aLayoutCache[i];
			break;
		}
		if(!pEntry || m_aLayoutCache[i].m_LastAccess < pEntry->m_LastAccess)
			pEntry = &m_aLayoutCache[i];
	}

	if(!pEntry)
		return;

	const int Slot = (int) (pEntry - m_aLayoutCache);

	// the evicted entry has to be dropped from the index before it is cleared
	if(pEntry->m_Valid)
	{
		CLayoutCacheKey OldKey;
		OldKey.m_Hash = pEntry->m_Key;
		m_LayoutCacheIndex.remove(OldKey);
	}

	FreeLayoutEntry(pEntry);
	pEntry->m_Valid = true;
	pEntry->m_Key = Hash;
	pEntry->m_LastAccess = ++m_LayoutCacheTick;
	m_LayoutCacheIndex.set(CacheKey, Slot);

	pEntry->m_NumGlyphs = pCursor->m_Glyphs.size();
	if(pEntry->m_NumGlyphs > 0)
	{
		pEntry->m_pGlyphs = (CScaledGlyph *) mem_alloc(sizeof(CScaledGlyph) * pEntry->m_NumGlyphs);
		mem_copy(pEntry->m_pGlyphs, pCursor->m_Glyphs.base_ptr(), sizeof(CScaledGlyph) * pEntry->m_NumGlyphs);
		for(int g = 0; g < pEntry->m_NumGlyphs; ++g)
			pEntry->m_pGlyphs[g].m_pGlyph->m_RefCount++;
	}

	pEntry->m_NumTextColors = pCursor->m_TextColors.size();
	if(pEntry->m_NumTextColors > 0)
	{
		pEntry->m_pTextColors = (vec4 *) mem_alloc(sizeof(vec4) * pEntry->m_NumTextColors);
		mem_copy(pEntry->m_pTextColors, pCursor->m_TextColors.base_ptr(), sizeof(vec4) * pEntry->m_NumTextColors);
	}

	pEntry->m_NumSecondaryColors = pCursor->m_SecondaryColors.size();
	if(pEntry->m_NumSecondaryColors > 0)
	{
		pEntry->m_pSecondaryColors = (vec4 *) mem_alloc(sizeof(vec4) * pEntry->m_NumSecondaryColors);
		mem_copy(pEntry->m_pSecondaryColors, pCursor->m_SecondaryColors.base_ptr(), sizeof(vec4) * pEntry->m_NumSecondaryColors);
	}

	pEntry->m_Width = pCursor->m_Width;
	pEntry->m_Height = pCursor->m_Height;
	pEntry->m_NextLineAdvanceY = pCursor->m_NextLineAdvanceY;
	pEntry->m_Advance = pCursor->m_Advance;
	pEntry->m_LineCount = pCursor->m_LineCount;
	pEntry->m_CharCount = pCursor->m_CharCount;
	pEntry->m_PageCountWhenDrawn = pCursor->m_PageCountWhenDrawn;
	pEntry->m_Truncated = pCursor->m_Truncated;
}

void CTextRender::TextNewline(CTextCursor *pCursor)
{
	if(pCursor->m_Truncated || pCursor->m_SkipTextRender)
		return;

	// Alignment
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	int ScreenWidth = Graphics()->ScreenWidth();
	int ScreenHeight = Graphics()->ScreenHeight();
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	int MaxLines = pCursor->m_MaxLines;
	if(MaxLines < 0)
		MaxLines = (ScreenY1 - ScreenY0) / pCursor->m_FontSize;

	if(pCursor->m_LineCount >= MaxLines)
	{
		pCursor->m_LineCount = MaxLines;
		pCursor->m_Truncated = true;
		return;
	}

	vec2 ScreenScale = vec2(ScreenWidth / (ScreenX1 - ScreenX0), ScreenHeight / (ScreenY1 - ScreenY0));
	pCursor->m_LineCount++;
	pCursor->m_Advance.y = pCursor->m_LineSpacing + pCursor->m_NextLineAdvanceY;
	pCursor->m_Advance.x = 0;
	pCursor->m_StartOfLine = true;
	float NextAdvanceY = pCursor->m_Advance.y + pCursor->m_FontSize;
	NextAdvanceY = (int) (NextAdvanceY * ScreenScale.y) / ScreenScale.y;
	pCursor->m_NextLineAdvanceY = NextAdvanceY;
}

void CTextRender::TextAdvance(CTextCursor *pCursor, float AdvanceX)
{
	// Alignment
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	int ScreenWidth = Graphics()->ScreenWidth();
	int ScreenHeight = Graphics()->ScreenHeight();
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	vec2 ScreenScale = vec2(ScreenWidth / (ScreenX1 - ScreenX0), ScreenHeight / (ScreenY1 - ScreenY0));

	int LineWidth = pCursor->m_Advance.x + AdvanceX;
	float MaxWidth = pCursor->m_MaxWidth;
	if(MaxWidth < 0)
		MaxWidth = INFINITY;
	if(LineWidth > MaxWidth)
	{
		TextNewline(pCursor);
		pCursor->m_Advance.x = LineWidth - MaxWidth;
	}
	else
	{
		pCursor->m_Advance.x = LineWidth;
	}

	pCursor->m_Advance.x = (int) (pCursor->m_Advance.x * ScreenScale.x) / ScreenScale.x;
}

void CTextRender::MakeSDFParams(const CTextCursor *pCursor, int StartGlyph, bool Outline, IGraphics::CTextSDFParams *pParams) const
{
	pParams->m_Enable = true;

	// screen pixels a distance field texel covers; from the layout, not the
	// graphics screen, so the SDF cannot drift from the glyphs
	int PixelSize = pCursor->m_PixelSize;
	if(PixelSize < 1)
		PixelSize = 1;
	const float Scale = (float) PixelSize / SDF_BASE_SIZE;

	// maps the normalized distance value onto a one pixel alpha ramp
	pParams->m_Gain = (255.0f / 128.0f) * SDF_SPREAD * Scale;

	// the outline and shadow color come from the first drawn glyph; callers may
	// draw the cursor in segments with different colors
	vec4 SecondaryColor = m_TextSecondaryColor;
	if(pCursor->m_Glyphs.size() > 0)
	{
		const int Index = StartGlyph >= 0 && StartGlyph < pCursor->m_Glyphs.size() ? StartGlyph : 0;
		const int ColorIndex = pCursor->m_Glyphs[Index].m_SecondaryColorIndex;
		if(ColorIndex >= 0 && ColorIndex < pCursor->m_SecondaryColors.size())
			SecondaryColor = pCursor->m_SecondaryColors[ColorIndex];
	}

	if(Outline)
	{
		const int OutlineThickness = AdjustOutlineThicknessToFontSize(1, PixelSize);
		pParams->m_OutlineOffset = OutlineThickness * 128.0f / (255.0f * SDF_SPREAD * Scale);
		pParams->m_OutlineColor = SecondaryColor;
	}
	else
	{
		pParams->m_OutlineOffset = 0.0f;
		pParams->m_OutlineColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
	}
}

void CTextRender::DrawText(CTextCursor *pCursor, vec2 Offset, bool Outline, bool Secondary, float Alpha, int StartGlyph, int NumGlyphs)
{
	STextDrawPass Pass;
	Pass.m_Offset = Offset;
	Pass.m_Secondary = Secondary;
	DrawTextPasses(pCursor, &Pass, 1, Outline, Alpha, StartGlyph, NumGlyphs);
}

void CTextRender::DrawTextPasses(CTextCursor *pCursor, const STextDrawPass *pPasses, int NumPasses, bool Outline, float Alpha, int StartGlyph, int NumGlyphs)
{
	int NumQuads = pCursor->m_Glyphs.size();
	if(NumQuads <= 0 || NumPasses <= 0)
		return;

	if(NumGlyphs < 0)
		NumGlyphs = NumQuads;

	const bool TimeDraw = m_pConfig && m_pConfig->m_DbgTextStats;
	const int64 DrawStart = TimeDraw ? time_get() : 0;
	m_StatDrawPasses += NumPasses;
	m_StatDrawCalls++;

	int EndGlyphs = StartGlyph + NumGlyphs;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	int ScreenWidth = Graphics()->ScreenWidth();
	int ScreenHeight = Graphics()->ScreenHeight();
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	vec2 ScreenScale = vec2(ScreenWidth / (ScreenX1 - ScreenX0), ScreenHeight / (ScreenY1 - ScreenY0));

	int HorizontalAlign = pCursor->m_Align & TEXTALIGN_MASK_HORI;
	CTextBoundingBox AlignBox = pCursor->AlignedBoundingBox();
	vec2 AlignOffset = vec2(AlignBox.x, AlignBox.y);

	// all passes share the same SDF parameters, so compute them once
	IGraphics::CTextSDFParams SDFParams;
	MakeSDFParams(pCursor, StartGlyph, Outline, &SDFParams);
	Graphics()->SetTextSDF(SDFParams);

	// requeue glyphs from recycled pages; ones not ready in time are skipped below
	TextRefreshGlyphs(pCursor);

	// upload all glyphs rendered since the last draw in one batch per page
	m_pGlyphMap->FlushUploads();

	vec4 LastColor = vec4(-1, -1, -1, -1);
	Graphics()->TextureSet(m_pGlyphMap->GetTexture());
	Graphics()->QuadsBegin();

	vec2 Anchor = pCursor->m_CursorPos + AlignOffset;

	// batch consecutive glyphs that share a color into one draw call
	enum
	{
		MAX_BATCH = 256
	};
	IGraphics::CQuadItem aBatchQuads[MAX_BATCH];
	vec4 aBatchUV[MAX_BATCH];
	int NumBatch = 0;

	const CScaledGlyph *pGlyphs = pCursor->m_Glyphs.base_ptr();
	const float CursorWidth = pCursor->m_Width;
	// the range check can be skipped when this draw covers the whole cursor
	const bool FullRange = StartGlyph <= 0 && EndGlyphs >= NumQuads;

	const int64 SetupEnd = TimeDraw ? time_get() : 0;

	for(int Pass = 0; Pass < NumPasses; ++Pass)
	{
		// the same for every glyph of the pass, so computed once here
		const vec2 OffsetScaled = pPasses[Pass].m_Offset / ScreenScale;
		const bool Secondary = pPasses[Pass].m_Secondary;
		const vec4 *pColors = Secondary ? pCursor->m_SecondaryColors.base_ptr() : pCursor->m_TextColors.base_ptr();
		const float AnchorY = (int) (Anchor.y * ScreenScale.y) / ScreenScale.y;

		int Line = -1;
		float AnchorX = 0.0f;

		for(int i = NumQuads - 1; i >= 0; --i)
		{
			const CScaledGlyph &rScaled = pGlyphs[i];
			const CGlyph *pGlyph = rScaled.m_pGlyph;

			if(Line != rScaled.m_Line)
			{
				Line = rScaled.m_Line;
				float LineOffset;
				if(HorizontalAlign == TEXTALIGN_RIGHT)
					LineOffset = CursorWidth - (rScaled.m_Advance.x + pGlyph->m_AdvanceX * rScaled.m_Size);
				else if(HorizontalAlign == TEXTALIGN_CENTER)
					LineOffset = (CursorWidth - (rScaled.m_Advance.x + pGlyph->m_AdvanceX * rScaled.m_Size)) / 2.0f;
				else
					LineOffset = 0.0f;
				AnchorX = (int) ((Anchor.x + LineOffset) * ScreenScale.x) / ScreenScale.x;
			}

			if(!FullRange && (i < StartGlyph || i >= EndGlyphs))
				continue;
			if(!m_pGlyphMap->IsGlyphValid(pGlyph))
				continue;

			m_pGlyphMap->TouchPage(pGlyph->m_AtlasIndex);

			const vec4 Color = pColors[Secondary ? rScaled.m_SecondaryColorIndex : rScaled.m_TextColorIndex];
			if(Color != LastColor)
			{
				// the pending quads still use the previous color
				if(NumBatch > 0)
				{
					Graphics()->QuadsDrawTLWithUV(aBatchQuads, aBatchUV, NumBatch, 0);
					NumBatch = 0;
				}
				Graphics()->SetColor(Color.r, Color.g, Color.b, Color.a * Alpha);
				LastColor = Color;
			}

			const vec2 QuadPosition = vec2(AnchorX, AnchorY) + rScaled.m_Advance + vec2(pGlyph->m_BearingX, pGlyph->m_BearingY) * rScaled.m_Size + OffsetScaled;
			aBatchQuads[NumBatch] = IGraphics::CQuadItem(QuadPosition.x, QuadPosition.y, pGlyph->m_Width * rScaled.m_Size, pGlyph->m_Height * rScaled.m_Size);
			aBatchUV[NumBatch] = vec4(pGlyph->m_aUvCoords[0], pGlyph->m_aUvCoords[1], pGlyph->m_aUvCoords[2], pGlyph->m_aUvCoords[3]);
			NumBatch++;

			if(TimeDraw)
			{
				m_StatGlyphs++;
				m_StatQuadPixels += (long long) (pGlyph->m_Width * pGlyph->m_Height * rScaled.m_Size * rScaled.m_Size * ScreenScale.x * ScreenScale.y);
			}

			if(NumBatch == MAX_BATCH)
			{
				Graphics()->QuadsDrawTLWithUV(aBatchQuads, aBatchUV, NumBatch, 0);
				NumBatch = 0;
			}
		}
	}

	const int64 LoopEnd = TimeDraw ? time_get() : 0;

	if(NumBatch > 0)
		Graphics()->QuadsDrawTLWithUV(aBatchQuads, aBatchUV, NumBatch, 0);

	Graphics()->QuadsEnd();

	// leave the SDF state enabled: SetTextSDF() re-arms it per draw, and
	// resetting it here would end the batch

	if(TimeDraw)
	{
		const int64 End = time_get();
		m_StatDrawTime += End - DrawStart;
		m_StatSetupTime += SetupEnd - DrawStart;
		m_StatLoopTime += LoopEnd - SetupEnd;
		m_StatEndTime += End - LoopEnd;
	}
}

void CTextRender::TextPlain(CTextCursor *pCursor, const char *pText, int Length)
{
	TextDeferred(pCursor, pText, Length);
	DrawTextPlain(pCursor, 1.0f, 0, -1);
}

void CTextRender::TextOutlined(CTextCursor *pCursor, const char *pText, int Length)
{
	TextDeferred(pCursor, pText, Length);
	DrawTextOutlined(pCursor, 1.0f, 0, -1);
}

void CTextRender::TextShadowed(CTextCursor *pCursor, const char *pText, int Length, vec2 ShadowOffset)
{
	TextDeferred(pCursor, pText, Length);
	DrawTextShadowed(pCursor, ShadowOffset, 1.0f, 0, -1);
}

void CTextRender::DrawTextPlain(CTextCursor *pCursor, float Alpha, int StartGlyph, int NumGlyphs)
{
	DrawText(pCursor, vec2(0, 0), false, false, Alpha, StartGlyph, NumGlyphs);
}

void CTextRender::DrawTextOutlined(CTextCursor *pCursor, float Alpha, int StartGlyph, int NumGlyphs)
{
	DrawText(pCursor, vec2(0, 0), true, false, Alpha, StartGlyph, NumGlyphs);
}

void CTextRender::DrawTextShadowed(CTextCursor *pCursor, vec2 ShadowOffset, float Alpha, int StartGlyph, int NumGlyphs)
{
	// shadow = the glyph shape shifted by ShadowOffset in the secondary color;
	// both passes share the texture and SDF parameters, so they batch into one draw
	STextDrawPass aPasses[2];
	aPasses[0].m_Offset = ShadowOffset;
	aPasses[0].m_Secondary = true;
	aPasses[1].m_Offset = vec2(0.0f, 0.0f);
	aPasses[1].m_Secondary = false;
	DrawTextPasses(pCursor, aPasses, 2, false, Alpha, StartGlyph, NumGlyphs);
}

int CTextRender::CharToGlyph(CTextCursor *pCursor, int NumChars, float *pLineWidth)
{
	int CursorChars = 0;
	int NumGlyphs = pCursor->m_Glyphs.size();
	if(NumGlyphs == 0 || NumChars == 0)
	{
		if(pLineWidth)
			*pLineWidth = 0.0f;
		return 0;
	}

	int GlyphIndex = -1;
	for(int i = 0; i < NumGlyphs; ++i)
	{
		CursorChars += pCursor->m_Glyphs[i].m_NumChars;
		if(CursorChars > NumChars)
		{
			GlyphIndex = i;
			break;
		}
	}

	int LastGlyphIndex = GlyphIndex;

	if(GlyphIndex < 0)
	{
		GlyphIndex = NumGlyphs;
		LastGlyphIndex = GlyphIndex - 1;
	}

	if(pLineWidth)
	{
		const int Line = pCursor->m_Glyphs[LastGlyphIndex].m_Line;

		for(; LastGlyphIndex < NumGlyphs; ++LastGlyphIndex)
		{
			if(LastGlyphIndex + 1 >= NumGlyphs)
				break;

			if(pCursor->m_Glyphs[LastGlyphIndex].m_Line > Line)
			{
				LastGlyphIndex -= 1;
				break;
			}
		}

		const CScaledGlyph &rScaled = pCursor->m_Glyphs[LastGlyphIndex];
		*pLineWidth = rScaled.m_Advance.x + rScaled.m_pGlyph->m_AdvanceX * rScaled.m_Size;
	}

	return GlyphIndex;
}

vec2 CTextRender::CaretPosition(CTextCursor *pCursor, int NumChars)
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	int ScreenWidth = Graphics()->ScreenWidth();
	int ScreenHeight = Graphics()->ScreenHeight();
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	vec2 ScreenScale = vec2(ScreenWidth / (ScreenX1 - ScreenX0), ScreenHeight / (ScreenY1 - ScreenY0));
	float Size = pCursor->m_FontSize;
	int PixelSize = (int) (Size * ScreenScale.y);
	Size = PixelSize / ScreenScale.y;

	int NumGlyphs = pCursor->m_Glyphs.size();
	float LineWidth;
	int GlyphIndex = CharToGlyph(pCursor, NumChars, &LineWidth);

	int HorizontalAlign = pCursor->m_Align & TEXTALIGN_MASK_HORI;
	int VerticalAlign = pCursor->m_Align & TEXTALIGN_MASK_VERT;

	vec2 Offset = vec2(0, 0);
	float LineOffset = 0.0f;

	if(HorizontalAlign == TEXTALIGN_RIGHT)
	{
		Offset.x = -pCursor->m_Width;
		LineOffset = pCursor->m_Width - LineWidth;
	}
	else if(HorizontalAlign == TEXTALIGN_CENTER)
	{
		Offset.x = -pCursor->m_Width / 2.0f;
		LineOffset = (pCursor->m_Width - LineWidth) / 2.0f;
	}

	if(VerticalAlign == TEXTALIGN_BOTTOM)
		Offset.y = -pCursor->m_Height + Size * 1.35f;
	else if(VerticalAlign == TEXTALIGN_MIDDLE)
		Offset.y = -pCursor->m_Height / 2.0f + Size * 0.675f;

	if(GlyphIndex == 0 || NumGlyphs == 0)
		return pCursor->m_CursorPos + Offset;

	if(GlyphIndex < NumGlyphs)
		return pCursor->m_CursorPos + pCursor->m_Glyphs[GlyphIndex].m_Advance + Offset;

	CScaledGlyph *pLastScaled = &pCursor->m_Glyphs[NumGlyphs - 1];
	return pCursor->m_CursorPos + pLastScaled->m_Advance + Offset + vec2(pLastScaled->m_pGlyph->m_AdvanceX + LineOffset, 0) * pLastScaled->m_Size;
}

IEngineTextRender *CreateEngineTextRender() { return new CTextRender; }
