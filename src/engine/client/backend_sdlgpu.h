/* (c) Teeworlds Archive Project Contributors. See license.txt. */
#ifndef ENGINE_CLIENT_BACKEND_SDLGPU_H
#define ENGINE_CLIENT_BACKEND_SDLGPU_H

#include "backend_sdl.h"
#include "graphics_threaded.h"

#include <SDL3/SDL.h>

// Fragment shader uniform block of the quad shader. Must match the UBO in the
// vulkan/gles/metal quad fragment shaders (48 bytes).
struct CQuadFragmentUniforms
{
	int m_UseTexture;
	int m_IsAlphaOnly;
	int m_IsStainedOnly;
	int m_IsSDF;
	float m_SDFGain;
	float m_SDFOutlineOffset;
	float m_SDFPadding0;
	float m_SDFPadding1;
	float m_SDFOutlineColor[4];
};

// takes care of SDL_GPU related rendering
class CCommandProcessorFragment_SDLGPU : public CCommandProcessorFragment_Texture
{
public:
	enum
	{
		CMD_INIT = CCommandBuffer::CMDGROUP_PLATFORM_SDLGPU,
		CMD_GPU_SHUTDOWN,
	};

	struct CInitCommand : public CCommandBuffer::CCommand
	{
		CInitCommand() :
			CCommand(CMD_INIT) {}
		SDL_GPUDevice *m_pDevice;
		SDL_Window *m_pWindow;
		volatile int *m_pTextureMemoryUsage;
		int m_FsaaSamples;
		SDL_GPUPresentMode m_PresentMode;
		SDL_GPUShaderFormat m_ShaderFormat;
		const unsigned char *m_pVertexShaderCode;
		unsigned m_VertexShaderSize;
		const unsigned char *m_pFragmentShaderCode;
		unsigned m_FragmentShaderSize;
		const unsigned char *m_pTilemapFragmentShaderCode;
		unsigned m_TilemapFragmentShaderSize;
	};

	struct CShutdownCommand : public CCommandBuffer::CCommand
	{
		CShutdownCommand() :
			CCommand(CMD_GPU_SHUTDOWN) {}
	};

	// pipeline variants: primitive type x blend mode
	enum
	{
		PIPELINE_QUADS_NONE = 0,
		PIPELINE_QUADS_ALPHA_PREMULTIPLIED,
		PIPELINE_QUADS_ALPHA,
		PIPELINE_LINES_NONE,
		PIPELINE_LINES_ALPHA_PREMULTIPLIED,
		PIPELINE_LINES_ALPHA,
		NUM_PIPELINES,

		PIPELINE_TILEMAP_NONE = 0,
		PIPELINE_TILEMAP_ALPHA_PREMULTIPLIED,
		NUM_TILEMAP_PIPELINES,
	};

	class CTexture
	{
	public:
		SDL_GPUTexture *m_pTexture;
		bool m_Valid;
		int m_Format;
		int m_MemSize;
		int m_BasicSamplerType;
	};

	class CPendingDraw
	{
	public:
		CCommandBuffer::CState m_State;
		unsigned m_PrimType;
		unsigned m_PrimCount;
		unsigned m_VertexOffset; // byte offset into the frame vertex buffer
		unsigned m_NumVertices;
		bool m_IsTilemap;
		int m_TileData;
		int m_TilemapLayer;
		int m_TilemapWidth;
		int m_TilemapHeight;
		int m_TilemapPassMode;
		int m_TilemapColorOpaque;
	};

	class CDeferredRelease
	{
	public:
		SDL_GPUTexture *m_pTexture;
		SDL_GPUBuffer *m_pBuffer;
		SDL_GPUTransferBuffer *m_pTransferBuffer;
		CDeferredRelease *m_pNext;
	};

private:
	SDL_GPUDevice *m_pDevice;
	SDL_Window *m_pWindow;

	SDL_GPUShader *m_pVertexShader;
	SDL_GPUShader *m_pFragmentShader;
	SDL_GPUShader *m_pTilemapFragmentShader;
	SDL_GPUGraphicsPipeline *m_apPipelines[NUM_PIPELINES];
	SDL_GPUGraphicsPipeline *m_apTilemapPipelines[NUM_TILEMAP_PIPELINES];
	SDL_GPUBuffer *m_pIndexBuffer;
	int m_IndexBufferNumIndices;
	SDL_GPUTextureFormat m_FrameFormat;
	SDL_GPUPresentMode m_PresentMode;

	CTexture m_aTextures[CCommandBuffer::MAX_TEXTURES];
	SDL_GPUSampler *m_aaSamplers[NUM_BASIC_SAMPLERS][NUM_WRAP_SAMPLERS];
	// tile data is read with texelFetch, so it uses exact (nearest) sampling
	SDL_GPUSampler *m_pTileDataSampler;

	// current frame state
	SDL_GPUCommandBuffer *m_pCommandBuffer;
	SDL_GPUFence *m_pFrameFence;
	SDL_GPUTexture *m_pFrameTexture;
	int m_FrameTextureWidth;
	int m_FrameTextureHeight;
	bool m_FrameCleared;
	SDL_FColor m_ClearColor;

	// render target of the current flush, NULL means the offscreen frame texture
	SDL_GPUTexture *m_pRenderTarget;
	int m_RenderTargetWidth;
	int m_RenderTargetHeight;
	bool m_RenderToFrameTexture;

	// pacing for the non-vsync modes, avoids blocking on the presentation queue
	Uint64 m_PresentIntervalNS;
	Uint64 m_LastPresentNS;

	CPendingDraw *m_pPendingDraws;
	unsigned m_PendingDrawCount;
	unsigned m_PendingDrawCapacity;
	CDeferredRelease *m_pDeferredHead;

	// CPU staging for the frame's vertices, reused and grown on demand
	unsigned char *m_pStagingData;
	unsigned m_StagingCapacity;
	unsigned m_StagingUsed;

	// persistent vertex and upload buffer, recreated only when they need to grow
	SDL_GPUBuffer *m_pVertexBuffer;
	SDL_GPUTransferBuffer *m_pVertexTransferBuffer;
	unsigned m_VertexBufferCapacity;
	unsigned m_VertexBufferUploaded;

	// cached render state so redundant binds and uniform pushes are skipped
	SDL_GPUGraphicsPipeline *m_pLastPipeline;
	SDL_GPUTexture *m_pLastTexture;
	SDL_GPUSampler *m_pLastSampler;
	SDL_GPUTexture *m_pLastTileDataTexture;
	bool m_BuffersBound;
	bool m_LastScissorValid;
	SDL_Rect m_LastScissor;
	bool m_LastOrthoMatrixValid;
	float m_LastOrthoMatrix[16];
	bool m_LastFragmentFlagsValid;
	CQuadFragmentUniforms m_LastFragmentUniforms;

	bool EnsureCommandBuffer();
	bool EnsureFrameTexture();
	bool EnsureStagingCapacity(unsigned Required);
	bool EnsurePendingDrawCapacity(unsigned Required);
	bool EnsureFrameBuffers(unsigned Required);
	void ResetRenderStateCache();
	void FlushDraws();
	void ApplyDraw(SDL_GPURenderPass *pPass, const CPendingDraw *pDraw);
	void ClearPendingDraws();
	void ResetFrameStaging();
	void AddDeferredTexture(SDL_GPUTexture *pTexture);
	void AddDeferredBuffer(SDL_GPUBuffer *pBuffer);
	void AddDeferredTransferBuffer(SDL_GPUTransferBuffer *pTransferBuffer);
	void ReleaseDeferred();
	bool UploadTexture(SDL_GPUTexture *pTexture, int X, int Y, int Z, int Width, int Height, int Layers, const void *pData, SDL_GPUTextureFormat Format, int BytesPerPixel);
	bool CreatePipeline(int Index, SDL_GPUPrimitiveType PrimType, int BlendVariant);
	bool CreateTilemapPipeline(int Index, int BlendVariant);

	static SDL_GPUTextureFormat TexFormatToSDLGPUFormat(int TexFormat);

	void Cmd_Init(const CInitCommand *pCommand);
	void Cmd_Shutdown(const CShutdownCommand *pCommand);
	void Cmd_Texture_Update(const CCommandBuffer::CTextureUpdateCommand *pCommand);
	void Cmd_Texture_Destroy(const CCommandBuffer::CTextureDestroyCommand *pCommand);
	void Cmd_Texture_Create(const CCommandBuffer::CTextureCreateCommand *pCommand);
	void Cmd_Clear(const CCommandBuffer::CClearCommand *pCommand);
	void Cmd_Render(const CCommandBuffer::CRenderCommand *pCommand);
	void Cmd_RenderTilemapTexture(const CCommandBuffer::CRenderTilemapTextureCommand *pCommand);
	void Cmd_Screenshot(const CCommandBuffer::CScreenshotCommand *pCommand);
	void Cmd_Swap(const CCommandBuffer::CSwapCommand *pCommand);
	void Cmd_VSync(const CCommandBuffer::CVSyncCommand *pCommand);
	void Cmd_WindowResized(const CCommandBuffer::CWindowResizedCommand *pCommand);

public:
	CCommandProcessorFragment_SDLGPU();

	bool RunCommand(const CCommandBuffer::CCommand *pBaseCommand);
};

// combines the general fragment with the SDL_GPU one
class CCommandProcessor_SDL_GPU : public CCommandProcessor_SDL
{
	CCommandProcessorFragment_SDLGPU m_SDLGPU;

protected:
	virtual bool RunBackendCommand(CCommandBuffer::CCommand *pCommand);
};

// graphics backend implemented with SDL_GPU
class CGraphicsBackend_SDL_GPU : public CGraphicsBackend_SDL
{
	SDL_GPUDevice *m_pDevice;
	unsigned char *m_pVertexShaderCode;
	unsigned m_VertexShaderSize;
	unsigned char *m_pFragmentShaderCode;
	unsigned m_FragmentShaderSize;
	unsigned char *m_pTilemapFragmentShaderCode;
	unsigned m_TilemapFragmentShaderSize;

public:
	CGraphicsBackend_SDL_GPU(class IStorage *pStorage);
	virtual int Init(const char *pName, int *pScreen, int *pWindowWidth, int *pWindowHeight, int *pScreenWidth, int *pScreenHeight, int FsaaSamples, int Flags, int *pDesktopWidth, int *pDesktopHeight);
	virtual int Shutdown();
};

#endif // ENGINE_CLIENT_BACKEND_SDLGPU_H
