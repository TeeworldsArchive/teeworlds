/* (c) Teeworlds Archive Project Contributors. See license.txt. */
#include <base/detect.h>
#include <base/tl/threading.h>

#include <engine/storage.h>

#include <SDL3/SDL.h>

#include "backend_sdl.h"
#include "backend_sdlgpu.h"
#include "graphics_threaded.h"

// ------------ present mode selection

static const char *PresentModeName(SDL_GPUPresentMode Mode)
{
	switch(Mode)
	{
	case SDL_GPU_PRESENTMODE_VSYNC: return "vsync";
	case SDL_GPU_PRESENTMODE_IMMEDIATE: return "immediate";
	case SDL_GPU_PRESENTMODE_MAILBOX: return "mailbox";
	default: return "unknown";
	}
}

// picks the best present mode the window supports: with vsync off IMMEDIATE is
// preferred, then MAILBOX, and VSYNC is only used as a fallback
static SDL_GPUPresentMode ChoosePresentMode(SDL_GPUDevice *pDevice, SDL_Window *pWindow, bool VSync)
{
	if(VSync)
		return SDL_GPU_PRESENTMODE_VSYNC;
	if(SDL_WindowSupportsGPUPresentMode(pDevice, pWindow, SDL_GPU_PRESENTMODE_IMMEDIATE))
		return SDL_GPU_PRESENTMODE_IMMEDIATE;
	if(SDL_WindowSupportsGPUPresentMode(pDevice, pWindow, SDL_GPU_PRESENTMODE_MAILBOX))
		return SDL_GPU_PRESENTMODE_MAILBOX;
	return SDL_GPU_PRESENTMODE_VSYNC;
}

// duration of one display refresh in nanoseconds, or 0 if unknown
static Uint64 GetDisplayRefreshIntervalNS(SDL_Window *pWindow)
{
	const SDL_DisplayMode *pMode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(pWindow));
	if(!pMode)
		return 0;
	if(pMode->refresh_rate_numerator > 0 && pMode->refresh_rate_denominator > 0)
		return 1000000000ull * (Uint64)pMode->refresh_rate_denominator / (Uint64)pMode->refresh_rate_numerator;
	if(pMode->refresh_rate > 0.0f)
		return (Uint64)(1000000000.0 / pMode->refresh_rate);
	return 0;
}

// ------------ CCommandProcessorFragment_SDLGPU

CCommandProcessorFragment_SDLGPU::CCommandProcessorFragment_SDLGPU()
{
	m_pDevice = 0;
	m_pWindow = 0;
	m_pVertexShader = 0;
	m_pFragmentShader = 0;
	m_pTilemapFragmentShader = 0;
	m_pTileDataSampler = 0;
	m_pIndexBuffer = 0;
	m_IndexBufferNumIndices = 0;
	m_FrameFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
	m_PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
	m_PresentIntervalNS = 0;
	m_LastPresentNS = 0;
	m_MaxTexSize = 16384;
	m_pCommandBuffer = 0;
	m_pFrameFence = 0;
	m_pFrameTexture = 0;
	m_FrameTextureWidth = 0;
	m_FrameTextureHeight = 0;
	m_FrameCleared = false;
	m_pRenderTarget = 0;
	m_RenderTargetWidth = 0;
	m_RenderTargetHeight = 0;
	m_RenderToFrameTexture = false;
	m_pDeferredHead = 0;
	m_pPendingDraws = 0;
	m_PendingDrawCount = 0;
	m_PendingDrawCapacity = 0;
	m_pStagingData = 0;
	m_StagingCapacity = 0;
	m_StagingUsed = 0;
	m_pVertexBuffer = 0;
	m_pVertexTransferBuffer = 0;
	m_VertexBufferCapacity = 0;
	m_VertexBufferUploaded = 0;
	m_pLastPipeline = 0;
	m_pLastTexture = 0;
	m_pLastSampler = 0;
	m_pLastTileDataTexture = 0;
	m_BuffersBound = false;
	m_LastScissorValid = false;
	mem_zero(&m_LastScissor, sizeof(m_LastScissor));
	m_LastOrthoMatrixValid = false;
	mem_zero(m_LastOrthoMatrix, sizeof(m_LastOrthoMatrix));
	m_LastFragmentFlagsValid = false;
	mem_zero(&m_LastFragmentUniforms, sizeof(m_LastFragmentUniforms));
	m_ClearColor.r = 0.0f;
	m_ClearColor.g = 0.0f;
	m_ClearColor.b = 0.0f;
	m_ClearColor.a = 1.0f;
	mem_zero(m_apPipelines, sizeof(m_apPipelines));
	mem_zero(m_apTilemapPipelines, sizeof(m_apTilemapPipelines));
	mem_zero(m_aTextures, sizeof(m_aTextures));
	mem_zero(m_aaSamplers, sizeof(m_aaSamplers));
}

SDL_GPUTextureFormat CCommandProcessorFragment_SDLGPU::TexFormatToSDLGPUFormat(int TexFormat)
{
	switch(TexFormat)
	{
	case CCommandBuffer::TEXFORMAT_ALPHA:
		return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
	case CCommandBuffer::TEXFORMAT_RG:
		return SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
	case CCommandBuffer::TEXFORMAT_RGB:
	case CCommandBuffer::TEXFORMAT_RGBA:
	default:
		return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	}
}

bool CCommandProcessorFragment_SDLGPU::EnsureCommandBuffer()
{
	if(m_pCommandBuffer)
		return true;

	// wait for the previous frame to finish before reusing resources
	if(m_pFrameFence)
	{
		SDL_WaitForGPUFences(m_pDevice, true, &m_pFrameFence, 1);
		SDL_ReleaseGPUFence(m_pDevice, m_pFrameFence);
		m_pFrameFence = 0;
		ReleaseDeferred();
	}

	m_pCommandBuffer = SDL_AcquireGPUCommandBuffer(m_pDevice);
	if(!m_pCommandBuffer)
	{
		dbg_msg("gfx", "failed to acquire command buffer: %s", SDL_GetError());
		return false;
	}
	return true;
}

bool CCommandProcessorFragment_SDLGPU::EnsureFrameTexture()
{
	int Width = 0;
	int Height = 0;
	SDL_GetWindowSizeInPixels(m_pWindow, &Width, &Height);
	if(Width <= 0 || Height <= 0)
		return false;

	if(m_pFrameTexture && Width == m_FrameTextureWidth && Height == m_FrameTextureHeight)
		return true;

	if(m_pFrameTexture)
	{
		AddDeferredTexture(m_pFrameTexture);
		m_pFrameTexture = 0;
	}

	SDL_GPUTextureCreateInfo Info;
	mem_zero(&Info, sizeof(Info));
	Info.type = SDL_GPU_TEXTURETYPE_2D;
	Info.format = m_FrameFormat;
	Info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	Info.width = Width;
	Info.height = Height;
	Info.layer_count_or_depth = 1;
	Info.num_levels = 1;
	Info.sample_count = SDL_GPU_SAMPLECOUNT_1;

	m_pFrameTexture = SDL_CreateGPUTexture(m_pDevice, &Info);
	if(!m_pFrameTexture)
	{
		dbg_msg("gfx", "failed to create frame texture: %s", SDL_GetError());
		return false;
	}
	m_FrameTextureWidth = Width;
	m_FrameTextureHeight = Height;
	m_FrameCleared = false;
	return true;
}

bool CCommandProcessorFragment_SDLGPU::EnsureStagingCapacity(unsigned Required)
{
	if(m_StagingCapacity >= Required)
		return true;

	unsigned NewCapacity = maximum(Required, 64u * 1024u);
	if(m_StagingCapacity > 0)
		NewCapacity = maximum(NewCapacity, m_StagingCapacity + m_StagingCapacity / 2);

	unsigned char *pNew = (unsigned char *)mem_alloc(NewCapacity);
	if(!pNew)
		return false;
	if(m_pStagingData)
	{
		mem_copy(pNew, m_pStagingData, m_StagingUsed);
		mem_free(m_pStagingData);
	}
	m_pStagingData = pNew;
	m_StagingCapacity = NewCapacity;
	return true;
}

bool CCommandProcessorFragment_SDLGPU::EnsurePendingDrawCapacity(unsigned Required)
{
	if(m_PendingDrawCapacity >= Required)
		return true;

	unsigned NewCapacity = maximum(Required, 256u);
	if(m_PendingDrawCapacity > 0)
		NewCapacity = maximum(NewCapacity, m_PendingDrawCapacity * 2);

	CPendingDraw *pNew = (CPendingDraw *)mem_alloc(sizeof(CPendingDraw) * NewCapacity);
	if(!pNew)
		return false;
	if(m_pPendingDraws)
	{
		mem_copy(pNew, m_pPendingDraws, sizeof(CPendingDraw) * m_PendingDrawCount);
		mem_free(m_pPendingDraws);
	}
	m_pPendingDraws = pNew;
	m_PendingDrawCapacity = NewCapacity;
	return true;
}

bool CCommandProcessorFragment_SDLGPU::EnsureFrameBuffers(unsigned Required)
{
	if(m_pVertexBuffer && m_pVertexTransferBuffer && m_VertexBufferCapacity >= Required)
		return true;

	unsigned NewCapacity = maximum(Required, 64u * 1024u);
	if(m_VertexBufferCapacity > 0)
		NewCapacity = maximum(NewCapacity, m_VertexBufferCapacity + m_VertexBufferCapacity / 2);
	// keep the sizes page aligned so growth does not happen for tiny increases
	NewCapacity = (NewCapacity + 0xFFFu) & ~0xFFFu;

	SDL_GPUTransferBufferCreateInfo TransferInfo;
	mem_zero(&TransferInfo, sizeof(TransferInfo));
	TransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	TransferInfo.size = NewCapacity;
	SDL_GPUTransferBuffer *pTransferBuffer = SDL_CreateGPUTransferBuffer(m_pDevice, &TransferInfo);

	SDL_GPUBufferCreateInfo BufferInfo;
	mem_zero(&BufferInfo, sizeof(BufferInfo));
	BufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
	BufferInfo.size = NewCapacity;
	SDL_GPUBuffer *pVertexBuffer = SDL_CreateGPUBuffer(m_pDevice, &BufferInfo);

	if(!pTransferBuffer || !pVertexBuffer)
	{
		if(pTransferBuffer)
			SDL_ReleaseGPUTransferBuffer(m_pDevice, pTransferBuffer);
		if(pVertexBuffer)
			SDL_ReleaseGPUBuffer(m_pDevice, pVertexBuffer);
		return false;
	}

	// an in-flight frame may still use the old buffers, so release them later
	if(m_pVertexTransferBuffer)
		AddDeferredTransferBuffer(m_pVertexTransferBuffer);
	if(m_pVertexBuffer)
		AddDeferredBuffer(m_pVertexBuffer);

	m_pVertexTransferBuffer = pTransferBuffer;
	m_pVertexBuffer = pVertexBuffer;
	m_VertexBufferCapacity = NewCapacity;
	// the new buffer is empty, so everything has to be uploaded again
	m_VertexBufferUploaded = 0;
	return true;
}

void CCommandProcessorFragment_SDLGPU::ResetRenderStateCache()
{
	m_pLastPipeline = 0;
	m_pLastTexture = 0;
	m_pLastSampler = 0;
	m_pLastTileDataTexture = 0;
	m_BuffersBound = false;
	m_LastScissorValid = false;
	m_LastOrthoMatrixValid = false;
	m_LastFragmentFlagsValid = false;
}

bool CCommandProcessorFragment_SDLGPU::UploadTexture(SDL_GPUTexture *pTexture, int X, int Y, int Z, int Width, int Height, int Layers, const void *pData, [[maybe_unused]] SDL_GPUTextureFormat Format, int BytesPerPixel)
{
	if(!EnsureCommandBuffer())
		return false;

	const unsigned Size = (unsigned)(Width * Height * Layers * BytesPerPixel);
	SDL_GPUTransferBufferCreateInfo TransferInfo;
	mem_zero(&TransferInfo, sizeof(TransferInfo));
	TransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	TransferInfo.size = Size;
	SDL_GPUTransferBuffer *pTransferBuffer = SDL_CreateGPUTransferBuffer(m_pDevice, &TransferInfo);
	if(!pTransferBuffer)
		return false;

	void *pMapped = SDL_MapGPUTransferBuffer(m_pDevice, pTransferBuffer, false);
	if(!pMapped)
	{
		SDL_ReleaseGPUTransferBuffer(m_pDevice, pTransferBuffer);
		return false;
	}
	mem_copy(pMapped, pData, Size);
	SDL_UnmapGPUTransferBuffer(m_pDevice, pTransferBuffer);

	SDL_GPUCopyPass *pCopyPass = SDL_BeginGPUCopyPass(m_pCommandBuffer);

	SDL_GPUTextureTransferInfo Src;
	mem_zero(&Src, sizeof(Src));
	Src.transfer_buffer = pTransferBuffer;
	// tightly packed data, so spell out the row/layer strides or later layers desync
	Src.pixels_per_row = (Uint32)Width;
	Src.rows_per_layer = (Uint32)Height;

	SDL_GPUTextureRegion Dst;
	mem_zero(&Dst, sizeof(Dst));
	Dst.texture = pTexture;
	Dst.mip_level = 0;
	Dst.x = X;
	Dst.y = Y;
	Dst.z = 0;
	Dst.w = Width;
	Dst.h = Height;
	Dst.d = 1;

	// upload layer by layer, multi-layer regions are not handled reliably
	const unsigned LayerSize = (unsigned)(Width * Height * BytesPerPixel);
	for(int l = 0; l < Layers; l++)
	{
		Src.offset = (Uint32)(l * LayerSize);
		Dst.layer = Z + l;
		SDL_UploadToGPUTexture(pCopyPass, &Src, &Dst, false);
	}
	SDL_EndGPUCopyPass(pCopyPass);

	AddDeferredTransferBuffer(pTransferBuffer);
	return true;
}

bool CCommandProcessorFragment_SDLGPU::CreateTilemapPipeline(int Index, int BlendVariant)
{
	SDL_GPUVertexBufferDescription VertexBufferDesc;
	mem_zero(&VertexBufferDesc, sizeof(VertexBufferDesc));
	VertexBufferDesc.slot = 0;
	VertexBufferDesc.pitch = sizeof(CCommandBuffer::CVertex);
	VertexBufferDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
	VertexBufferDesc.instance_step_rate = 0;

	SDL_GPUVertexAttribute aAttributes[3];
	mem_zero(aAttributes, sizeof(aAttributes));
	aAttributes[0].location = 0;
	aAttributes[0].buffer_slot = 0;
	aAttributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
	aAttributes[0].offset = 0;
	aAttributes[1].location = 1;
	aAttributes[1].buffer_slot = 0;
	aAttributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
	aAttributes[1].offset = sizeof(float) * 2;
	aAttributes[2].location = 2;
	aAttributes[2].buffer_slot = 0;
	aAttributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
	aAttributes[2].offset = sizeof(float) * 5;

	SDL_GPUVertexInputState VertexInputState;
	mem_zero(&VertexInputState, sizeof(VertexInputState));
	VertexInputState.vertex_buffer_descriptions = &VertexBufferDesc;
	VertexInputState.num_vertex_buffers = 1;
	VertexInputState.vertex_attributes = aAttributes;
	VertexInputState.num_vertex_attributes = 3;

	SDL_GPURasterizerState RasterizerState;
	mem_zero(&RasterizerState, sizeof(RasterizerState));
	RasterizerState.fill_mode = SDL_GPU_FILLMODE_FILL;
	RasterizerState.cull_mode = SDL_GPU_CULLMODE_NONE;
	RasterizerState.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
	RasterizerState.enable_depth_clip = false;

	SDL_GPUMultisampleState MultisampleState;
	mem_zero(&MultisampleState, sizeof(MultisampleState));
	MultisampleState.sample_count = SDL_GPU_SAMPLECOUNT_1;

	SDL_GPUDepthStencilState DepthStencilState;
	mem_zero(&DepthStencilState, sizeof(DepthStencilState));
	DepthStencilState.enable_depth_test = false;
	DepthStencilState.enable_depth_write = false;

	SDL_GPUColorTargetBlendState BlendState;
	mem_zero(&BlendState, sizeof(BlendState));
	// the opaque pass has alpha 1 and premultiplied color, so it needs no blend
	BlendState.enable_blend = BlendVariant != PIPELINE_TILEMAP_NONE;
	BlendState.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	BlendState.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	BlendState.color_blend_op = SDL_GPU_BLENDOP_ADD;
	BlendState.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	BlendState.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	BlendState.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
	BlendState.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
	BlendState.enable_color_write_mask = false;

	SDL_GPUColorTargetDescription ColorTarget;
	mem_zero(&ColorTarget, sizeof(ColorTarget));
	ColorTarget.format = m_FrameFormat;
	ColorTarget.blend_state = BlendState;

	SDL_GPUGraphicsPipelineTargetInfo TargetInfo;
	mem_zero(&TargetInfo, sizeof(TargetInfo));
	TargetInfo.color_target_descriptions = &ColorTarget;
	TargetInfo.num_color_targets = 1;
	TargetInfo.has_depth_stencil_target = false;

	SDL_GPUGraphicsPipelineCreateInfo PipelineInfo;
	mem_zero(&PipelineInfo, sizeof(PipelineInfo));
	PipelineInfo.vertex_shader = m_pVertexShader;
	PipelineInfo.fragment_shader = m_pTilemapFragmentShader;
	PipelineInfo.vertex_input_state = VertexInputState;
	PipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	PipelineInfo.rasterizer_state = RasterizerState;
	PipelineInfo.multisample_state = MultisampleState;
	PipelineInfo.depth_stencil_state = DepthStencilState;
	PipelineInfo.target_info = TargetInfo;

	m_apTilemapPipelines[Index] = SDL_CreateGPUGraphicsPipeline(m_pDevice, &PipelineInfo);
	if(!m_apTilemapPipelines[Index])
		dbg_msg("gfx", "failed to create tilemap graphics pipeline: %s", SDL_GetError());
	return m_apTilemapPipelines[Index] != 0;
}

bool CCommandProcessorFragment_SDLGPU::CreatePipeline(int Index, SDL_GPUPrimitiveType PrimType, int BlendVariant)
{
	SDL_GPUVertexBufferDescription VertexBufferDesc;
	mem_zero(&VertexBufferDesc, sizeof(VertexBufferDesc));
	VertexBufferDesc.slot = 0;
	VertexBufferDesc.pitch = sizeof(CCommandBuffer::CVertex);
	VertexBufferDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
	VertexBufferDesc.instance_step_rate = 0;

	SDL_GPUVertexAttribute aAttributes[3];
	mem_zero(aAttributes, sizeof(aAttributes));
	aAttributes[0].location = 0;
	aAttributes[0].buffer_slot = 0;
	aAttributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
	aAttributes[0].offset = 0;
	aAttributes[1].location = 1;
	aAttributes[1].buffer_slot = 0;
	aAttributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
	aAttributes[1].offset = sizeof(float) * 2;
	aAttributes[2].location = 2;
	aAttributes[2].buffer_slot = 0;
	aAttributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
	aAttributes[2].offset = sizeof(float) * 5;

	SDL_GPUVertexInputState VertexInputState;
	mem_zero(&VertexInputState, sizeof(VertexInputState));
	VertexInputState.vertex_buffer_descriptions = &VertexBufferDesc;
	VertexInputState.num_vertex_buffers = 1;
	VertexInputState.vertex_attributes = aAttributes;
	VertexInputState.num_vertex_attributes = 3;

	SDL_GPURasterizerState RasterizerState;
	mem_zero(&RasterizerState, sizeof(RasterizerState));
	RasterizerState.fill_mode = SDL_GPU_FILLMODE_FILL;
	RasterizerState.cull_mode = SDL_GPU_CULLMODE_NONE;
	RasterizerState.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
	RasterizerState.enable_depth_clip = false;

	SDL_GPUMultisampleState MultisampleState;
	mem_zero(&MultisampleState, sizeof(MultisampleState));
	MultisampleState.sample_count = SDL_GPU_SAMPLECOUNT_1;

	SDL_GPUDepthStencilState DepthStencilState;
	mem_zero(&DepthStencilState, sizeof(DepthStencilState));
	DepthStencilState.enable_depth_test = false;
	DepthStencilState.enable_depth_write = false;

	SDL_GPUColorTargetBlendState BlendState;
	mem_zero(&BlendState, sizeof(BlendState));
	// like the OpenGL backend blending stays enabled, only the factors change
	BlendState.enable_blend = true;
	// premultiplied alpha uses ONE, plain RGB uses SRC_ALPHA
	BlendState.src_color_blendfactor = BlendVariant == 2 ? SDL_GPU_BLENDFACTOR_SRC_ALPHA : SDL_GPU_BLENDFACTOR_ONE;
	BlendState.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	BlendState.color_blend_op = SDL_GPU_BLENDOP_ADD;
	BlendState.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	BlendState.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	BlendState.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
	BlendState.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
	BlendState.enable_color_write_mask = false;

	SDL_GPUColorTargetDescription ColorTarget;
	mem_zero(&ColorTarget, sizeof(ColorTarget));
	ColorTarget.format = m_FrameFormat;
	ColorTarget.blend_state = BlendState;

	SDL_GPUGraphicsPipelineTargetInfo TargetInfo;
	mem_zero(&TargetInfo, sizeof(TargetInfo));
	TargetInfo.color_target_descriptions = &ColorTarget;
	TargetInfo.num_color_targets = 1;
	TargetInfo.has_depth_stencil_target = false;

	SDL_GPUGraphicsPipelineCreateInfo PipelineInfo;
	mem_zero(&PipelineInfo, sizeof(PipelineInfo));
	PipelineInfo.vertex_shader = m_pVertexShader;
	PipelineInfo.fragment_shader = m_pFragmentShader;
	PipelineInfo.vertex_input_state = VertexInputState;
	PipelineInfo.primitive_type = PrimType;
	PipelineInfo.rasterizer_state = RasterizerState;
	PipelineInfo.multisample_state = MultisampleState;
	PipelineInfo.depth_stencil_state = DepthStencilState;
	PipelineInfo.target_info = TargetInfo;

	m_apPipelines[Index] = SDL_CreateGPUGraphicsPipeline(m_pDevice, &PipelineInfo);
	if(!m_apPipelines[Index])
		dbg_msg("gfx", "failed to create graphics pipeline: %s", SDL_GetError());
	return m_apPipelines[Index] != 0;
}

void CCommandProcessorFragment_SDLGPU::Cmd_Init(const CInitCommand *pCommand)
{
	m_pDevice = pCommand->m_pDevice;
	m_pWindow = pCommand->m_pWindow;
	m_pTextureMemoryUsage = pCommand->m_pTextureMemoryUsage;
	*m_pTextureMemoryUsage = 0;

	m_FrameFormat = SDL_GetGPUSwapchainTextureFormat(m_pDevice, m_pWindow);
	m_PresentMode = pCommand->m_PresentMode;
	m_PresentIntervalNS = GetDisplayRefreshIntervalNS(m_pWindow);
	m_LastPresentNS = 0;

	// shaders, MSL entry points are named main0
	const char *pEntrypoint = pCommand->m_ShaderFormat == SDL_GPU_SHADERFORMAT_MSL ? "main0" : "main";
	SDL_GPUShaderCreateInfo ShaderInfo;
	mem_zero(&ShaderInfo, sizeof(ShaderInfo));
	ShaderInfo.code_size = pCommand->m_VertexShaderSize;
	ShaderInfo.code = pCommand->m_pVertexShaderCode;
	ShaderInfo.entrypoint = pEntrypoint;
	ShaderInfo.format = pCommand->m_ShaderFormat;
	ShaderInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
	ShaderInfo.num_samplers = 0;
	ShaderInfo.num_uniform_buffers = 1;
	m_pVertexShader = SDL_CreateGPUShader(m_pDevice, &ShaderInfo);

	mem_zero(&ShaderInfo, sizeof(ShaderInfo));
	ShaderInfo.code_size = pCommand->m_FragmentShaderSize;
	ShaderInfo.code = pCommand->m_pFragmentShaderCode;
	ShaderInfo.entrypoint = pEntrypoint;
	ShaderInfo.format = pCommand->m_ShaderFormat;
	ShaderInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	ShaderInfo.num_samplers = 1;
	ShaderInfo.num_uniform_buffers = 1;
	m_pFragmentShader = SDL_CreateGPUShader(m_pDevice, &ShaderInfo);

	// the tilemap shader samples the tile data texture in addition to the tileset
	mem_zero(&ShaderInfo, sizeof(ShaderInfo));
	ShaderInfo.code_size = pCommand->m_TilemapFragmentShaderSize;
	ShaderInfo.code = pCommand->m_pTilemapFragmentShaderCode;
	ShaderInfo.entrypoint = pEntrypoint;
	ShaderInfo.format = pCommand->m_ShaderFormat;
	ShaderInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	ShaderInfo.num_samplers = 2;
	ShaderInfo.num_uniform_buffers = 1;
	m_pTilemapFragmentShader = SDL_CreateGPUShader(m_pDevice, &ShaderInfo);
	if(!m_pVertexShader || !m_pFragmentShader || !m_pTilemapFragmentShader)
		dbg_msg("gfx", "failed to create SDL_GPU shaders: %s", SDL_GetError());

	// samplers
	for(int i = 0; i < NUM_BASIC_SAMPLERS; i++)
	{
		for(int j = 0; j < NUM_WRAP_SAMPLERS; j++)
		{
			SDL_GPUSamplerCreateInfo SamplerInfo;
			mem_zero(&SamplerInfo, sizeof(SamplerInfo));
			SamplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
			SamplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
			SamplerInfo.mipmap_mode = i == SAMPLER2D_MIPMAPS ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
			SamplerInfo.min_lod = 0.0f;
			SamplerInfo.max_lod = i == SAMPLER2D_MIPMAPS ? 1000.0f : 0.0f;

			switch(j)
			{
			case SAMPLER2D_REPEAT_REPEAT:
				SamplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
				SamplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
				break;
			case SAMPLER2D_REPEAT_CLAMP:
				SamplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
				SamplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
				break;
			case SAMPLER2D_CLAMP_CLAMP:
				SamplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
				SamplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
				break;
			case SAMPLER2D_CLAMP_REPEAT:
			default:
				SamplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
				SamplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
				break;
			}
			SamplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

			m_aaSamplers[i][j] = SDL_CreateGPUSampler(m_pDevice, &SamplerInfo);
		}
	}

	// tile data is read with texelFetch, so bind an exact (nearest) sampler
	{
		SDL_GPUSamplerCreateInfo SamplerInfo;
		mem_zero(&SamplerInfo, sizeof(SamplerInfo));
		SamplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
		SamplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
		SamplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		SamplerInfo.min_lod = 0.0f;
		SamplerInfo.max_lod = 0.0f;
		SamplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		SamplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		SamplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		m_pTileDataSampler = SDL_CreateGPUSampler(m_pDevice, &SamplerInfo);
	}

	// pipelines
	CreatePipeline(PIPELINE_QUADS_NONE, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, 0);
	CreatePipeline(PIPELINE_QUADS_ALPHA_PREMULTIPLIED, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, 1);
	CreatePipeline(PIPELINE_QUADS_ALPHA, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, 2);
	CreatePipeline(PIPELINE_LINES_NONE, SDL_GPU_PRIMITIVETYPE_LINELIST, 0);
	CreatePipeline(PIPELINE_LINES_ALPHA_PREMULTIPLIED, SDL_GPU_PRIMITIVETYPE_LINELIST, 1);
	CreatePipeline(PIPELINE_LINES_ALPHA, SDL_GPU_PRIMITIVETYPE_LINELIST, 2);

	CreateTilemapPipeline(PIPELINE_TILEMAP_NONE, PIPELINE_TILEMAP_NONE);
	CreateTilemapPipeline(PIPELINE_TILEMAP_ALPHA_PREMULTIPLIED, PIPELINE_TILEMAP_ALPHA_PREMULTIPLIED);

	// static quad index buffer
	m_IndexBufferNumIndices = CCommandBuffer::MAX_VERTICES / 4 * 6;
	unsigned int *pIndices = (unsigned int *)mem_alloc(m_IndexBufferNumIndices * sizeof(unsigned int));
	BuildQuadIndexBuffer(pIndices, m_IndexBufferNumIndices);

	SDL_GPUBufferCreateInfo BufferInfo;
	mem_zero(&BufferInfo, sizeof(BufferInfo));
	BufferInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
	BufferInfo.size = m_IndexBufferNumIndices * sizeof(unsigned int);
	m_pIndexBuffer = SDL_CreateGPUBuffer(m_pDevice, &BufferInfo);

	if(m_pIndexBuffer)
	{
		SDL_GPUTransferBufferCreateInfo TransferInfo;
		mem_zero(&TransferInfo, sizeof(TransferInfo));
		TransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		TransferInfo.size = m_IndexBufferNumIndices * sizeof(unsigned int);
		SDL_GPUTransferBuffer *pTransferBuffer = SDL_CreateGPUTransferBuffer(m_pDevice, &TransferInfo);
		if(pTransferBuffer)
		{
			void *pMapped = SDL_MapGPUTransferBuffer(m_pDevice, pTransferBuffer, false);
			mem_copy(pMapped, pIndices, TransferInfo.size);
			SDL_UnmapGPUTransferBuffer(m_pDevice, pTransferBuffer);

			SDL_GPUCommandBuffer *pCommandBuffer = SDL_AcquireGPUCommandBuffer(m_pDevice);
			SDL_GPUCopyPass *pCopyPass = SDL_BeginGPUCopyPass(pCommandBuffer);
			SDL_GPUTransferBufferLocation Src;
			Src.transfer_buffer = pTransferBuffer;
			Src.offset = 0;
			SDL_GPUBufferRegion Dst;
			Dst.buffer = m_pIndexBuffer;
			Dst.offset = 0;
			Dst.size = TransferInfo.size;
			SDL_UploadToGPUBuffer(pCopyPass, &Src, &Dst, false);
			SDL_EndGPUCopyPass(pCopyPass);
			SDL_SubmitGPUCommandBuffer(pCommandBuffer);
			SDL_ReleaseGPUTransferBuffer(m_pDevice, pTransferBuffer);
		}
	}
	mem_free(pIndices);

	// frame texture is created lazily on first use
	EnsureFrameTexture();

	if(pCommand->m_FsaaSamples > 1)
		dbg_msg("gfx", "SDL_GPU backend does not support FSAA yet, ignoring %d samples", pCommand->m_FsaaSamples);
	dbg_msg("gfx", "SDL_GPU backend initialized (swapchain format %d)", (int)m_FrameFormat);
}

void CCommandProcessorFragment_SDLGPU::Cmd_Shutdown([[maybe_unused]] const CShutdownCommand *pCommand)
{
	if(m_pCommandBuffer)
	{
		SDL_CancelGPUCommandBuffer(m_pCommandBuffer);
		m_pCommandBuffer = 0;
	}
	if(m_pFrameFence)
	{
		SDL_WaitForGPUFences(m_pDevice, true, &m_pFrameFence, 1);
		SDL_ReleaseGPUFence(m_pDevice, m_pFrameFence);
		m_pFrameFence = 0;
	}
	ReleaseDeferred();
	ResetFrameStaging();

	for(int i = 0; i < NUM_PIPELINES; i++)
	{
		if(m_apPipelines[i])
			SDL_ReleaseGPUGraphicsPipeline(m_pDevice, m_apPipelines[i]);
		m_apPipelines[i] = 0;
	}
	for(int i = 0; i < NUM_TILEMAP_PIPELINES; i++)
	{
		if(m_apTilemapPipelines[i])
			SDL_ReleaseGPUGraphicsPipeline(m_pDevice, m_apTilemapPipelines[i]);
		m_apTilemapPipelines[i] = 0;
	}
	if(m_pVertexShader)
		SDL_ReleaseGPUShader(m_pDevice, m_pVertexShader);
	m_pVertexShader = 0;
	if(m_pFragmentShader)
		SDL_ReleaseGPUShader(m_pDevice, m_pFragmentShader);
	m_pFragmentShader = 0;
	if(m_pTilemapFragmentShader)
		SDL_ReleaseGPUShader(m_pDevice, m_pTilemapFragmentShader);
	m_pTilemapFragmentShader = 0;
	for(int i = 0; i < NUM_BASIC_SAMPLERS; i++)
		for(int j = 0; j < NUM_WRAP_SAMPLERS; j++)
		{
			if(m_aaSamplers[i][j])
				SDL_ReleaseGPUSampler(m_pDevice, m_aaSamplers[i][j]);
			m_aaSamplers[i][j] = 0;
		}
	if(m_pTileDataSampler)
		SDL_ReleaseGPUSampler(m_pDevice, m_pTileDataSampler);
	m_pTileDataSampler = 0;
	if(m_pIndexBuffer)
		SDL_ReleaseGPUBuffer(m_pDevice, m_pIndexBuffer);
	m_pIndexBuffer = 0;
	if(m_pVertexBuffer)
		SDL_ReleaseGPUBuffer(m_pDevice, m_pVertexBuffer);
	m_pVertexBuffer = 0;
	if(m_pVertexTransferBuffer)
		SDL_ReleaseGPUTransferBuffer(m_pDevice, m_pVertexTransferBuffer);
	m_pVertexTransferBuffer = 0;
	m_VertexBufferCapacity = 0;
	m_VertexBufferUploaded = 0;
	if(m_pStagingData)
		mem_free(m_pStagingData);
	m_pStagingData = 0;
	m_StagingCapacity = 0;
	m_StagingUsed = 0;
	if(m_pPendingDraws)
		mem_free(m_pPendingDraws);
	m_pPendingDraws = 0;
	m_PendingDrawCapacity = 0;
	m_PendingDrawCount = 0;
	if(m_pFrameTexture)
		SDL_ReleaseGPUTexture(m_pDevice, m_pFrameTexture);
	m_pFrameTexture = 0;
	for(int i = 0; i < CCommandBuffer::MAX_TEXTURES; i++)
	{
		if(m_aTextures[i].m_Valid && m_aTextures[i].m_pTexture)
			SDL_ReleaseGPUTexture(m_pDevice, m_aTextures[i].m_pTexture);
		m_aTextures[i].m_Valid = false;
		m_aTextures[i].m_pTexture = 0;
	}
}

void CCommandProcessorFragment_SDLGPU::AddDeferredTexture(SDL_GPUTexture *pTexture)
{
	CDeferredRelease *pRelease = new CDeferredRelease;
	pRelease->m_pTexture = pTexture;
	pRelease->m_pBuffer = 0;
	pRelease->m_pTransferBuffer = 0;
	pRelease->m_pNext = m_pDeferredHead;
	m_pDeferredHead = pRelease;
}

void CCommandProcessorFragment_SDLGPU::AddDeferredBuffer(SDL_GPUBuffer *pBuffer)
{
	CDeferredRelease *pRelease = new CDeferredRelease;
	pRelease->m_pTexture = 0;
	pRelease->m_pBuffer = pBuffer;
	pRelease->m_pTransferBuffer = 0;
	pRelease->m_pNext = m_pDeferredHead;
	m_pDeferredHead = pRelease;
}

void CCommandProcessorFragment_SDLGPU::AddDeferredTransferBuffer(SDL_GPUTransferBuffer *pTransferBuffer)
{
	CDeferredRelease *pRelease = new CDeferredRelease;
	pRelease->m_pTexture = 0;
	pRelease->m_pBuffer = 0;
	pRelease->m_pTransferBuffer = pTransferBuffer;
	pRelease->m_pNext = m_pDeferredHead;
	m_pDeferredHead = pRelease;
}

void CCommandProcessorFragment_SDLGPU::ReleaseDeferred()
{
	while(m_pDeferredHead)
	{
		CDeferredRelease *pRelease = m_pDeferredHead;
		m_pDeferredHead = pRelease->m_pNext;
		if(pRelease->m_pTexture)
			SDL_ReleaseGPUTexture(m_pDevice, pRelease->m_pTexture);
		if(pRelease->m_pBuffer)
			SDL_ReleaseGPUBuffer(m_pDevice, pRelease->m_pBuffer);
		if(pRelease->m_pTransferBuffer)
			SDL_ReleaseGPUTransferBuffer(m_pDevice, pRelease->m_pTransferBuffer);
		delete pRelease;
	}
}

void CCommandProcessorFragment_SDLGPU::ClearPendingDraws()
{
	// the staged vertices stay, later draws of this frame simply append to them
	m_PendingDrawCount = 0;
}

void CCommandProcessorFragment_SDLGPU::ResetFrameStaging()
{
	m_PendingDrawCount = 0;
	m_StagingUsed = 0;
	m_VertexBufferUploaded = 0;
}

void CCommandProcessorFragment_SDLGPU::ApplyDraw(SDL_GPURenderPass *pPass, const CPendingDraw *pDraw)
{
	const CCommandBuffer::CState &State = pDraw->m_State;

	const bool HasTexture = State.m_Texture >= 0 && State.m_Texture < CCommandBuffer::MAX_TEXTURES && m_aTextures[State.m_Texture].m_Valid;
	const bool IsAlphaOnly = HasTexture && m_aTextures[State.m_Texture].m_Format == CCommandBuffer::TEXFORMAT_ALPHA;
	const bool SrcIsAlpha = HasTexture && m_aTextures[State.m_Texture].m_Format == CCommandBuffer::TEXFORMAT_RGB;

	// resolve the texture first: without one the draw is skipped entirely
	SDL_GPUTexture *pTexture;
	SDL_GPUSampler *pSampler;
	if(HasTexture)
	{
		const int WrapSamplerType = WrapModeToSamplerType(State.m_WrapModeU, State.m_WrapModeV);
		pTexture = m_aTextures[State.m_Texture].m_pTexture;
		pSampler = m_aaSamplers[m_aTextures[State.m_Texture].m_BasicSamplerType][WrapSamplerType];
	}
	else
	{
		pTexture = m_aTextures[0].m_pTexture;
		pSampler = m_aaSamplers[SAMPLER2D_NOMIPMAPS][SAMPLER2D_REPEAT_REPEAT];
	}
	if(!pTexture || !pSampler)
		return;

	SDL_GPUTexture *pTileData = 0;
	if(pDraw->m_IsTilemap)
	{
		if(pDraw->m_TileData < 0 || pDraw->m_TileData >= CCommandBuffer::MAX_TEXTURES || !m_aTextures[pDraw->m_TileData].m_Valid)
			return;
		pTileData = m_aTextures[pDraw->m_TileData].m_pTexture;
	}

	const int BlendVariant = State.m_BlendMode == CCommandBuffer::BLEND_NONE ? 0 : (SrcIsAlpha ? 2 : 1);
	SDL_GPUGraphicsPipeline *pPipeline;
	if(pDraw->m_IsTilemap)
		pPipeline = m_apTilemapPipelines[State.m_BlendMode == CCommandBuffer::BLEND_NONE ? PIPELINE_TILEMAP_NONE : PIPELINE_TILEMAP_ALPHA_PREMULTIPLIED];
	else
		pPipeline = m_apPipelines[(pDraw->m_PrimType == CCommandBuffer::PRIMTYPE_LINES ? 1 : 0) * 3 + BlendVariant];
	if(pPipeline != m_pLastPipeline)
	{
		SDL_BindGPUGraphicsPipeline(pPass, pPipeline);
		m_pLastPipeline = pPipeline;
	}

	// the buffer is bound once per pass, the draw picks its slice via the offset
	if(!m_BuffersBound)
	{
		SDL_GPUBufferBinding VertexBinding;
		VertexBinding.buffer = m_pVertexBuffer;
		VertexBinding.offset = 0;
		SDL_BindGPUVertexBuffers(pPass, 0, &VertexBinding, 1);
		if(m_pIndexBuffer)
		{
			SDL_GPUBufferBinding IndexBinding;
			IndexBinding.buffer = m_pIndexBuffer;
			IndexBinding.offset = 0;
			SDL_BindGPUIndexBuffer(pPass, &IndexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
		}
		m_BuffersBound = true;
	}

	// uniform data persists on the command buffer, only push it when it changed
	float aOrthoMatrix[16];
	ComputeOrthoMatrix(State, aOrthoMatrix);
	bool OrthoChanged = !m_LastOrthoMatrixValid;
	for(int i = 0; !OrthoChanged && i < 16; i++)
		OrthoChanged = aOrthoMatrix[i] != m_LastOrthoMatrix[i];
	if(OrthoChanged)
	{
		SDL_PushGPUVertexUniformData(m_pCommandBuffer, 0, aOrthoMatrix, sizeof(aOrthoMatrix));
		mem_copy(m_LastOrthoMatrix, aOrthoMatrix, sizeof(aOrthoMatrix));
		m_LastOrthoMatrixValid = true;
	}

	if(pDraw->m_IsTilemap)
	{
		struct STilemapUniforms
		{
			float m_MapSizeTileSize[4];
			int m_Params[4];
		} Uniforms;
		Uniforms.m_MapSizeTileSize[0] = (float)pDraw->m_TilemapWidth;
		Uniforms.m_MapSizeTileSize[1] = (float)pDraw->m_TilemapHeight;
		Uniforms.m_MapSizeTileSize[2] = 32.0f;
		Uniforms.m_MapSizeTileSize[3] = 0.0f;
		Uniforms.m_Params[0] = pDraw->m_TilemapPassMode;
		Uniforms.m_Params[1] = pDraw->m_TilemapLayer;
		Uniforms.m_Params[2] = pDraw->m_TilemapColorOpaque ? 1 : 0;
		Uniforms.m_Params[3] = 0;
		SDL_PushGPUFragmentUniformData(m_pCommandBuffer, 0, &Uniforms, sizeof(Uniforms));
		// the quad shader uses the same slot, so force it to be pushed again
		m_LastFragmentFlagsValid = false;

		SDL_GPUTextureSamplerBinding aBindings[2];
		aBindings[0].texture = pTexture;
		aBindings[0].sampler = pSampler;
		aBindings[1].texture = pTileData;
		aBindings[1].sampler = m_pTileDataSampler ? m_pTileDataSampler : m_aaSamplers[SAMPLER2D_NOMIPMAPS][SAMPLER2D_CLAMP_CLAMP];
		SDL_BindGPUFragmentSamplers(pPass, 0, aBindings, 2);
		m_pLastTexture = pTexture;
		m_pLastSampler = pSampler;
		m_pLastTileDataTexture = pTileData;
	}
	else
	{
		CQuadFragmentUniforms Uniforms = {};
		Uniforms.m_UseTexture = HasTexture ? 1 : 0;
		Uniforms.m_IsAlphaOnly = IsAlphaOnly ? 1 : 0;
		Uniforms.m_IsStainedOnly = State.m_IsStainedOnly ? 1 : 0;
		Uniforms.m_IsSDF = State.m_IsSDF ? 1 : 0;
		Uniforms.m_SDFGain = State.m_SDFGain;
		Uniforms.m_SDFOutlineOffset = State.m_SDFOutlineOffset;
		Uniforms.m_SDFOutlineColor[0] = State.m_SDFOutlineColor.r;
		Uniforms.m_SDFOutlineColor[1] = State.m_SDFOutlineColor.g;
		Uniforms.m_SDFOutlineColor[2] = State.m_SDFOutlineColor.b;
		Uniforms.m_SDFOutlineColor[3] = State.m_SDFOutlineColor.a;

		bool FlagsChanged = !m_LastFragmentFlagsValid;
		if(!FlagsChanged)
			FlagsChanged = mem_comp(&Uniforms, &m_LastFragmentUniforms, sizeof(Uniforms)) != 0;
		if(FlagsChanged)
		{
			SDL_PushGPUFragmentUniformData(m_pCommandBuffer, 0, &Uniforms, sizeof(Uniforms));
			mem_copy(&m_LastFragmentUniforms, &Uniforms, sizeof(Uniforms));
			m_LastFragmentFlagsValid = true;
		}

		if(pTexture != m_pLastTexture || pSampler != m_pLastSampler)
		{
			SDL_GPUTextureSamplerBinding TextureSamplerBinding;
			TextureSamplerBinding.texture = pTexture;
			TextureSamplerBinding.sampler = pSampler;
			SDL_BindGPUFragmentSamplers(pPass, 0, &TextureSamplerBinding, 1);
			m_pLastTexture = pTexture;
			m_pLastSampler = pSampler;
		}
		m_pLastTileDataTexture = 0;
	}

	SDL_Rect Scissor;
	if(State.m_ClipEnable)
	{
		// clip coordinates are bottom-left in the command buffer, top-left in SDL_GPU
		Scissor.x = maximum(0, State.m_ClipX);
		Scissor.y = maximum(0, m_RenderTargetHeight - (State.m_ClipY + State.m_ClipH));
		Scissor.w = minimum(State.m_ClipW, m_RenderTargetWidth - Scissor.x);
		Scissor.h = minimum(State.m_ClipH, m_RenderTargetHeight - Scissor.y);
	}
	else
	{
		Scissor.x = 0;
		Scissor.y = 0;
		Scissor.w = m_RenderTargetWidth;
		Scissor.h = m_RenderTargetHeight;
	}
	if(!m_LastScissorValid || Scissor.x != m_LastScissor.x || Scissor.y != m_LastScissor.y ||
		Scissor.w != m_LastScissor.w || Scissor.h != m_LastScissor.h)
	{
		SDL_SetGPUScissor(pPass, &Scissor);
		m_LastScissor = Scissor;
		m_LastScissorValid = true;
	}

	const unsigned BaseVertex = pDraw->m_VertexOffset / sizeof(CCommandBuffer::CVertex);
	if(pDraw->m_IsTilemap)
	{
		if(!m_pIndexBuffer)
			return;
		SDL_DrawGPUIndexedPrimitives(pPass, 6, 1, 0, (Sint32)BaseVertex, 0);
	}
	else if(pDraw->m_PrimType == CCommandBuffer::PRIMTYPE_QUADS)
	{
		if(!m_pIndexBuffer)
			return;
		SDL_DrawGPUIndexedPrimitives(pPass, pDraw->m_PrimCount * 6, 1, 0, (Sint32)BaseVertex, 0);
	}
	else
	{
		SDL_DrawGPUPrimitives(pPass, pDraw->m_NumVertices, 1, BaseVertex, 0);
	}
}

void CCommandProcessorFragment_SDLGPU::FlushDraws()
{
	// nothing to draw and the target was already rendered to this frame
	if(m_FrameCleared && m_PendingDrawCount == 0)
		return;
	if(!EnsureCommandBuffer())
	{
		ClearPendingDraws();
		return;
	}

	// use the swapchain image when given, otherwise the offscreen frame texture
	SDL_GPUTexture *pTarget = m_pRenderTarget;
	const bool TargetIsFrameTexture = pTarget == 0;
	if(TargetIsFrameTexture)
	{
		if(!EnsureFrameTexture())
		{
			ClearPendingDraws();
			return;
		}
		pTarget = m_pFrameTexture;
		m_RenderTargetWidth = m_FrameTextureWidth;
		m_RenderTargetHeight = m_FrameTextureHeight;
		m_RenderToFrameTexture = true;
	}
	if(!pTarget)
	{
		ClearPendingDraws();
		return;
	}

	// a frame may flush several times, so only upload the part that is still new
	if(m_StagingUsed > m_VertexBufferUploaded)
	{
		if(!EnsureFrameBuffers(m_StagingUsed))
		{
			ClearPendingDraws();
			return;
		}
		const unsigned UploadStart = m_VertexBufferUploaded;
		const unsigned UploadSize = m_StagingUsed - UploadStart;

		void *pMapped = SDL_MapGPUTransferBuffer(m_pDevice, m_pVertexTransferBuffer, false);
		if(!pMapped)
		{
			ClearPendingDraws();
			return;
		}
		mem_copy((unsigned char *)pMapped + UploadStart, m_pStagingData + UploadStart, UploadSize);
		SDL_UnmapGPUTransferBuffer(m_pDevice, m_pVertexTransferBuffer);

		SDL_GPUCopyPass *pCopyPass = SDL_BeginGPUCopyPass(m_pCommandBuffer);
		SDL_GPUTransferBufferLocation Src;
		mem_zero(&Src, sizeof(Src));
		Src.transfer_buffer = m_pVertexTransferBuffer;
		Src.offset = UploadStart;
		SDL_GPUBufferRegion Dst;
		mem_zero(&Dst, sizeof(Dst));
		Dst.buffer = m_pVertexBuffer;
		Dst.offset = UploadStart;
		Dst.size = UploadSize;
		SDL_UploadToGPUBuffer(pCopyPass, &Src, &Dst, false);
		SDL_EndGPUCopyPass(pCopyPass);
		m_VertexBufferUploaded = m_StagingUsed;
	}

	SDL_GPUColorTargetInfo Target;
	mem_zero(&Target, sizeof(Target));
	Target.texture = pTarget;
	Target.mip_level = 0;
	Target.layer_or_depth_plane = 0;
	Target.clear_color = m_ClearColor;
	Target.load_op = m_FrameCleared ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
	Target.store_op = SDL_GPU_STOREOP_STORE;
	// cycling lets an in-flight frame keep the old texture; swapchain images are fresh
	Target.cycle = TargetIsFrameTexture && !m_FrameCleared;

	SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(m_pCommandBuffer, &Target, 1, 0);
	if(!pPass)
	{
		dbg_msg("gfx", "failed to begin render pass: %s", SDL_GetError());
		ClearPendingDraws();
		return;
	}
	m_FrameCleared = true;
	ResetRenderStateCache();

	for(unsigned i = 0; i < m_PendingDrawCount; i++)
		ApplyDraw(pPass, &m_pPendingDraws[i]);

	SDL_EndGPURenderPass(pPass);
	ClearPendingDraws();
}

void CCommandProcessorFragment_SDLGPU::Cmd_Texture_Update(const CCommandBuffer::CTextureUpdateCommand *pCommand)
{
	if(pCommand->m_Format == CCommandBuffer::TEXFORMAT_RGBA)
		PremultiplyAlpha((unsigned char *)pCommand->m_pData, pCommand->m_Width * pCommand->m_Height);

	void *pUploadData = pCommand->m_pData;
	int BytesPerPixel = GetPixelSize(pCommand->m_Format);
	if(pCommand->m_Format == CCommandBuffer::TEXFORMAT_RGB)
	{
		const int NumPixels = pCommand->m_Width * pCommand->m_Height;
		const unsigned char *pSrc = (const unsigned char *)pCommand->m_pData;
		unsigned char *pDst = (unsigned char *)mem_alloc((size_t)NumPixels * 4);
		for(int i = 0; i < NumPixels; i++)
		{
			pDst[i * 4 + 0] = pSrc[i * 3 + 0];
			pDst[i * 4 + 1] = pSrc[i * 3 + 1];
			pDst[i * 4 + 2] = pSrc[i * 3 + 2];
			pDst[i * 4 + 3] = 255;
		}
		pUploadData = pDst;
		BytesPerPixel = 4;
	}

	if(m_aTextures[pCommand->m_Slot].m_Valid)
		UploadTexture(m_aTextures[pCommand->m_Slot].m_pTexture, pCommand->m_X, pCommand->m_Y, pCommand->m_Z, pCommand->m_Width, pCommand->m_Height, 1, pUploadData, TexFormatToSDLGPUFormat(pCommand->m_Format), BytesPerPixel);

	if(pUploadData != pCommand->m_pData)
		mem_free(pUploadData);
	mem_free(pCommand->m_pData);
}

void CCommandProcessorFragment_SDLGPU::Cmd_Texture_Destroy(const CCommandBuffer::CTextureDestroyCommand *pCommand)
{
	if(m_aTextures[pCommand->m_Slot].m_Valid && m_aTextures[pCommand->m_Slot].m_pTexture)
		AddDeferredTexture(m_aTextures[pCommand->m_Slot].m_pTexture);
	*m_pTextureMemoryUsage -= m_aTextures[pCommand->m_Slot].m_MemSize;
	m_aTextures[pCommand->m_Slot].m_Valid = false;
	m_aTextures[pCommand->m_Slot].m_pTexture = 0;
	m_aTextures[pCommand->m_Slot].m_MemSize = 0;
}

void CCommandProcessorFragment_SDLGPU::Cmd_Texture_Create(const CCommandBuffer::CTextureCreateCommand *pCommand)
{
	int Width;
	int Height;
	const int Layers = pCommand->m_Layers;
	void *pTexData = PrepareTextureData(pCommand, Width, Height);
	m_aTextures[pCommand->m_Slot].m_Format = pCommand->m_Format;

	const bool Mipmaps = !(pCommand->m_Flags & CCommandBuffer::TEXFLAG_NOMIPMAPS);
	const int Levels = Mipmaps ? NumMipLevels(Width, Height) : 1;
	const SDL_GPUTextureFormat GpuFormat = TexFormatToSDLGPUFormat(pCommand->m_Format);

	SDL_GPUTextureCreateInfo Info;
	mem_zero(&Info, sizeof(Info));
	Info.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
	Info.format = GpuFormat;
	Info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (Mipmaps ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
	Info.width = Width;
	Info.height = Height;
	Info.layer_count_or_depth = Layers;
	Info.num_levels = Levels;
	Info.sample_count = SDL_GPU_SAMPLECOUNT_1;

	SDL_GPUTexture *pTexture = SDL_CreateGPUTexture(m_pDevice, &Info);
	m_aTextures[pCommand->m_Slot].m_pTexture = pTexture;
	m_aTextures[pCommand->m_Slot].m_Valid = pTexture != 0;
	m_aTextures[pCommand->m_Slot].m_BasicSamplerType = SAMPLER2D_NOMIPMAPS;
	if(Mipmaps)
		m_aTextures[pCommand->m_Slot].m_BasicSamplerType = SAMPLER2D_MIPMAPS;

	void *pUploadData = pTexData;
	int BytesPerPixel = GetPixelSize(pCommand->m_Format);
	if(pCommand->m_Format == CCommandBuffer::TEXFORMAT_RGB)
	{
		const int NumPixels = Width * Height * Layers;
		const unsigned char *pSrc = (const unsigned char *)pTexData;
		unsigned char *pDst = (unsigned char *)mem_alloc((size_t)NumPixels * 4);
		for(int i = 0; i < NumPixels; i++)
		{
			pDst[i * 4 + 0] = pSrc[i * 3 + 0];
			pDst[i * 4 + 1] = pSrc[i * 3 + 1];
			pDst[i * 4 + 2] = pSrc[i * 3 + 2];
			pDst[i * 4 + 3] = 255;
		}
		pUploadData = pDst;
		BytesPerPixel = 4;
	}

	if(pTexture)
		UploadTexture(pTexture, 0, 0, 0, Width, Height, Layers, pUploadData, GpuFormat, BytesPerPixel);

	if(pUploadData != pTexData)
		mem_free(pUploadData);

	if(Mipmaps && Levels > 1 && pTexture)
	{
		if(EnsureCommandBuffer())
			SDL_GenerateMipmapsForGPUTexture(m_pCommandBuffer, pTexture);
	}

	// calculate memory usage
	CalcTextureMemSize(Width, Height, Layers, pCommand->m_PixelSize, Mipmaps, m_aTextures[pCommand->m_Slot].m_MemSize);
	*m_pTextureMemoryUsage += m_aTextures[pCommand->m_Slot].m_MemSize;

	mem_free(pTexData);
}

void CCommandProcessorFragment_SDLGPU::Cmd_Clear(const CCommandBuffer::CClearCommand *pCommand)
{
	// only flush when there is something to draw, an empty flush would start a
	// render pass with the previous clear color
	if(m_PendingDrawCount > 0)
		FlushDraws();
	m_ClearColor.r = pCommand->m_Color.r;
	m_ClearColor.g = pCommand->m_Color.g;
	m_ClearColor.b = pCommand->m_Color.b;
	m_ClearColor.a = 1.0f;
	m_FrameCleared = false;
}

void CCommandProcessorFragment_SDLGPU::Cmd_Render(const CCommandBuffer::CRenderCommand *pCommand)
{
	const unsigned NumVertices = pCommand->m_PrimType == CCommandBuffer::PRIMTYPE_QUADS ? pCommand->m_PrimCount * 4 : pCommand->m_PrimCount * 2;
	const unsigned Bytes = NumVertices * sizeof(CCommandBuffer::CVertex);
	if(Bytes == 0)
		return;

	if(!EnsureStagingCapacity(m_StagingUsed + Bytes) || !EnsurePendingDrawCapacity(m_PendingDrawCount + 1))
		return;

	CPendingDraw *pDraw = &m_pPendingDraws[m_PendingDrawCount++];
	pDraw->m_State = pCommand->m_State;
	pDraw->m_PrimType = pCommand->m_PrimType;
	pDraw->m_PrimCount = pCommand->m_PrimCount;
	pDraw->m_NumVertices = NumVertices;
	pDraw->m_VertexOffset = m_StagingUsed;
	pDraw->m_IsTilemap = false;

	mem_copy(m_pStagingData + m_StagingUsed, pCommand->m_pVertices, Bytes);
	m_StagingUsed += Bytes;
}

void CCommandProcessorFragment_SDLGPU::Cmd_RenderTilemapTexture(const CCommandBuffer::CRenderTilemapTextureCommand *pCommand)
{
	if(pCommand->m_TileData < 0 || pCommand->m_TileData >= CCommandBuffer::MAX_TEXTURES || !m_aTextures[pCommand->m_TileData].m_Valid)
		return;
	if(pCommand->m_State.m_Texture < 0 || pCommand->m_State.m_Texture >= CCommandBuffer::MAX_TEXTURES || !m_aTextures[pCommand->m_State.m_Texture].m_Valid)
		return;

	const unsigned Bytes = 4 * sizeof(CCommandBuffer::CVertex);
	if(!EnsureStagingCapacity(m_StagingUsed + Bytes) || !EnsurePendingDrawCapacity(m_PendingDrawCount + 1))
		return;

	CPendingDraw *pDraw = &m_pPendingDraws[m_PendingDrawCount++];
	pDraw->m_State = pCommand->m_State;
	pDraw->m_PrimType = CCommandBuffer::PRIMTYPE_QUADS;
	pDraw->m_PrimCount = 1;
	pDraw->m_NumVertices = 4;
	pDraw->m_VertexOffset = m_StagingUsed;
	pDraw->m_IsTilemap = true;
	pDraw->m_TileData = pCommand->m_TileData;
	pDraw->m_TilemapLayer = pCommand->m_Layer;
	pDraw->m_TilemapWidth = pCommand->m_Width;
	pDraw->m_TilemapHeight = pCommand->m_Height;
	pDraw->m_TilemapPassMode = pCommand->m_PassMode;
	pDraw->m_TilemapColorOpaque = pCommand->m_ColorOpaque ? 1 : 0;

	mem_copy(m_pStagingData + m_StagingUsed, pCommand->m_aVertices, Bytes);
	m_StagingUsed += Bytes;
}

void CCommandProcessorFragment_SDLGPU::Cmd_Screenshot(const CCommandBuffer::CScreenshotCommand *pCommand)
{
	pCommand->m_pImage->m_pData = 0;
	FlushDraws();
	if(!m_pCommandBuffer || !m_pFrameTexture)
		return;

	int x = maximum(0, pCommand->m_X);
	int y = maximum(0, pCommand->m_Y);
	int w = pCommand->m_W == -1 ? m_FrameTextureWidth : pCommand->m_W;
	int h = pCommand->m_H == -1 ? m_FrameTextureHeight : pCommand->m_H;
	w = minimum(w, m_FrameTextureWidth - x);
	h = minimum(h, m_FrameTextureHeight - y);
	if(w <= 0 || h <= 0)
		return;

	SDL_GPUTransferBufferCreateInfo TransferInfo;
	mem_zero(&TransferInfo, sizeof(TransferInfo));
	TransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	TransferInfo.size = w * h * 4;
	SDL_GPUTransferBuffer *pTransferBuffer = SDL_CreateGPUTransferBuffer(m_pDevice, &TransferInfo);
	if(!pTransferBuffer)
		return;

	SDL_GPUCopyPass *pCopyPass = SDL_BeginGPUCopyPass(m_pCommandBuffer);

	SDL_GPUTextureRegion Src;
	mem_zero(&Src, sizeof(Src));
	Src.texture = m_pFrameTexture;
	Src.mip_level = 0;
	Src.layer = 0;
	Src.x = x;
	Src.y = y;
	Src.z = 0;
	Src.w = w;
	Src.h = h;
	Src.d = 1;

	SDL_GPUTextureTransferInfo Dst;
	mem_zero(&Dst, sizeof(Dst));
	Dst.transfer_buffer = pTransferBuffer;
	Dst.offset = 0;
	Dst.pixels_per_row = 0;
	Dst.rows_per_layer = 0;

	SDL_DownloadFromGPUTexture(pCopyPass, &Src, &Dst);
	SDL_EndGPUCopyPass(pCopyPass);

	SDL_GPUFence *pFence = SDL_SubmitGPUCommandBufferAndAcquireFence(m_pCommandBuffer);
	m_pCommandBuffer = 0;
	if(pFence)
	{
		SDL_WaitForGPUFences(m_pDevice, true, &pFence, 1);
		SDL_ReleaseGPUFence(m_pDevice, pFence);
	}

	unsigned char *pData = (unsigned char *)SDL_MapGPUTransferBuffer(m_pDevice, pTransferBuffer, false);
	if(pData)
	{
		unsigned char *pPixels = (unsigned char *)mem_alloc(w * h * 4);
		// SDL_GPU textures use a top-left origin, so no vertical flip is needed
		mem_copy(pPixels, pData, w * h * 4);
		SDL_UnmapGPUTransferBuffer(m_pDevice, pTransferBuffer);

		// the frame texture may be BGRA, while CImageInfo expects RGBA
		if(m_FrameFormat == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM || m_FrameFormat == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB)
		{
			for(int i = 0; i < w * h; i++)
			{
				const unsigned char Tmp = pPixels[i * 4 + 0];
				pPixels[i * 4 + 0] = pPixels[i * 4 + 2];
				pPixels[i * 4 + 2] = Tmp;
			}
		}

		pCommand->m_pImage->m_Width = w;
		pCommand->m_pImage->m_Height = h;
		pCommand->m_pImage->m_Format = CImageInfo::FORMAT_RGBA;
		pCommand->m_pImage->m_pData = pPixels;
	}
	SDL_ReleaseGPUTransferBuffer(m_pDevice, pTransferBuffer);
}

void CCommandProcessorFragment_SDLGPU::Cmd_Swap(const CCommandBuffer::CSwapCommand *pCommand)
{
	// a screenshot may have submitted the frame already, acquire a new command buffer
	if(!EnsureCommandBuffer())
		return;

	const bool VSync = m_PresentMode == SDL_GPU_PRESENTMODE_VSYNC;

	// the compositor only shows one frame per refresh, so skip frames instead of
	// blocking on the presentation queue and dragging the game loop down with it
	bool Present = true;
	if(!VSync && m_PresentIntervalNS > 0 && m_LastPresentNS != 0 &&
		SDL_GetTicksNS() - m_LastPresentNS < m_PresentIntervalNS)
	{
		Present = false;
	}

	SDL_GPUTexture *pSwapchainTexture = 0;
	Uint32 SwapchainWidth = 0;
	Uint32 SwapchainHeight = 0;
	bool Acquired = true;
	if(Present)
	{
		if(VSync)
		{
			// vsync is supposed to pace the game loop, so blocking is intended
			Acquired = SDL_WaitAndAcquireGPUSwapchainTexture(m_pCommandBuffer, m_pWindow, &pSwapchainTexture, &SwapchainWidth, &SwapchainHeight);
		}
		else
		{
			Acquired = SDL_AcquireGPUSwapchainTexture(m_pCommandBuffer, m_pWindow, &pSwapchainTexture, &SwapchainWidth, &SwapchainHeight);
		}
	}

	if(Acquired && pSwapchainTexture)
	{
		m_LastPresentNS = SDL_GetTicksNS();

		if(m_RenderToFrameTexture)
		{
			// the frame is already in the offscreen texture, finish it there and
			// present it with a blit
			m_pRenderTarget = 0;
			FlushDraws();

			if(m_pFrameTexture)
			{
				SDL_GPUBlitInfo Blit;
				mem_zero(&Blit, sizeof(Blit));
				Blit.source.texture = m_pFrameTexture;
				Blit.source.mip_level = 0;
				Blit.source.layer_or_depth_plane = 0;
				Blit.source.x = 0;
				Blit.source.y = 0;
				Blit.source.w = m_FrameTextureWidth;
				Blit.source.h = m_FrameTextureHeight;
				Blit.destination.texture = pSwapchainTexture;
				Blit.destination.mip_level = 0;
				Blit.destination.layer_or_depth_plane = 0;
				Blit.destination.x = 0;
				Blit.destination.y = 0;
				Blit.destination.w = SwapchainWidth;
				Blit.destination.h = SwapchainHeight;
				Blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
				Blit.clear_color = m_ClearColor;
				Blit.flip_mode = SDL_FLIP_NONE;
				Blit.filter = SDL_GPU_FILTER_NEAREST;
				Blit.cycle = false;
				SDL_BlitGPUTexture(m_pCommandBuffer, &Blit);
			}
		}
		else
		{
			// draw straight into the swapchain image, no offscreen pass and no blit
			m_pRenderTarget = pSwapchainTexture;
			m_RenderTargetWidth = (int)SwapchainWidth;
			m_RenderTargetHeight = (int)SwapchainHeight;
			FlushDraws();
		}
	}
	else if(!Acquired)
	{
		// only report this once, a lost device would otherwise flood the log
		static bool s_LoggedSwapchainError = false;
		if(!s_LoggedSwapchainError)
		{
			dbg_msg("gfx", "failed to acquire swapchain texture: %s", SDL_GetError());
			s_LoggedSwapchainError = true;
		}
	}
	// a NULL texture means no image was ready, ResetFrameStaging drops the draws

	m_pRenderTarget = 0;
	m_RenderTargetWidth = 0;
	m_RenderTargetHeight = 0;
	m_RenderToFrameTexture = false;

	m_pFrameFence = SDL_SubmitGPUCommandBufferAndAcquireFence(m_pCommandBuffer);
	m_pCommandBuffer = 0;
	m_FrameCleared = false;
	// the next frame waits on this fence before reusing the staging and vertex buffer
	ResetFrameStaging();

	if(pCommand->m_Finish && m_pFrameFence)
	{
		SDL_WaitForGPUFences(m_pDevice, true, &m_pFrameFence, 1);
		SDL_ReleaseGPUFence(m_pDevice, m_pFrameFence);
		m_pFrameFence = 0;
		ReleaseDeferred();
	}
}

void CCommandProcessorFragment_SDLGPU::Cmd_VSync(const CCommandBuffer::CVSyncCommand *pCommand)
{
	const bool VSync = pCommand->m_VSync != 0;
	const SDL_GPUPresentMode Mode = ChoosePresentMode(m_pDevice, m_pWindow, VSync);
	if(!VSync && Mode == SDL_GPU_PRESENTMODE_VSYNC)
	{
		dbg_msg("gfx", "SDL_GPU: neither IMMEDIATE nor MAILBOX is supported, vsync cannot be disabled");
		*pCommand->m_pRetOk = false;
		return;
	}
	*pCommand->m_pRetOk = SDL_SetGPUSwapchainParameters(m_pDevice, m_pWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, Mode);
	if(*pCommand->m_pRetOk)
	{
		m_PresentMode = Mode;
		// the display may have changed, so refresh the pacing interval as well
		m_PresentIntervalNS = GetDisplayRefreshIntervalNS(m_pWindow);
		m_LastPresentNS = 0;
		dbg_msg("gfx", "SDL_GPU present mode: %s", PresentModeName(Mode));
	}
	else
		dbg_msg("gfx", "SDL_GPU: failed to set present mode %s: %s", PresentModeName(Mode), SDL_GetError());
}

void CCommandProcessorFragment_SDLGPU::Cmd_WindowResized([[maybe_unused]] const CCommandBuffer::CWindowResizedCommand *pCommand)
{
	// the frame texture is recreated lazily in EnsureFrameTexture
}

bool CCommandProcessorFragment_SDLGPU::RunCommand(const CCommandBuffer::CCommand *pBaseCommand)
{
	switch(pBaseCommand->m_Cmd)
	{
	case CMD_INIT: Cmd_Init(static_cast<const CInitCommand *>(pBaseCommand)); break;
	case CMD_GPU_SHUTDOWN: Cmd_Shutdown(static_cast<const CShutdownCommand *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_WINDOWRESIZED: Cmd_WindowResized(static_cast<const CCommandBuffer::CWindowResizedCommand *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_TEXTURE_CREATE: Cmd_Texture_Create(static_cast<const CCommandBuffer::CTextureCreateCommand *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_TEXTURE_DESTROY: Cmd_Texture_Destroy(static_cast<const CCommandBuffer::CTextureDestroyCommand *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_TEXTURE_UPDATE: Cmd_Texture_Update(static_cast<const CCommandBuffer::CTextureUpdateCommand *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_CLEAR: Cmd_Clear(static_cast<const CCommandBuffer::CClearCommand *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_RENDER: Cmd_Render(static_cast<const CCommandBuffer::CRenderCommand *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_RENDER_TILEMAP_TEXTURE: Cmd_RenderTilemapTexture(static_cast<const CCommandBuffer::CRenderTilemapTextureCommand *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_SCREENSHOT: Cmd_Screenshot(static_cast<const CCommandBuffer::CScreenshotCommand *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_SWAP: Cmd_Swap(static_cast<const CCommandBuffer::CSwapCommand *>(pBaseCommand)); break;
	case CCommandBuffer::CMD_VSYNC: Cmd_VSync(static_cast<const CCommandBuffer::CVSyncCommand *>(pBaseCommand)); break;
	default: return false;
	}
	return true;
}

// ------------ CCommandProcessor_SDL_GPU

bool CCommandProcessor_SDL_GPU::RunBackendCommand(CCommandBuffer::CCommand *pCommand)
{
	return m_SDLGPU.RunCommand(pCommand);
}

// ------------ CGraphicsBackend_SDL_GPU

CGraphicsBackend_SDL_GPU::CGraphicsBackend_SDL_GPU(IStorage *pStorage) :
	CGraphicsBackend_SDL(pStorage)
{
	m_pDevice = 0;
	m_pVertexShaderCode = 0;
	m_VertexShaderSize = 0;
	m_pFragmentShaderCode = 0;
	m_FragmentShaderSize = 0;
	m_pTilemapFragmentShaderCode = 0;
	m_TilemapFragmentShaderSize = 0;
}

int CGraphicsBackend_SDL_GPU::Init(const char *pName, int *pScreen, int *pWindowWidth, int *pWindowHeight, int *pScreenWidth, int *pScreenHeight, int FsaaSamples, int Flags, int *pDesktopWidth, int *pDesktopHeight)
{
	if(InitWindow(pName, pScreen, pWindowWidth, pWindowHeight, pScreenWidth, pScreenHeight, Flags, pDesktopWidth, pDesktopHeight, 0) != 0)
		return -1;

#ifdef CONF_DEBUG
	const bool DebugMode = true;
#else
	const bool DebugMode = false;
#endif
	// the backends we ship shaders for, SDL picks the matching driver
	m_pDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, DebugMode, 0);
	if(m_pDevice == NULL)
	{
		dbg_msg("gfx", "unable to create SDL_GPU device: %s", SDL_GetError());
		ShutdownWindow();
		return -1;
	}
	dbg_msg("gfx", "using SDL_GPU driver: %s", SDL_GetGPUDeviceDriver(m_pDevice));

	// pick the shader sources matching the driver SDL selected
	SDL_GPUShaderFormat ShaderFormat;
	const char *pVertexShaderFile;
	const char *pFragmentShaderFile;
	const char *pTilemapFragmentShaderFile;
	if(SDL_GetGPUShaderFormats(m_pDevice) & SDL_GPU_SHADERFORMAT_MSL)
	{
		ShaderFormat = SDL_GPU_SHADERFORMAT_MSL;
		pVertexShaderFile = "shaders/metal/quad.vert.msl";
		pFragmentShaderFile = "shaders/metal/quad.frag.msl";
		pTilemapFragmentShaderFile = "shaders/metal/tilemap.frag.msl";
	}
	else
	{
		ShaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
		pVertexShaderFile = "shaders/vulkan/quad.vert.spv";
		pFragmentShaderFile = "shaders/vulkan/quad.frag.spv";
		pTilemapFragmentShaderFile = "shaders/vulkan/tilemap.frag.spv";
	}

	// three images in flight, two is not enough slack to keep the pacing smooth
	if(!SDL_SetGPUAllowedFramesInFlight(m_pDevice, 3))
		dbg_msg("gfx", "unable to raise SDL_GPU frames in flight: %s", SDL_GetError());

	if(!SDL_ClaimWindowForGPUDevice(m_pDevice, m_pWindow))
	{
		dbg_msg("gfx", "unable to claim window for SDL_GPU device: %s", SDL_GetError());
		SDL_DestroyGPUDevice(m_pDevice);
		m_pDevice = 0;
		ShutdownWindow();
		return -1;
	}

	const bool VSync = (Flags & IGraphicsBackend::INITFLAG_VSYNC) != 0;
	const SDL_GPUPresentMode PresentMode = ChoosePresentMode(m_pDevice, m_pWindow, VSync);
	if(!VSync && PresentMode == SDL_GPU_PRESENTMODE_VSYNC)
		dbg_msg("gfx", "SDL_GPU: neither IMMEDIATE nor MAILBOX is supported, vsync cannot be disabled");
	if(SDL_SetGPUSwapchainParameters(m_pDevice, m_pWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, PresentMode))
		dbg_msg("gfx", "SDL_GPU present mode: %s", PresentModeName(PresentMode));
	else
		dbg_msg("gfx", "SDL_GPU: failed to set present mode %s: %s", PresentModeName(PresentMode), SDL_GetError());

	// load the shaders from the data directory
	if(!m_pStorage->ReadFile(pVertexShaderFile, IStorage::TYPE_ALL, (void **)&m_pVertexShaderCode, &m_VertexShaderSize) ||
		!m_pStorage->ReadFile(pFragmentShaderFile, IStorage::TYPE_ALL, (void **)&m_pFragmentShaderCode, &m_FragmentShaderSize) ||
		!m_pStorage->ReadFile(pTilemapFragmentShaderFile, IStorage::TYPE_ALL, (void **)&m_pTilemapFragmentShaderCode, &m_TilemapFragmentShaderSize))
	{
		dbg_msg("gfx", "unable to load SDL_GPU shaders");
		SDL_ReleaseWindowFromGPUDevice(m_pDevice, m_pWindow);
		SDL_DestroyGPUDevice(m_pDevice);
		m_pDevice = 0;
		ShutdownWindow();
		return -1;
	}

	m_pProcessor = new CCommandProcessor_SDL_GPU;
	StartProcessor(m_pProcessor);

	// Vulkan/D3D12/Metal all guarantee at least a 4096x4096 2D texture
	m_MaxTextureSize = 4096;

	CCommandBuffer CmdBuffer(1024, 512);
	CCommandProcessorFragment_SDLGPU::CInitCommand Cmd;
	Cmd.m_pDevice = m_pDevice;
	Cmd.m_pWindow = m_pWindow;
	Cmd.m_pTextureMemoryUsage = &m_TextureMemoryUsage;
	Cmd.m_FsaaSamples = FsaaSamples;
	Cmd.m_PresentMode = PresentMode;
	Cmd.m_ShaderFormat = ShaderFormat;
	Cmd.m_pVertexShaderCode = m_pVertexShaderCode;
	Cmd.m_VertexShaderSize = m_VertexShaderSize;
	Cmd.m_pFragmentShaderCode = m_pFragmentShaderCode;
	Cmd.m_FragmentShaderSize = m_FragmentShaderSize;
	Cmd.m_pTilemapFragmentShaderCode = m_pTilemapFragmentShaderCode;
	Cmd.m_TilemapFragmentShaderSize = m_TilemapFragmentShaderSize;
	CmdBuffer.AddCommand(Cmd);
	RunBuffer(&CmdBuffer);
	WaitForIdle();

	return 0;
}

int CGraphicsBackend_SDL_GPU::Shutdown()
{
	{
		CCommandBuffer CmdBuffer(1024, 512);
		CCommandProcessorFragment_SDLGPU::CShutdownCommand Cmd;
		CmdBuffer.AddCommand(Cmd);
		RunBuffer(&CmdBuffer);
		WaitForIdle();
	}

	StopAndDeleteProcessor();

	mem_free(m_pVertexShaderCode);
	m_pVertexShaderCode = 0;
	mem_free(m_pFragmentShaderCode);
	m_pFragmentShaderCode = 0;
	mem_free(m_pTilemapFragmentShaderCode);
	m_pTilemapFragmentShaderCode = 0;

	if(m_pDevice)
	{
		SDL_ReleaseWindowFromGPUDevice(m_pDevice, m_pWindow);
		SDL_DestroyGPUDevice(m_pDevice);
		m_pDevice = 0;
	}
	ShutdownWindow();
	return 0;
}

// ------------ backend selection

IGraphicsBackend *CreateGraphicsBackend(IStorage *pStorage, int Backend)
{
	// 0 = auto, 1 = OpenGL ES, 2 = SDL_GPU
	if(Backend == 1)
		return new CGraphicsBackend_SDL_OpenGL(pStorage);
	return new CGraphicsBackend_SDL_GPU(pStorage);
}
