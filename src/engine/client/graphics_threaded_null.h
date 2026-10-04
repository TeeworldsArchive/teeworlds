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

	virtual void ClipEnable([[maybe_unused]] int x, [[maybe_unused]] int y, [[maybe_unused]] int w, [[maybe_unused]] int h) override {};
	virtual void ClipDisable() override {};

	virtual void BlendNone() override {};
	virtual void BlendNormal() override {};

	virtual void WrapNormal() override {};
	virtual void WrapClamp() override {};
	virtual void WrapMode([[maybe_unused]] int WrapU, [[maybe_unused]] int WrapV) override {};

	virtual int MemoryUsage() const override { return 0; };

	virtual void StainedOnly([[maybe_unused]] bool Flag) override {};
	virtual void SetTextSDF([[maybe_unused]] const CTextSDFParams &Params) override {};

	virtual float ScreenUIScale() const override { return 1.0f; };

	virtual void MapScreen([[maybe_unused]] float TopLeftX, [[maybe_unused]] float TopLeftY, [[maybe_unused]] float BottomRightX, [[maybe_unused]] float BottomRightY) override {};
	virtual void GetScreen(float *pTopLeftX, float *pTopLeftY, float *pBottomRightX, float *pBottomRightY) override
	{
		*pTopLeftX = 0;
		*pTopLeftY = 0;
		*pBottomRightX = 600;
		*pBottomRightY = 600;
	};

	virtual void LinesBegin() override {};
	virtual void LinesEnd() override {};
	virtual void LinesDraw([[maybe_unused]] const CLineItem *pArray, [[maybe_unused]] int Num) override {};

	virtual int UnloadTexture([[maybe_unused]] IGraphics::CTextureHandle *Index) override { return 0; };
	virtual IGraphics::CTextureHandle LoadTextureRaw([[maybe_unused]] int Width, [[maybe_unused]] int Height, [[maybe_unused]] int Layers, [[maybe_unused]] int Format, [[maybe_unused]] const void *pData, [[maybe_unused]] int StoreFormat, [[maybe_unused]] int Flags) override { return CreateTextureHandle(0); };
	virtual int LoadTextureRawSub([[maybe_unused]] IGraphics::CTextureHandle TextureID, [[maybe_unused]] int x, [[maybe_unused]] int y, [[maybe_unused]] int z, [[maybe_unused]] int Width, [[maybe_unused]] int Height, [[maybe_unused]] int Format, [[maybe_unused]] const void *pData) override { return 0; };

	// simple uncompressed RGBA loaders
	virtual IGraphics::CTextureHandle LoadTexture([[maybe_unused]] const char *pFilename, [[maybe_unused]] int StorageType, [[maybe_unused]] int StoreFormat, [[maybe_unused]] int Flags) override { return CreateTextureHandle(0); };
	virtual int LoadPNG([[maybe_unused]] CImageInfo *pImg, [[maybe_unused]] const char *pFilename, [[maybe_unused]] int StorageType) override { return 0; };
	virtual int LoadPNGRaw([[maybe_unused]] CImageInfo *pImg, [[maybe_unused]] const unsigned char *pData, [[maybe_unused]] int Size, [[maybe_unused]] const char *pContext = "raw data") override { return 0; };

	virtual void TextureSet([[maybe_unused]] CTextureHandle TextureID) override {};

	virtual void Clear([[maybe_unused]] float r, [[maybe_unused]] float g, [[maybe_unused]] float b) override {};

	virtual void QuadsBegin() override {};
	virtual void QuadsEnd() override {};
	virtual void QuadsSetRotation([[maybe_unused]] float Angle) override {};

	virtual void SetColorVertex([[maybe_unused]] const CColorVertex *pArray, [[maybe_unused]] int Num) override {};
	virtual void SetColor([[maybe_unused]] float r, [[maybe_unused]] float g, [[maybe_unused]] float b, [[maybe_unused]] float a) override {};
	virtual void SetColor4([[maybe_unused]] const vec4 &TopLeft, [[maybe_unused]] const vec4 &TopRight, [[maybe_unused]] const vec4 &BottomLeft, [[maybe_unused]] const vec4 &BottomRight) override {};
	virtual void SetGlobalAlpha([[maybe_unused]] float Alpha) override {};
	virtual float GetGlobalAlpha() const override { return 1.0f; };

	virtual void QuadsSetSubset([[maybe_unused]] float TlU, [[maybe_unused]] float TlV, [[maybe_unused]] float BrU, [[maybe_unused]] float BrV, [[maybe_unused]] int TextureIndex = -1) override {};
	virtual void QuadsSetSubsetFree(
		[[maybe_unused]] float x0, [[maybe_unused]] float y0, [[maybe_unused]] float x1, [[maybe_unused]] float y1,
		[[maybe_unused]] float x2, [[maybe_unused]] float y2, [[maybe_unused]] float x3, [[maybe_unused]] float y3, [[maybe_unused]] int TextureIndex = -1) override {};

	virtual void QuadsDraw([[maybe_unused]] CQuadItem *pArray, [[maybe_unused]] int Num) override {};
	virtual void SingleQuadDrawTL([[maybe_unused]] const CQuadItem *pQuad) override {};
	virtual void QuadsDrawTL([[maybe_unused]] const CQuadItem *pArray, [[maybe_unused]] int Num) override {};
	virtual void QuadsDrawTLWithUV([[maybe_unused]] const CQuadItem *pArray, [[maybe_unused]] const vec4 *pUV, [[maybe_unused]] int Num, [[maybe_unused]] int TextureIndex = -1) override {};
	virtual void QuadsDrawFreeform([[maybe_unused]] const CFreeformItem *pArray, [[maybe_unused]] int Num) override {};
	virtual void QuadsText([[maybe_unused]] float x, [[maybe_unused]] float y, [[maybe_unused]] float Size, [[maybe_unused]] const char *pText) override {};

	// the null backend does not implement the GPU tilemap path
	virtual void RenderTilemapTexture([[maybe_unused]] CTextureHandle TileData, [[maybe_unused]] int Layer, [[maybe_unused]] int Width, [[maybe_unused]] int Height, [[maybe_unused]] int PassMode, [[maybe_unused]] bool ColorOpaque, [[maybe_unused]] const vec4 &Color) override {};

	virtual int GetNumScreens() const override { return 0; };
	virtual void Minimize() override {};
	virtual void Maximize() override {};
	virtual bool Fullscreen([[maybe_unused]] bool State) override { return false; };
	virtual void SetWindowBordered([[maybe_unused]] bool State) override {};
	virtual bool SetWindowScreen([[maybe_unused]] int Index) override { return false; };
	virtual int GetWindowScreen() override { return 0; };

	virtual bool WindowActive() override { return false; };
	virtual bool WindowOpen() override { return false; };

	virtual int Init() override { return 0; };
	virtual void Shutdown() override {};

	virtual void ReadBackbuffer([[maybe_unused]] unsigned char **ppPixels, [[maybe_unused]] int x, [[maybe_unused]] int y, [[maybe_unused]] int w, [[maybe_unused]] int h) override {};
	virtual void TakeScreenshot([[maybe_unused]] const char *pFilename, [[maybe_unused]] FScreenshotCallback pfnCallback, [[maybe_unused]] void *pUser) override {};
	virtual void Swap() override {};
	virtual bool SetVSync([[maybe_unused]] bool State) override { return false; };

	virtual int GetVideoModes([[maybe_unused]] CVideoMode *pModes, [[maybe_unused]] int MaxModes, [[maybe_unused]] int Screen) override { return 0; };

	// syncronization
	virtual void InsertSignal([[maybe_unused]] semaphore *pSemaphore) override {};
	virtual bool IsIdle() const override { return false; };
	virtual void WaitForIdle() override {};

	virtual bool ResizeWindow([[maybe_unused]] int Width, [[maybe_unused]] int Height) override { return false; };
	virtual void OnWindowResized([[maybe_unused]] int Width, [[maybe_unused]] int Height) override {};
	virtual void OnWindowPixelResized([[maybe_unused]] int ScreenWidth, [[maybe_unused]] int ScreenHeight) override {};
	virtual void *GetWindowHandle() override { return 0; };
};

#endif // ENGINE_CLIENT_GRAPHICS_THREADED_NULL_H
