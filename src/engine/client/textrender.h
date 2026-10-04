/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef ENGINE_CLIENT_TEXTRENDER_H
#define ENGINE_CLIENT_TEXTRENDER_H

#include <base/tl/hashtable.h>
#include <base/system/time.h>
#include <base/vmath.h>
#include <engine/textrender.h>

#include <engine/shared/memheap.h>

#include "font_download.h"

struct _json_value;
typedef struct _json_value json_value;

class CConfig;

// ft2 texture
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

// text shaping
#include <hb.h>

enum
{
	MAX_FACES = 16,
	MAX_CHARACTERS = 64,
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
	// padding in reference texels; bounds how thick an outline can be drawn
	SDF_SPREAD = 10,

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
	// FreeType glyph index on m_Face; the pair identifies the record in the cache
	int m_GlyphIndex;
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
	FT_Face m_Face;
	int m_GlyphIndex;

	friend bool operator==(const CGlyphIndex &l, const CGlyphIndex &r)
	{
		return l.m_Face == r.m_Face && l.m_GlyphIndex == r.m_GlyphIndex;
	};
};

class CGlyphSearchFunction : public basic_table_function
{
public:
	static unsigned hash(CGlyphIndex key)
	{
		// pointer mixed with the glyph index
		unsigned long long Hash = (unsigned long long) (size_t) key.m_Face;
		Hash ^= (unsigned long long) (unsigned) key.m_GlyphIndex + 0x9e3779b97f4a7c15ull + (Hash << 6) + (Hash >> 2);
		return (unsigned) Hash;
	}
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

	// CPU mirror of the atlas; touched page regions are uploaded in one batch
	unsigned char *m_pStaging;

	// runtime atlas geometry, fixed by InitTexture()
	int m_TextureSize;
	int m_PageSize;

	// frame counter used for the glyph LRU
	int m_Frame;

	// glyphs that only have their metrics loaded; rasterization is deferred
	array<CGlyph *> m_PendingGlyphs;
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
	// wins over the default face for every codepoint it covers, replacing the text face's own emoji
	FT_Face m_EmojiFace;
	FT_Face m_aFallbackFaces[MAX_FACES];
	int m_NumFallbackFaces;

	FT_Face m_aFtFaces[MAX_FACES];
	int m_NumFtFaces;
	// one HarfBuzz font per FreeType face
	hb_font_t *m_aHbFonts[MAX_FACES];

	void InitTexture(int Width, int Height);
	int FitGlyph(int Width, int Height, ivec2 *Position);
	void UploadGlyph(int TextureIndex, int PosX, int PosY, int Width, int Height, const unsigned char *pData);
	bool SetFaceByName(FT_Face *pFace, const char *pFamilyName);

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
	bool SetEmojiFaceByName(const char *pFamilyName);

	bool RenderGlyph(CGlyph *pGlyph, bool Render);
	CGlyph *GetGlyph(int Chr, bool Render);
	// glyph record for an explicit (face, glyph index), used by the shaper
	CGlyph *GetGlyphByIndex(FT_Face Face, int GlyphIndex, bool Render);
	// resolves the codepoint through the default/variant/fallback/emoji faces
	int GetCharGlyph(int Chr, FT_Face *pFace);
	// HarfBuzz font matching a FreeType face, or NULL
	hb_font_t *HBFont(FT_Face Face) const;

	// Pins the face to SDF_BASE_SIZE, the size shaping and rasterization share.
	// True when the size changed, so the caller can tell hb-ft to re-read its metrics.
	bool PrepareFaceForShaping(FT_Face Face);

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

	// runtime-fetched fonts, started by LoadFontsAsync() and finished before the loading screen closes
	CFontDownloader m_Downloader;
	int m_NumLoadedFaces;
	// which entries of "font files" are already in the glyph map
	bool m_aLoadedFonts[MAX_FACES];

	// support regional variant fonts
	int m_NumVariants;
	CFontLanguageVariant *m_pVariants;
	// active language variant family, re-applied after a font download provides it
	char m_aVariantFamilyName[FONT_NAME_SIZE];

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

	// one glyph produced by HarfBuzz for a run of source text
	struct SShapedGlyph
	{
		CGlyph *m_pGlyph;
		// cluster byte offset in the shaped range; m_CharCount is set on the first glyph only
		int m_CharOffset;
		int m_CharCount;
		// kerning already applied; in SDF reference units, scaled by the caller
		float m_AdvanceX;
		float m_OffsetX;
		float m_OffsetY;
	};

	// shaping scratch, reused between MakeWord() calls
	hb_buffer_t *m_pShapeBuffer;
	array<char> m_ShapeTextBuffer;
	array<SShapedGlyph> m_ShapedGlyphs;

	// One line is shaped once and reused by all of its words, which keeps the cost
	// linear instead of O(n^2) in the line length. The text is compared on lookup so
	// a recycled buffer address cannot produce a stale hit.
	array<char> m_ShapeCacheText;
	int m_ShapeCacheValid;

	// Shapes a UTF-8 range into per-font runs; false if a codepoint has no face.
	// Always shapes at SDF_BASE_SIZE, so advances are size independent and float precise.
	bool ShapeText(const char *pText, int Length, bool Render);

	// Shapes [pText, pText+Length) unless the cache already holds that text. The range
	// must start at a line boundary. False when a codepoint has no usable face.
	bool ShapeTextCached(const char *pText, int Length, bool Render);

	// Bytes in the line starting at pText, up to the next newline.
	static int LineLength(const char *pText, const char *pEnd);

	// Drops the cached line after a font change alters glyph resolution.
	void InvalidateShapeCache();

	// pLineStart is the first byte of the line pText belongs to; the line is shaped
	// once and each of its words is sliced out of that result
	CWordWidthHint MakeWord(CTextCursor *pCursor, const char *pLineStart, const char *pText, const char *pEnd, float Size, int PixelSize, vec2 ScreenScale);
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

	virtual void Init() override;
	virtual void Update() override;
	virtual void Shutdown() override;

	virtual void LoadFonts(IStorage *pStorage, IConsole *pConsole) override;
	virtual void SetFontLanguageVariant(const char *pLanguageFile) override;
	// applies m_aVariantFamilyName to the glyph map and prebakes ASCII
	void ApplyFontLanguageVariant();

	// loads local fonts and starts the downloads in fonts/index.json; false while one is in flight
	virtual bool LoadFontsAsync(IStorage *pStorage, IConsole *pConsole) override;
	// advances the downloads and loads whatever finished; never blocks
	virtual void PollFontDownloads(IStorage *pStorage, IConsole *pConsole) override;
	// one last poll so results arriving at the deadline are still picked up
	virtual void FinishFontDownloads(IStorage *pStorage, IConsole *pConsole) override;
	virtual bool FontsPending() const override;
	virtual float FontDownloadProgress() const override;

	// loads every font in the parsed index and returns the number of faces added;
	// AlreadyLoaded marks the entries to skip
	int LoadFontFiles(IStorage *pStorage, IConsole *pConsole, const json_value *pJsonData, bool AlreadyLoaded[MAX_FACES]);

	virtual void TextColor(const vec4 &Color) override { m_TextColor = Color; }
	virtual void TextSecondaryColor(const vec4 &Color) override { m_TextSecondaryColor = Color; }

	virtual vec4 GetColor() const override { return m_TextColor; }
	virtual vec4 GetSecondaryColor() const override { return m_TextSecondaryColor; }

	virtual float TextWidth(float FontSize, const char *pText, int Length) override;
	virtual void TextDeferred(CTextCursor *pCursor, const char *pText, int Length) override;
	virtual void TextDeferredCached(CTextCursor *pCursor, const char *pText, int Length) override;
	virtual void TextNewline(CTextCursor *pCursor) override;
	virtual void TextAdvance(CTextCursor *pCursor, float AdvanceX) override;
	virtual void TextPlain(CTextCursor *pCursor, const char *pText, int Length) override;
	virtual void TextOutlined(CTextCursor *pCursor, const char *pText, int Length) override;
	virtual void TextShadowed(CTextCursor *pCursor, const char *pText, int Length, vec2 ShadowOffset) override;

	virtual void DrawTextPlain(CTextCursor *pCursor, float Alpha, int StartGlyph, int NumGlyphs) override;
	virtual void DrawTextOutlined(CTextCursor *pCursor, float Alpha, int StartGlyph, int NumGlyphs) override;
	virtual void DrawTextShadowed(CTextCursor *pCursor, vec2 ShadowOffset, float Alpha, int StartGlyph, int NumGlyphs) override;

	virtual int CharToGlyph(CTextCursor *pCursor, int NumChars, float *pLineWidth = 0) override;
	virtual vec2 CaretPosition(CTextCursor *pCursor, int NumChars) override;
};

#endif
