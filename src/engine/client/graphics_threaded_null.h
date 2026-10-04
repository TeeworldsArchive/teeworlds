/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef ENGINE_CLIENT_GRAPHICS_THREADED_NULL_H
#define ENGINE_CLIENT_GRAPHICS_THREADED_NULL_H

#include <engine/graphics.h>

class CGraphics_ThreadedNull : public IEngineGraphics
{
public:
	CGraphics_ThreadedNull()
	{
		m_ScreenWidth = 800;
		m_ScreenHeight = 600;
		m_DesktopScreenWidth = 800;
		m_DesktopScreenHeight = 600;
	};

	virtual void ClipEnable([[maybe_unused]] int x, [[maybe_unused]] int y, [[maybe_unused]] int w, [[maybe_unused]] int h) {};
	virtual void ClipDisable() {};

	virtual void BlendNone() {};
	virtual void BlendNormal() {};

	virtual void WrapNormal() {};
	virtual void WrapClamp() {};
	virtual void WrapMode([[maybe_unused]] int WrapU, [[maybe_unused]] int WrapV) {};

	virtual int MemoryUsage() const { return 0; };

	virtual void StainedOnly([[maybe_unused]] bool Flag) {};
	virtual void SetTextSDF([[maybe_unused]] const CTextSDFParams &Params) {};

	virtual float ScreenUIScale() const { return 1.0f; };

	virtual void MapScreen([[maybe_unused]] float TopLeftX, [[maybe_unused]] float TopLeftY, [[maybe_unused]] float BottomRightX, [[maybe_unused]] float BottomRightY) {};
	virtual void GetScreen(float *pTopLeftX, float *pTopLeftY, float *pBottomRightX, float *pBottomRightY)
	{
		*pTopLeftX = 0;
		*pTopLeftY = 0;
		*pBottomRightX = 600;
		*pBottomRightY = 600;
	};

	virtual void LinesBegin() {};
	virtual void LinesEnd() {};
	virtual void LinesDraw([[maybe_unused]] const CLineItem *pArray, [[maybe_unused]] int Num) {};

	virtual int UnloadTexture([[maybe_unused]] IGraphics::CTextureHandle *Index) { return 0; };
	virtual IGraphics::CTextureHandle LoadTextureRaw([[maybe_unused]] int Width, [[maybe_unused]] int Height, [[maybe_unused]] int Layers, [[maybe_unused]] int Format, [[maybe_unused]] const void *pData, [[maybe_unused]] int StoreFormat, [[maybe_unused]] int Flags) { return CreateTextureHandle(0); };
	virtual int LoadTextureRawSub([[maybe_unused]] IGraphics::CTextureHandle TextureID, [[maybe_unused]] int x, [[maybe_unused]] int y, [[maybe_unused]] int z, [[maybe_unused]] int Width, [[maybe_unused]] int Height, [[maybe_unused]] int Format, [[maybe_unused]] const void *pData) { return 0; };

	// simple uncompressed RGBA loaders
	virtual IGraphics::CTextureHandle LoadTexture([[maybe_unused]] const char *pFilename, [[maybe_unused]] int StorageType, [[maybe_unused]] int StoreFormat, [[maybe_unused]] int Flags) { return CreateTextureHandle(0); };
	virtual int LoadPNG([[maybe_unused]] CImageInfo *pImg, [[maybe_unused]] const char *pFilename, [[maybe_unused]] int StorageType) { return 0; };
	virtual int LoadPNGRaw([[maybe_unused]] CImageInfo *pImg, [[maybe_unused]] const unsigned char *pData, [[maybe_unused]] int Size, [[maybe_unused]] const char *pContext = "raw data") { return 0; };

	virtual void TextureSet([[maybe_unused]] CTextureHandle TextureID) {};

	virtual void Clear([[maybe_unused]] float r, [[maybe_unused]] float g, [[maybe_unused]] float b) {};

	virtual void QuadsBegin() {};
	virtual void QuadsEnd() {};
	virtual void QuadsSetRotation([[maybe_unused]] float Angle) {};

	virtual void SetColorVertex([[maybe_unused]] const CColorVertex *pArray, [[maybe_unused]] int Num) {};
	virtual void SetColor([[maybe_unused]] float r, [[maybe_unused]] float g, [[maybe_unused]] float b, [[maybe_unused]] float a) {};
	virtual void SetColor4([[maybe_unused]] const vec4 &TopLeft, [[maybe_unused]] const vec4 &TopRight, [[maybe_unused]] const vec4 &BottomLeft, [[maybe_unused]] const vec4 &BottomRight) {};
	virtual void SetGlobalAlpha([[maybe_unused]] float Alpha) {};
	virtual float GetGlobalAlpha() const { return 1.0f; };

	virtual void QuadsSetSubset([[maybe_unused]] float TlU, [[maybe_unused]] float TlV, [[maybe_unused]] float BrU, [[maybe_unused]] float BrV, [[maybe_unused]] int TextureIndex = -1) {};
	virtual void QuadsSetSubsetFree(
		[[maybe_unused]] float x0, [[maybe_unused]] float y0, [[maybe_unused]] float x1, [[maybe_unused]] float y1,
		[[maybe_unused]] float x2, [[maybe_unused]] float y2, [[maybe_unused]] float x3, [[maybe_unused]] float y3, [[maybe_unused]] int TextureIndex = -1) {};

	virtual void QuadsDraw([[maybe_unused]] CQuadItem *pArray, [[maybe_unused]] int Num) {};
	virtual void SingleQuadDrawTL([[maybe_unused]] const CQuadItem *pQuad) {};
	virtual void QuadsDrawTL([[maybe_unused]] const CQuadItem *pArray, [[maybe_unused]] int Num) {};
	virtual void QuadsDrawTLWithUV([[maybe_unused]] const CQuadItem *pArray, [[maybe_unused]] const vec4 *pUV, [[maybe_unused]] int Num, [[maybe_unused]] int TextureIndex = -1) {};
	virtual void QuadsDrawFreeform([[maybe_unused]] const CFreeformItem *pArray, [[maybe_unused]] int Num) {};
	virtual void QuadsText([[maybe_unused]] float x, [[maybe_unused]] float y, [[maybe_unused]] float Size, [[maybe_unused]] const char *pText) {};

	// the null backend does not implement the GPU tilemap path
	virtual void RenderTilemapTexture([[maybe_unused]] CTextureHandle TileData, [[maybe_unused]] int Layer, [[maybe_unused]] int Width, [[maybe_unused]] int Height, [[maybe_unused]] int PassMode, [[maybe_unused]] bool ColorOpaque, [[maybe_unused]] const vec4 &Color) {};

	virtual int GetNumScreens() const { return 0; };
	virtual void Minimize() {};
	virtual void Maximize() {};
	virtual bool Fullscreen([[maybe_unused]] bool State) { return false; };
	virtual void SetWindowBordered([[maybe_unused]] bool State) {};
	virtual bool SetWindowScreen([[maybe_unused]] int Index) { return false; };
	virtual int GetWindowScreen() { return 0; };

	virtual bool WindowActive() { return false; };
	virtual bool WindowOpen() { return false; };

	virtual int Init() { return 0; };
	virtual void Shutdown() {};

	virtual void ReadBackbuffer([[maybe_unused]] unsigned char **ppPixels, [[maybe_unused]] int x, [[maybe_unused]] int y, [[maybe_unused]] int w, [[maybe_unused]] int h) {};
	virtual void TakeScreenshot([[maybe_unused]] const char *pFilename, [[maybe_unused]] FScreenshotCallback pfnCallback, [[maybe_unused]] void *pUser) {};
	virtual void Swap() {};
	virtual bool SetVSync([[maybe_unused]] bool State) { return false; };

	virtual int GetVideoModes([[maybe_unused]] CVideoMode *pModes, [[maybe_unused]] int MaxModes, [[maybe_unused]] int Screen) { return 0; };

	// syncronization
	virtual void InsertSignal([[maybe_unused]] semaphore *pSemaphore) {};
	virtual bool IsIdle() const { return false; };
	virtual void WaitForIdle() {};

	virtual bool ResizeWindow([[maybe_unused]] int Width, [[maybe_unused]] int Height) { return false; };
	virtual void OnWindowResized([[maybe_unused]] int Width, [[maybe_unused]] int Height) {};
	virtual void OnWindowPixelResized([[maybe_unused]] int ScreenWidth, [[maybe_unused]] int ScreenHeight) {};
	virtual void *GetWindowHandle() { return 0; };
};

#endif // ENGINE_CLIENT_GRAPHICS_THREADED_NULL_H
