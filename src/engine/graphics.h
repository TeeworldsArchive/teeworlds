/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef ENGINE_GRAPHICS_H
#define ENGINE_GRAPHICS_H

#include <base/vmath.h>
#include <base/system/time.h>

#include "kernel.h"

class CImageInfo
{
public:
	enum
	{
		FORMAT_AUTO = -1,
		FORMAT_RGB = 0,
		FORMAT_RGBA = 1,
		FORMAT_ALPHA = 2,
		// two channel tile data (index/flags); SDL_GPU has no RGB8, so use this
		FORMAT_RG = 3,
	};

	/* Variable: width
		Contains the width of the image */
	int m_Width;

	/* Variable: height
		Contains the height of the image */
	int m_Height;

	/* Variable: format
		Contains the format of the image. See <Image Formats> for more information. */
	int m_Format;

	/* Variable: data
		Pointer to the image data. */
	void *m_pData;

	static int GetPixelSize(int Format)
	{
		switch(Format)
		{
			case FORMAT_RGB: return 3;
			case FORMAT_RGBA: return 4;
			case FORMAT_ALPHA: return 1;
			case FORMAT_RG: return 2;
		}
		return 0;
	}

	int GetPixelSize() const
	{
		return GetPixelSize(m_Format);
	}
};

typedef void (*FScreenshotCallback)(void *pUser, const char *pPath);

/*
	Structure: CVideoMode
*/
class CVideoMode
{
public:
	int m_Width, m_Height;

	bool operator<(const CVideoMode &Other) { return Other.m_Width < m_Width; }
};

class IGraphics : public IInterface
{
	MACRO_INTERFACE("graphics", 0)
protected:
	int m_ScreenWidth;
	int m_ScreenHeight;
	int m_DesktopScreenWidth;
	int m_DesktopScreenHeight;
	float m_ScreenHiDPIScale;
	float m_ScreenUIScale;

public:
	/* Constants: Texture Loading Flags
		TEXLOAD_NORESAMPLE - Prevents the texture from any resampling
		TEXLOAD_NOMIPMAPS - Prevents the texture from generating mipmaps
	*/
	enum
	{
		TEXLOAD_NORESAMPLE = 1,
		TEXLOAD_NOMIPMAPS = 2,
		TEXLOAD_TILEMAP = 4,

		NUMTILES_DIMENSION = 16, // number of tiles in each dimension within a texture
	};

	/* Constants: Wrap Modes */
	enum
	{
		WRAP_REPEAT = 0,
		WRAP_CLAMP,
	};

	class CTextureHandle
	{
		friend class IGraphics;
		int m_Id;

	public:
		CTextureHandle() : m_Id(-1)
		{
		}

		bool IsValid() const { return Id() >= 0; }
		int Id() const { return m_Id; }
		void Invalidate() { m_Id = -1; }
	};

	int ScreenWidth() const { return m_ScreenWidth; }
	int ScreenHeight() const { return m_ScreenHeight; }
	virtual float ScreenUIScale() const = 0;
	float ScreenAspect() const { return (float) ScreenWidth() / (float) ScreenHeight(); }
	float ScreenHiDPIScale() const { return m_ScreenHiDPIScale; }
	int DesktopWidth() const { return m_DesktopScreenWidth; }
	int DesktopHeight() const { return m_DesktopScreenHeight; }
	float DesktopAspect() const { return m_DesktopScreenWidth / (float) m_DesktopScreenHeight; }

	virtual void Clear(float r, float g, float b) = 0;

	virtual void ClipEnable(int x, int y, int w, int h) = 0;
	virtual void ClipDisable() = 0;

	virtual void MapScreen(float TopLeftX, float TopLeftY, float BottomRightX, float BottomRightY) = 0;
	virtual void GetScreen(float *pTopLeftX, float *pTopLeftY, float *pBottomRightX, float *pBottomRightY) = 0;

	// TODO: These should perhaps not be virtuals
	virtual void BlendNone() = 0;
	virtual void BlendNormal() = 0;
	virtual void WrapNormal() = 0;
	virtual void WrapClamp() = 0;
	virtual void WrapMode(int WrapU, int WrapV) = 0;
	virtual int MemoryUsage() const = 0;

	// largest texture the active device can create, used to size the glyph atlas
	virtual int MaxTextureSize() const { return 2048; }

	// render commands emitted since the previous call, and resets the counter
	virtual int TakeRenderCommandCount() { return 0; }

	// frames presented since the previous call, and resets the counter
	virtual int TakeRenderedFrameCount() { return 0; }

	// render thread time since the previous call, in time_get() units
	virtual int64 TakeRenderThreadTime() { return 0; }

	virtual void StainedOnly(bool Flag) = 0;

	// signed distance field text: the shader rebuilds the glyph coverage (and
	// optionally an outline) from a distance field texture
	struct CTextSDFParams
	{
		bool m_Enable;
		float m_Gain; // maps the normalized distance value onto a screen-space alpha ramp
		float m_OutlineOffset; // distance-value offset of the outline edge, 0 disables it
		vec4 m_OutlineColor;

		CTextSDFParams() :
			m_Enable(false), m_Gain(1.0f), m_OutlineOffset(0.0f),
			m_OutlineColor(0.0f, 0.0f, 0.0f, 0.0f) {}
	};
	virtual void SetTextSDF(const CTextSDFParams &Params) = 0;

	virtual int LoadPNGRaw(CImageInfo *pImg, const unsigned char *pData, int Size, const char *pContext = "raw data") = 0;
	virtual int LoadPNG(CImageInfo *pImg, const char *pFilename, int StorageType) = 0;

	virtual int UnloadTexture(CTextureHandle *pIndex) = 0;
	virtual CTextureHandle LoadTextureRaw(int Width, int Height, int Layers, int Format, const void *pData, int StoreFormat, int Flags) = 0;
	virtual int LoadTextureRawSub(CTextureHandle TextureID, int x, int y, int z, int Width, int Height, int Format, const void *pData) = 0;
	virtual CTextureHandle LoadTexture(const char *pFilename, int StorageType, int StoreFormat, int Flags) = 0;
	virtual void TextureSet(CTextureHandle Texture) = 0;
	void TextureClear() { TextureSet(CTextureHandle()); }

	struct CLineItem
	{
		float m_X0, m_Y0, m_X1, m_Y1;
		CLineItem() {}
		CLineItem(float x0, float y0, float x1, float y1) : m_X0(x0), m_Y0(y0), m_X1(x1), m_Y1(y1) {}
	};
	virtual void LinesBegin() = 0;
	virtual void LinesEnd() = 0;
	virtual void LinesDraw(const CLineItem *pArray, int Num) = 0;

	virtual void QuadsBegin() = 0;
	virtual void QuadsEnd() = 0;
	virtual void QuadsSetRotation(float Angle) = 0;
	virtual void QuadsSetSubset(float TopLeftY, float TopLeftV, float BottomRightU, float BottomRightV, int TextureIndex = -1) = 0;
	virtual void QuadsSetSubsetFree(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, int TextureIndex = -1) = 0;

	struct CQuadItem
	{
		float m_X, m_Y, m_Width, m_Height;
		CQuadItem() {}
		CQuadItem(float x, float y, float w, float h) : m_X(x), m_Y(y), m_Width(w), m_Height(h) {}
	};
	virtual void QuadsDraw(CQuadItem *pArray, int Num) = 0;
	virtual void SingleQuadDrawTL(const CQuadItem *pQuad) = 0;
	virtual void QuadsDrawTL(const CQuadItem *pArray, int Num) = 0;

	// draws top-left anchored quads with a per-quad texture subset, without the
	// per-quad state change of QuadsSetSubset + QuadsDrawTL
	virtual void QuadsDrawTLWithUV(const CQuadItem *pArray, const vec4 *pUV, int Num, int TextureIndex = -1) = 0;

	struct CFreeformItem
	{
		float m_X0, m_Y0, m_X1, m_Y1, m_X2, m_Y2, m_X3, m_Y3;
		CFreeformItem() {}
		CFreeformItem(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) : m_X0(x0), m_Y0(y0), m_X1(x1), m_Y1(y1), m_X2(x2), m_Y2(y2), m_X3(x3), m_Y3(y3) {}
	};
	virtual void QuadsDrawFreeform(const CFreeformItem *pArray, int Num) = 0;
	virtual void QuadsText(float x, float y, float Size, const char *pText) = 0;

	struct CColorVertex
	{
		int m_Index;
		float m_R, m_G, m_B, m_A;
		CColorVertex() {}
		CColorVertex(int i, float r, float g, float b, float a) : m_Index(i), m_R(r), m_G(g), m_B(b), m_A(a) {}
	};
	virtual void SetColorVertex(const CColorVertex *pArray, int Num) = 0;

	// Tile map rendering: the tile data is uploaded as one 2D array layer per tile
	// map and sampled by a dedicated shader, so the CPU never builds tile geometry.
	enum
	{
		TILEMAP_PASS_OPAQUE = 0, // only tiles marked TILEFLAG_OPAQUE
		TILEMAP_PASS_TRANSPARENT = 1, // only tiles that are not opaque
		TILEMAP_PASS_ALL = 2, // every tile, always blended
		TILEMAP_PASS_DATA_DEBUG = 3, // draw the raw tile data texture (red = index, green = flags)
	};

	// Whether the backend implements RenderTilemapTexture.
	virtual bool TilemapShaderEnabled() const { return false; }
	// TileData holds one array layer per tile map and Color is premultiplied.
	// ColorOpaque lets the shader skip the tile pass split just like the CPU path.
	virtual void RenderTilemapTexture(CTextureHandle TileData, int Layer, int Width, int Height, int PassMode, bool ColorOpaque, const vec4 &Color) = 0;

	virtual void SetColor(float r, float g, float b, float a) = 0;
	inline void SetColor(const vec4 &Color) { SetColor(Color.r, Color.g, Color.b, Color.a); }
	virtual void SetColor4(const vec4 &TopLeft, const vec4 &TopRight, const vec4 &BottomLeft, const vec4 &BottomRight) = 0;

	// Multiplies every color set through the SetColor* functions by a global alpha,
	// fading whole groups of UI elements in and out at once. The color channels are
	// scaled as well so that premultiplied-alpha textures do not brighten while fading.
	virtual void SetGlobalAlpha(float Alpha) = 0;
	virtual float GetGlobalAlpha() const = 0;

	virtual void ReadBackbuffer(unsigned char **ppPixels, int x, int y, int w, int h) = 0;
	virtual void TakeScreenshot(const char *pFilename, FScreenshotCallback pfnCallback, void *pUser) = 0;
	virtual int GetVideoModes(CVideoMode *pModes, int MaxModes, int Screen) = 0;

	virtual void Swap() = 0;
	virtual int GetNumScreens() const = 0;

	virtual bool ResizeWindow(int Width, int Height) = 0;
	// syncronization
	virtual void InsertSignal(class semaphore *pSemaphore) = 0;
	virtual bool IsIdle() const = 0;
	virtual void WaitForIdle() = 0;

protected:
	inline CTextureHandle CreateTextureHandle(int Index)
	{
		CTextureHandle Tex;
		Tex.m_Id = Index;
		return Tex;
	}
};

class IEngineGraphics : public IGraphics
{
	MACRO_INTERFACE("enginegraphics", 0)
public:
	virtual int Init() = 0;
	virtual void Shutdown() = 0;

	virtual bool Fullscreen(bool State) = 0;
	virtual void SetWindowBordered(bool State) = 0;
	virtual bool SetWindowScreen(int Index) = 0;
	virtual bool SetVSync(bool State) = 0;
	virtual int GetWindowScreen() = 0;

	virtual void Minimize() = 0;
	virtual void Maximize() = 0;

	virtual bool WindowActive() = 0;
	virtual bool WindowOpen() = 0;

	virtual void OnWindowResized(int Width, int Height) = 0;
	virtual void OnWindowPixelResized(int ScreenWidth, int ScreenHeight) = 0;
	virtual void *GetWindowHandle() = 0;
};

extern IEngineGraphics *CreateEngineGraphicsThreaded();

#endif
