/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef ENGINE_CLIENT_TEXTRENDER_H
#define ENGINE_CLIENT_TEXTRENDER_H

#include <base/tl/hashtable.h>
#include <base/vmath.h>
#include <engine/textrender.h>

#include <engine/shared/memheap.h>

class CConfig;

// ft2 texture
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

enum
{
	MAX_FACES = 16,
	MAX_CHARACTERS = 64,
	MAX_KERNING_CACHE = 1024,
	MAX_GLYPHS = 32768,
	// square, single channel glyph atlas; the actual size is chosen at startup
	// between TEXTURE_SIZE and MAX_TEXTURE_SIZE
	TEXTURE_SIZE = 2048,
	MAX_TEXTURE_SIZE = 4096,
	NUM_PAGES_PER_DIM = 4, // 16 pages total

	FONT_NAME_SIZE = 128,
};

// glyphs are rasterized once at a single reference size and scaled when drawn
enum
{
	// reference size the distance field is rasterized at
	SDF_BASE_SIZE = 64,
	SDF_SPREAD = 8,

	// the field is derived from a supersampled coverage bitmap, so the outline is
	// located to a fraction of a texel
	SDF_SUPERSAMPLE = 2,

	// pending glyphs are spread over frames with a time budget so a screen full of
	// new text does not stall a frame
	SDF_RASTERIZE_BUDGET_MS = 4,
	SDF_MIN_GLYPHS_PER_FRAME = 6,
};

struct CGlyph
{
	int m_ID; // unicode code point
	int m_GlyphIndex; // FreeType glyph index of m_ID on m_Face
	int m_AtlasIndex;
	int m_PageID;
	FT_Face m_Face;

	// cursor reference count and frame of last use, for LRU eviction
	int m_RefCount;
	int m_LastAccess;

	// set while the glyph sits in the rasterization queue
	bool m_Pending;

	bool m_Rendered;
	float m_Width;
	float m_Height;
	float m_BearingX;
	float m_BearingY;
	float m_AdvanceX;
	float m_aUvCoords[4];
};

struct CGlyphIndex
{
	int m_ID;

	friend bool operator==(const CGlyphIndex &l, const CGlyphIndex &r)
	{
		return l.m_ID == r.m_ID;
	};
};

struct CGlyphKerning
{
	int m_PixelSize;
	int m_LeftID;
	int m_RightID;

	friend bool operator==(const CGlyphKerning &l, const CGlyphKerning &r)
	{
		return l.m_PixelSize == r.m_PixelSize && l.m_LeftID == r.m_LeftID && l.m_RightID == r.m_RightID;
	};
};

class CGlyphSearchFunction : public basic_table_function
{
public:
	static unsigned hash(CGlyphIndex key) { return (unsigned) key.m_ID; }
	static unsigned hash(CGlyphKerning key) { return (key.m_PixelSize << 24) + (key.m_LeftID << 10) + key.m_RightID; }
};

// reusable scratch memory for the distance field generation
struct CSdfScratch
{
	float *m_pFloats;
	int m_NumFloats;
	int *m_pInts;
	int m_NumInts;
};

class CGlyphMap
{
	class CAtlas
	{
	public:
		array<ivec3> m_Sections;

		int m_ID;
		int m_Width;
		int m_Height;

		ivec2 m_Offset;

		int m_LastFrameAccess;
		int m_Access;
		bool m_IsEmpty;

		// region of this page that has not been uploaded to the texture yet
		bool m_Dirty;
		int m_DirtyX0;
		int m_DirtyY0;
		int m_DirtyX1;
		int m_DirtyY1;

		CAtlas()
		{
			m_LastFrameAccess = 0;
			m_Access = 0;
			m_Dirty = false;
			m_DirtyX0 = m_DirtyY0 = m_DirtyX1 = m_DirtyY1 = 0;
		}
		int TrySection(int Index, int Width, int Height);
		void Init(int Index, int X, int Y, int Width, int Height);
		ivec2 Add(int Width, int Height);
		void MarkDirty(int X0, int Y0, int X1, int Y1);
	};
	IGraphics *m_pGraphics;
	IGraphics::CTextureHandle m_Texture;
	CAtlas m_aAtlasPages[NUM_PAGES_PER_DIM * NUM_PAGES_PER_DIM];
	int m_ActiveAtlasIndex;
	hash_table<CGlyphIndex, CGlyph *, 64, CGlyphSearchFunction> m_Glyphs;
	hash_table<CGlyphKerning, vec2, 64, CGlyphSearchFunction> m_Kernings;

	// CPU mirror of the atlas; touched page regions are uploaded in one batch
	unsigned char *m_pStaging;

	// runtime atlas geometry, fixed by InitTexture()
	int m_TextureSize;
	int m_PageSize;

	// frame counter used for the glyph LRU
	int m_Frame;

	// glyphs that only have their metrics loaded; rasterization is deferred
	array<int> m_PendingGlyphs;
	int m_PendingFrame;
	bool m_PendingBudgetSpent;

	// scratch buffers reused while generating a distance field
	CSdfScratch m_SdfScratch;

	int m_NumTotalPages;
	// atlas pages recycled since startup (the log message is throttled)
	int m_NumPageRecycles;
	// glyphs rasterized into the atlas since startup
	int m_NumRasterized;

	FT_Face m_DefaultFace;
	FT_Face m_VariantFace;
	FT_Face m_aFallbackFaces[MAX_FACES];
	int m_NumFallbackFaces;

	FT_Face m_aFtFaces[MAX_FACES];
	int m_NumFtFaces;

	void InitTexture(int Width, int Height);
	int FitGlyph(int Width, int Height, ivec2 *Position);
	void UploadGlyph(int TextureIndex, int PosX, int PosY, int Width, int Height, const unsigned char *pData);
	bool SetFaceByName(FT_Face *pFace, const char *pFamilyName);
	int GetCharGlyph(int Chr, FT_Face *pFace);

	// loads the metrics only; the glyph is queued for rasterization
	bool LoadGlyphMetrics(CGlyph *pGlyph);

	// glyph record allocation and LRU eviction
	CGlyph *AllocateGlyph();
	void FreeGlyph(CGlyph *pGlyph);
	void EvictGlyphs();
	static void FreeGlyphCallback(CGlyph *&pGlyph, void *pUser);

	struct CEvictContext
	{
		CGlyph *m_pVictim;
		int m_VictimID;
	};
	static void EvictScanCallback(CGlyph *&pGlyph, void *pUser);

public:
	CGlyphMap(IGraphics *pGraphics, FT_Library FtLibrary);
	~CGlyphMap();

	IGraphics::CTextureHandle GetTexture() const { return m_Texture; }
	FT_Face GetDefaultFace() const { return m_DefaultFace; }
	int NumPageRecycles() const { return m_NumPageRecycles; }
	int NumRasterized() const { return m_NumRasterized; }
	int AddFace(FT_Face Face);
	void SetDefaultFaceByName(const char *pFamilyName);
	void AddFallbackFaceByName(const char *pFamilyName);
	void SetVariantFaceByName(const char *pFamilyName);

	bool RenderGlyph(CGlyph *pGlyph, bool Render);
	CGlyph *GetGlyph(int Chr, bool Render);
	vec2 Kerning(CGlyph *pLeft, CGlyph *pRight, int PixelSize);

	// queues a glyph for rasterization by ProcessPendingGlyphs()
	void QueueGlyph(CGlyph *pGlyph);

	// rasterizes queued glyphs within a per frame budget unless Unlimited
	void ProcessPendingGlyphs(bool Unlimited = false);

	// a glyph is only drawable while its atlas page still holds its pixels
	bool IsGlyphValid(const CGlyph *pGlyph) const
	{
		return pGlyph && pGlyph->m_Rendered && pGlyph->m_AtlasIndex >= 0 &&
		       m_aAtlasPages[pGlyph->m_AtlasIndex].m_ID == pGlyph->m_PageID;
	}

	// Uploads all pending glyph data to the atlas texture.
	void FlushUploads();

	int NumTotalPages() const { return m_NumTotalPages; }
	void TouchPage(int Index);
	void PagesAccessReset();
};

struct CFontLanguageVariant
{
	char m_aLanguageFile[IO_MAX_PATH_LENGTH];
	char m_aFamilyName[FONT_NAME_SIZE];
};

struct CWordWidthHint
{
	float m_EffectiveAdvanceX;
	int m_CharCount;
	int m_GlyphCount;
	bool m_EndsWithNewline;
	bool m_IsBroken;
};

class CTextRender : public IEngineTextRender
{
	IGraphics *m_pGraphics;
	IGraphics *Graphics() { return m_pGraphics; }

	vec4 m_TextColor;
	vec4 m_TextSecondaryColor;

	CGlyphMap *m_pGlyphMap;
	void *m_apFontData[MAX_FACES];

	// support regional variant fonts
	int m_NumVariants;
	int m_CurrentVariant;
	CFontLanguageVariant *m_pVariants;

	FT_Library m_FTLibrary;

	// layout cache: TextDeferredCached() results keyed by the layout inputs, so
	// a whole UI's worth of labels fits without thrashing
	enum
	{
		LAYOUT_CACHE_SIZE = 512,
		LAYOUT_CACHE_BUCKETS = 512
	};
	struct CLayoutCacheKey
	{
		unsigned long long m_Hash;
		friend bool operator==(const CLayoutCacheKey &a, const CLayoutCacheKey &b) { return a.m_Hash == b.m_Hash; }
	};
	class CLayoutCacheFunction : public basic_table_function
	{
	public:
		static unsigned hash(CLayoutCacheKey Key) { return (unsigned) (Key.m_Hash ^ (Key.m_Hash >> 32)); }
	};
	struct SLayoutCacheEntry
	{
		bool m_Valid;
		unsigned long long m_Key;
		int m_LastAccess;

		CScaledGlyph *m_pGlyphs;
		int m_NumGlyphs;
		vec4 *m_pTextColors;
		int m_NumTextColors;
		vec4 *m_pSecondaryColors;
		int m_NumSecondaryColors;

		float m_Width;
		float m_Height;
		float m_NextLineAdvanceY;
		vec2 m_Advance;
		int m_LineCount;
		int m_CharCount;
		int m_PageCountWhenDrawn;
		bool m_Truncated;
	};
	SLayoutCacheEntry m_aLayoutCache[LAYOUT_CACHE_SIZE];
	// maps a layout key to its slot in m_aLayoutCache
	hash_table<CLayoutCacheKey, int, LAYOUT_CACHE_BUCKETS, CLayoutCacheFunction> m_LayoutCacheIndex;
	int m_LayoutCacheTick;

	// debug statistics, printed once per second when dbg_text_stats is set
	CConfig *m_pConfig;
	int m_StatFrame;
	int m_StatDrawPasses;
	int m_StatDrawCalls;
	int m_StatGlyphs;
	long long m_StatQuadPixels;
	int m_StatLayoutCalls;
	int m_StatCacheHits;
	int m_StatCacheMisses;
	int m_StatRasterized;
	int m_StatPageRecycles;
	int64 m_StatLayoutTime;
	int64 m_StatDrawTime;
	int64 m_StatSetupTime;
	int64 m_StatLoopTime;
	int64 m_StatEndTime;
	int64 m_StatLastFlushTime;

	// Frees the heap data of a cache entry and drops its glyph references.
	void FreeLayoutEntry(SLayoutCacheEntry *pEntry);
	void ClearLayoutCache();
	static void ReleaseGlyphsOf(CScaledGlyph *pGlyphs, int NumGlyphs);

	int LoadFontCollection(const char *pFilename, const void *pBuf, unsigned FileSize);

	// rasterizes the printable ASCII range up front
	void PrebakeGlyphs();

	static bool IsWestern(int Chr)
	{
		return Chr >= 0x0020 && Chr <= 0x218F;
	}

	CWordWidthHint MakeWord(CTextCursor *pCursor, const char *pText, const char *pEnd, float Size, int PixelSize, vec2 ScreenScale);
	void TextRefreshGlyphs(CTextCursor *pCursor);

	// shader parameters for rebuilding coverage and outline; the scale comes from
	// the cursor's layout, not the graphics screen
	void MakeSDFParams(const CTextCursor *pCursor, int StartGlyph, bool Outline, IGraphics::CTextSDFParams *pParams) const;

	// one color/offset combination of a text draw; a shadowed text is two of them
	struct STextDrawPass
	{
		vec2 m_Offset;
		bool m_Secondary;
	};

	// draws the glyphs of the cursor; Offset is a screen pixel offset and
	// Secondary selects the secondary color
	void DrawText(CTextCursor *pCursor, vec2 Offset, bool Outline, bool Secondary, float Alpha, int StartGlyph, int NumGlyphs);

	// all passes go into one QuadsBegin()/QuadsEnd() block, so a shadowed text is
	// a single render command
	void DrawTextPasses(CTextCursor *pCursor, const STextDrawPass *pPasses, int NumPasses, bool Outline, float Alpha, int StartGlyph, int NumGlyphs);

public:
	CTextRender();

	void Init();
	void Update();
	void Shutdown();

	void LoadFonts(IStorage *pStorage, IConsole *pConsole);
	void SetFontLanguageVariant(const char *pLanguageFile);

	void TextColor(const vec4 &Color) { m_TextColor = Color; }
	void TextSecondaryColor(const vec4 &Color) { m_TextSecondaryColor = Color; }

	vec4 GetColor() const { return m_TextColor; }
	vec4 GetSecondaryColor() const { return m_TextSecondaryColor; }

	float TextWidth(float FontSize, const char *pText, int Length);
	void TextDeferred(CTextCursor *pCursor, const char *pText, int Length);
	void TextDeferredCached(CTextCursor *pCursor, const char *pText, int Length);
	void TextNewline(CTextCursor *pCursor);
	void TextAdvance(CTextCursor *pCursor, float AdvanceX);
	void TextPlain(CTextCursor *pCursor, const char *pText, int Length);
	void TextOutlined(CTextCursor *pCursor, const char *pText, int Length);
	void TextShadowed(CTextCursor *pCursor, const char *pText, int Length, vec2 ShadowOffset);

	void DrawTextPlain(CTextCursor *pCursor, float Alpha, int StartGlyph, int NumGlyphs);
	void DrawTextOutlined(CTextCursor *pCursor, float Alpha, int StartGlyph, int NumGlyphs);
	void DrawTextShadowed(CTextCursor *pCursor, vec2 ShadowOffset, float Alpha, int StartGlyph, int NumGlyphs);

	int CharToGlyph(CTextCursor *pCursor, int NumChars, float *pLineWidth = 0);
	vec2 CaretPosition(CTextCursor *pCursor, int NumChars);
};

#endif
