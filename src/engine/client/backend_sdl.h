/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef ENGINE_CLIENT_BACKEND_SDL_H
#define ENGINE_CLIENT_BACKEND_SDL_H

#include "graphics_threaded.h"
#include <base/system/mem.h>
#include <base/system/time.h>

#include <engine/external/glad/gl.h>

#include <SDL3/SDL.h>

#if defined(CONF_PLATFORM_MACOS)
#include <objc/objc-runtime.h>

class CAutoreleasePool
{
private:
	id m_Pool;

public:
	CAutoreleasePool()
	{
		Class NSAutoreleasePoolClass = (Class) objc_getClass("NSAutoreleasePool");
		m_Pool = class_createInstance(NSAutoreleasePoolClass, 0);
		SEL selector = sel_registerName("init");
		((id (*)(id, SEL)) objc_msgSend)(m_Pool, selector);
	}

	~CAutoreleasePool()
	{
		SEL selector = sel_registerName("drain");
		((id (*)(id, SEL)) objc_msgSend)(m_Pool, selector);
	}
};
#endif

// basic threaded backend, abstract, missing init and shutdown functions
class CGraphicsBackend_Threaded : public IGraphicsBackend
{
public:
	// constructed on the main thread, the rest of the functions is run on the render thread
	class ICommandProcessor
	{
	public:
		virtual ~ICommandProcessor() {}
		virtual void RunBuffer(CCommandBuffer *pBuffer) = 0;
	};

	CGraphicsBackend_Threaded();

	virtual void RunBuffer(CCommandBuffer *pBuffer) override;
	virtual bool IsIdle() const override;
	virtual void WaitForIdle() override;

	// conservative default, overridden once the device has been created
	virtual int MaxTextureSize() const override { return 2048; }

	// time the render thread spent inside RunBuffer() since the last call
	virtual int64 TakeRenderThreadTime() override
	{
		const int64 Time = m_RenderThreadTime;
		m_RenderThreadTime = 0;
		return Time;
	}

protected:
	void StartProcessor(ICommandProcessor *pProcessor);
	void StopProcessor();

private:
	ICommandProcessor *m_pProcessor;
	CCommandBuffer *volatile m_pBuffer;
	int64 volatile m_RenderThreadTime;
	volatile bool m_Shutdown;
	semaphore m_Activity;
	semaphore m_BufferDone;
	void *m_pThread;

	static void ThreadFunc(void *pUser);
};

// takes care of implementation independent operations
class CCommandProcessorFragment_General
{
	void Cmd_Nop();
	void Cmd_Signal(const CCommandBuffer::CSignalCommand *pCommand);

public:
	bool RunCommand(const CCommandBuffer::CCommand *pBaseCommand);
};

// takes care of texture handling shared by the rendering fragments
class CCommandProcessorFragment_Texture
{
public:
	enum
	{
		SAMPLER2D_NOMIPMAPS = 0,
		SAMPLER2D_MIPMAPS,
		NUM_BASIC_SAMPLERS,

		SAMPLER2D_REPEAT_REPEAT = 0,
		SAMPLER2D_REPEAT_CLAMP,
		SAMPLER2D_CLAMP_CLAMP,
		SAMPLER2D_CLAMP_REPEAT,
		NUM_WRAP_SAMPLERS,
	};

protected:
	volatile int *m_pTextureMemoryUsage;
	int m_MaxTexSize;

	CCommandProcessorFragment_Texture();

	static int GetPixelSize(int TexFormat);
	static int NumMipLevels(int Width, int Height);
	static int WrapModeToSamplerType(int WrapModeU, int WrapModeV);
	static void PremultiplyAlpha(unsigned char *pTexels, int NumPixels);
	static void ComputeOrthoMatrix(const CCommandBuffer::CState &State, float *pMatrix);
	static void BuildQuadIndexBuffer(unsigned int *pIndices, int NumIndices);

	// resamples and premultiplies texture data, caller frees the result with mem_free
	void *PrepareTextureData(const CCommandBuffer::CTextureCreateCommand *pCommand, int &Width, int &Height);
	void CalcTextureMemSize(int Width, int Height, int Layers, int PixelSize, bool Mipmaps, int &MemSize);
};

// takes care of OpenGL ES related rendering
class CCommandProcessorFragment_OpenGL : public CCommandProcessorFragment_Texture
{
	GLuint m_PrimitiveDrawVertexID;
	GLuint m_PrimitiveDrawBufferID;

	struct CRenderShader
	{
		GLuint m_ShaderProgram;
		int m_UseTextureLoc;
		int m_IsAlphaOnlyLoc;
		int m_IsStainedOnlyLoc;
		int m_IsSDFLoc;
		int m_SDFGainLoc;
		int m_SDFOutlineOffsetLoc;
		int m_SDFOutlineColorLoc;
		int m_OurTextureLoc;
		int m_ProjectionLoc;
	} m_RenderShader;

	// dedicated tile map shader, it samples a tile data texture in addition to the tileset
	struct CTilemapShader
	{
		GLuint m_ShaderProgram;
		int m_OurTextureLoc;
		int m_TileDataLoc;
		int m_MapSizeLoc;
		int m_PassModeLoc;
		int m_LayerIndexLoc;
		int m_ColorOpaqueLoc;
		int m_ProjectionLoc;
	} m_TilemapShader;

	class CTexture
	{
	public:
		GLuint m_Texture;
		bool m_Valid;
		int m_Format;
		int m_MemSize;
		int m_BasicSamplerType;
	};
	CTexture m_aTextures[CCommandBuffer::MAX_TEXTURES];
	int m_Max2DArrayLayers;
	GLuint m_QuadDrawIndexBufferID;
	int m_LastSrcBlendMode;

	bool m_LastAlphaOnly;
	bool m_LastStainedOnly;
	bool m_LastUseTexture;
	GLuint m_LastTextureID;

	// cached SDF uniforms so redundant glUniform calls are skipped
	bool m_LastSDFValid;
	IGraphics::CTextSDFParams m_LastSDFParams;

	bool m_LastClipEnable;

	GLuint m_LastSampler;
	GLuint m_aaSampler2D[NUM_BASIC_SAMPLERS][NUM_WRAP_SAMPLERS];

public:
	enum
	{
		CMD_INIT = CCommandBuffer::CMDGROUP_PLATFORM_OPENGL,
		CMD_GL_SHUTDOWN,
	};

	struct CInitCommand : public CCommandBuffer::CCommand
	{
		CInitCommand() : CCommand(CMD_INIT) {}
		volatile int *m_pTextureMemoryUsage;
		// receives the GL_MAX_TEXTURE_SIZE queried during init
		volatile int *m_pMaxTextureSize;
		// shader sources, must stay alive until the command is processed
		const char *m_pVertexShaderSource;
		const char *m_pFragmentShaderSource;
		const char *m_pTilemapFragmentShaderSource;
	};

	struct CGLShutdownCommand : public CCommandBuffer::CCommand
	{
		CGLShutdownCommand() :
			CCommand(CMD_GL_SHUTDOWN) {}
	};

private:
	static int TexFormatToOpenGLFormat(int TexFormat);

	bool SetState(const CCommandBuffer::CState &State);

	GLuint CompileShader(GLuint Type, const char *pSource);
	GLuint CreateShaderProgram(const char *pVertexSource, const char *pFragmentSource);

	void Cmd_Init(const CInitCommand *pCommand);
	void Cmd_Shutdown(const CGLShutdownCommand *pCommand);
	void Cmd_SetViewport(const CCommandBuffer::CWindowResizedCommand *pCommand);
	void Cmd_Texture_Update(const CCommandBuffer::CTextureUpdateCommand *pCommand);
	void Cmd_Texture_Destroy(const CCommandBuffer::CTextureDestroyCommand *pCommand);
	void Cmd_Texture_Create(const CCommandBuffer::CTextureCreateCommand *pCommand);
	void Cmd_Clear(const CCommandBuffer::CClearCommand *pCommand);
	void Cmd_Render(const CCommandBuffer::CRenderCommand *pCommand);
	void Cmd_RenderTilemapTexture(const CCommandBuffer::CRenderTilemapTextureCommand *pCommand);
	void Cmd_Screenshot(const CCommandBuffer::CScreenshotCommand *pCommand);

public:
	CCommandProcessorFragment_OpenGL();

	bool RunCommand(const CCommandBuffer::CCommand *pBaseCommand);
};

// takes care of sdl related commands
class CCommandProcessorFragment_SDL
{
	// SDL stuff
	SDL_Window *m_pWindow;
	SDL_GLContext m_GLContext;

public:
	enum
	{
		CMD_INIT = CCommandBuffer::CMDGROUP_PLATFORM_SDL,
		CMD_SHUTDOWN,
	};

	struct CInitCommand : public CCommandBuffer::CCommand
	{
		CInitCommand() : CCommand(CMD_INIT) {}
		SDL_Window *m_pWindow;
		SDL_GLContext m_GLContext;
	};

	struct CShutdownCommand : public CCommandBuffer::CCommand
	{
		CShutdownCommand() : CCommand(CMD_SHUTDOWN) {}
	};

private:
	void Cmd_Init(const CInitCommand *pCommand);
	void Cmd_Shutdown(const CShutdownCommand *pCommand);
	void Cmd_Swap(const CCommandBuffer::CSwapCommand *pCommand);
	void Cmd_VSync(const CCommandBuffer::CVSyncCommand *pCommand);

public:
	CCommandProcessorFragment_SDL();

	bool RunCommand(const CCommandBuffer::CCommand *pBaseCommand);
};

// combines the general fragment with the backend specific ones
class CCommandProcessor_SDL : public CGraphicsBackend_Threaded::ICommandProcessor
{
	CCommandProcessorFragment_General m_General;

protected:
	// runs the backend specific fragments, false if none handled the command
	virtual bool RunBackendCommand(CCommandBuffer::CCommand *pCommand) = 0;

public:
	virtual void RunBuffer(CCommandBuffer *pBuffer) override;
};

// OpenGL ES command processor
class CCommandProcessor_SDL_OpenGL : public CCommandProcessor_SDL
{
	CCommandProcessorFragment_OpenGL m_OpenGL;
	CCommandProcessorFragment_SDL m_SDL;

protected:
	virtual bool RunBackendCommand(CCommandBuffer::CCommand *pCommand) override;
};

// shared SDL window handling for the SDL based graphics backends
class CGraphicsBackend_SDL : public CGraphicsBackend_Threaded
{
protected:
	SDL_Window *m_pWindow;
	ICommandProcessor *m_pProcessor;
	volatile int m_TextureMemoryUsage;
	// reported by the render thread during init, else stays at the default
	volatile int m_MaxTextureSize;
	int m_NumScreens;
	class IStorage *m_pStorage;

	// creates the SDL window and initializes the shared screen parameters
	int InitWindow(const char *pName, int *pScreen, int *pWindowWidth, int *pWindowHeight,
		int *pScreenWidth, int *pScreenHeight, int Flags, int *pDesktopWidth,
		int *pDesktopHeight, int ExtraSdlFlags);
	// destroys the window and shuts the video subsystem down
	void ShutdownWindow();
	// stops the render thread and deletes the command processor
	void StopAndDeleteProcessor();

public:
	CGraphicsBackend_SDL(class IStorage *pStorage);
	virtual ~CGraphicsBackend_SDL() {}

	virtual int MemoryUsage() const override;

	virtual int MaxTextureSize() const override { return m_MaxTextureSize; }

	virtual int GetNumScreens() const override { return m_NumScreens; }

	virtual void Minimize() override;
	virtual void Maximize() override;
	virtual bool Fullscreen(bool State) override; // on=true/off=false
	virtual void SetWindowBordered(bool State) override; // on=true/off=false
	virtual bool SetWindowScreen(int Index) override;
	virtual int GetWindowScreen() override;
	virtual int GetVideoModes(CVideoMode *pModes, int MaxModes, int Screen) override;
	virtual bool GetDesktopResolution(int Index, int *pDesktopWidth, int *pDesktopHeight) override;
	virtual bool WindowActive() override;
	virtual bool WindowOpen() override;

	virtual bool ResizeWindow(int Width, int Height) override;
	virtual void *GetWindowHandle() override;
};

// graphics backend implemented with SDL and OpenGL ES
class CGraphicsBackend_SDL_OpenGL : public CGraphicsBackend_SDL
{
	SDL_GLContext m_GLContext;
	char *m_pVertexShaderSource;
	char *m_pFragmentShaderSource;
	char *m_pTilemapFragmentShaderSource;

public:
	CGraphicsBackend_SDL_OpenGL(class IStorage *pStorage);
	virtual int Init(const char *pName, int *pScreen, int *pWindowWidth, int *pWindowHeight, int *pScreenWidth, int *pScreenHeight, int FsaaSamples, int Flags, int *pDesktopWidth, int *pDesktopHeight) override;
	virtual int Shutdown() override;
};

#endif // ENGINE_CLIENT_BACKEND_SDL_H
