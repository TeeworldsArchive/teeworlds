/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/detect.h>
#include <base/tl/threading.h>

#include <engine/external/glad/gl.h>
#include <SDL3/SDL.h>

#include "backend_sdl.h"
#include "graphics_threaded.h"

static const char *s_VertexShaderSource = R"(
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aTexCoord;
layout (location = 2) in vec4 aColor;

out vec3 TexCoord;
out vec4 Color;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
    Color = aColor;
}
)";

static const char *s_FragmentShaderSource = R"(
#ifdef USE_2D_TEXTURE
    #define SAMPLER_TYPE sampler2D
#else
    #define SAMPLER_TYPE sampler2DArray
#endif
out vec4 FragColor;

in vec3 TexCoord;
in vec4 Color;

uniform PRECISION_TYPE SAMPLER_TYPE ourTexture;
uniform bool useTexture;
uniform bool IsAlphaOnly;
uniform bool IsStainedOnly;

void main()
{
    if(useTexture)
    {
	#ifdef USE_2D_TEXTURE
        vec4 texColor = texture(ourTexture, TexCoord.xy);
	#else
		vec4 texColor = texture(ourTexture, TexCoord.xyz);
	#endif
       	if(IsAlphaOnly)
        {
            FragColor = vec4(Color.rgb, texColor.r * Color.a);
        }
		else if(IsStainedOnly)
		{
			const float epsilon = 0.2;
			if(abs(texColor.r - texColor.g) < epsilon && abs(texColor.r - texColor.b) < epsilon)
			{
				FragColor = texColor * vec4(Color.rgb * Color.a, Color.a);
			}
			else
			{
				FragColor = vec4(texColor.rgb, texColor.a * Color.a);
			}
		}
        else
        {
            // Regular RGBA textures
       		FragColor = texColor * Color;
        }
    }
    else
    {
        // No texture, use vertex color directly
        FragColor = Color;
    }
}
)";

static const char *s_ShaderVersionES = R"(
#version 320 es
precision highp float;
precision highp int;
#define PRECISION_TYPE highp
)";
static const char *s_ShaderVersion = R"(
#version 330 core
#define PRECISION_TYPE
)";

// ------------ CGraphicsBackend_Threaded

void CGraphicsBackend_Threaded::ThreadFunc(void *pUser)
{
	CGraphicsBackend_Threaded *pThis = (CGraphicsBackend_Threaded *) pUser;

	while(!pThis->m_Shutdown)
	{
		pThis->m_Activity.wait();
		if(pThis->m_pBuffer)
		{
#ifdef CONF_PLATFORM_MACOS
			CAutoreleasePool AutoreleasePool;
#endif
			pThis->m_pProcessor->RunBuffer(pThis->m_pBuffer);
			sync_barrier();
			pThis->m_pBuffer = 0x0;
			pThis->m_BufferDone.signal();
		}
	}
}

CGraphicsBackend_Threaded::CGraphicsBackend_Threaded()
{
	m_pBuffer = 0x0;
	m_pProcessor = 0x0;
	m_pThread = 0x0;
}

void CGraphicsBackend_Threaded::StartProcessor(ICommandProcessor *pProcessor)
{
	m_Shutdown = false;
	m_pProcessor = pProcessor;
	m_pThread = thread_init(ThreadFunc, this);
	m_BufferDone.signal();
}

void CGraphicsBackend_Threaded::StopProcessor()
{
	m_Shutdown = true;
	m_Activity.signal();
	thread_wait(m_pThread);
	thread_destroy(m_pThread);
}

void CGraphicsBackend_Threaded::RunBuffer(CCommandBuffer *pBuffer)
{
	WaitForIdle();
	m_pBuffer = pBuffer;
	m_Activity.signal();
}

bool CGraphicsBackend_Threaded::IsIdle() const
{
	return m_pBuffer == 0x0;
}

void CGraphicsBackend_Threaded::WaitForIdle()
{
	while(m_pBuffer != 0x0)
		m_BufferDone.wait();
}

// ------------ CCommandProcessorFragment_General

void CCommandProcessorFragment_General::Cmd_Signal(const CCommandBuffer::CSignalCommand *pCommand)
{
	pCommand->m_pSemaphore->signal();
}

bool CCommandProcessorFragment_General::RunCommand(const CCommandBuffer::CCommand *pBaseCommand)
{
	switch(pBaseCommand->m_Cmd)
	{
		case CCommandBuffer::CMD_NOP: break;
		case CCommandBuffer::CMD_SIGNAL: Cmd_Signal(static_cast<const CCommandBuffer::CSignalCommand *>(pBaseCommand)); break;
		default: return false;
	}

	return true;
}

// ------------ CCommandProcessorFragment_OpenGL

int CCommandProcessorFragment_OpenGL::TexFormatToOpenGLFormat(int TexFormat)
{
	switch(TexFormat)
	{
		case CCommandBuffer::TEXFORMAT_RGB:
			return GL_RGB;
		case CCommandBuffer::TEXFORMAT_ALPHA:
			return GL_RED;
		case CCommandBuffer::TEXFORMAT_RGBA:
			return GL_RGBA;
		default:
			return GL_RGBA;
	}
}

int CCommandProcessorFragment_OpenGL::GetPixelSize(int TexFormat)
{
	switch(TexFormat)
	{
		case CCommandBuffer::TEXFORMAT_RGB:
			return 3;
		case CCommandBuffer::TEXFORMAT_ALPHA:
			return 1;
		case CCommandBuffer::TEXFORMAT_RGBA:
			return 4;
		default:
			return 4;
	}
}

unsigned char CCommandProcessorFragment_OpenGL::Sample(int w, int h, const unsigned char *pData, int u, int v, int Offset, int ScaleW, int ScaleH, int Bpp)
{
	int Sum = 0;
	for(int x = 0; x < ScaleW; x++)
		for(int y = 0; y < ScaleH; y++)
			Sum += pData[((v + y) * w + (u + x)) * Bpp + Offset];
	return Sum / (ScaleW * ScaleH);
}

void *CCommandProcessorFragment_OpenGL::Rescale(int Width, int Height, int NewWidth, int NewHeight, int Format, const unsigned char *pData)
{
	int ScaleW = Width / NewWidth;
	int ScaleH = Height / NewHeight;

	int Bpp = 3;
	if(Format == CCommandBuffer::TEXFORMAT_RGBA)
		Bpp = 4;

	unsigned char *pTmpData = (unsigned char *) mem_alloc(NewWidth * NewHeight * Bpp);

	for(int y = 0; y < NewHeight; y++)
		for(int x = 0; x < NewWidth; x++)
			for(int b = 0; b < Bpp; b++)
				pTmpData[(NewWidth * y + x) * Bpp + b] = Sample(Width, Height, pData, x * ScaleW, y * ScaleH, b, ScaleW, ScaleH, Bpp);

	return pTmpData;
}

GLuint CCommandProcessorFragment_OpenGL::CompileShader(GLuint Type, const char *pSource)
{
	GLuint Shader = glCreateShader(Type);
	glShaderSource(Shader, 1, &pSource, NULL);
	glCompileShader(Shader);

	int Success;
	char aInfoLog[512];
	glGetShaderiv(Shader, GL_COMPILE_STATUS, &Success);
	if(!Success)
	{
		glGetShaderInfoLog(Shader, 512, NULL, aInfoLog);
		dbg_msg("render", "Shader compilation failed: %s", aInfoLog);
	}

	return Shader;
}

GLuint CCommandProcessorFragment_OpenGL::CreateShaderProgram(bool Is3D)
{
	char aBuf[2048];
	str_format(aBuf, sizeof(aBuf), "%s%s%s", m_IsOpenGLES ? s_ShaderVersionES : s_ShaderVersion, Is3D ? "\n" : "#define USE_2D_TEXTURE\n", s_VertexShaderSource);
	GLuint VertexShader = CompileShader(GL_VERTEX_SHADER, aBuf);
	str_format(aBuf, sizeof(aBuf), "%s%s%s", m_IsOpenGLES ? s_ShaderVersionES : s_ShaderVersion, Is3D ? "\n" : "#define USE_2D_TEXTURE\n", s_FragmentShaderSource);
	GLuint FragmentShader = CompileShader(GL_FRAGMENT_SHADER, aBuf);

	GLuint ShaderProgram = glCreateProgram();
	glAttachShader(ShaderProgram, VertexShader);
	glAttachShader(ShaderProgram, FragmentShader);
	glLinkProgram(ShaderProgram);

	// Check for linking errors
	int Success;
	char aInfoLog[512];
	glGetProgramiv(ShaderProgram, GL_LINK_STATUS, &Success);
	if(!Success)
	{
		glGetProgramInfoLog(ShaderProgram, 512, NULL, aInfoLog);
		dbg_msg("render", "shader program linking failed: %s", aInfoLog);
	}

	glDeleteShader(VertexShader);
	glDeleteShader(FragmentShader);

	return ShaderProgram;
}

bool CCommandProcessorFragment_OpenGL::SetState(const CCommandBuffer::CState &State)
{
	// clip
	if(State.m_ClipEnable)
	{
		glScissor(State.m_ClipX, State.m_ClipY, State.m_ClipW, State.m_ClipH);
		if(!m_LastClipEnable)
		{
			glEnable(GL_SCISSOR_TEST);
			m_LastClipEnable = true;
		}
	}
	else if(m_LastClipEnable)
	{
		glDisable(GL_SCISSOR_TEST);
		m_LastClipEnable = false;
	}
	// texture handling
	bool HasValidTexture = false;
	int SrcBlendMode = GL_ONE;
	bool IsAlphaOnly = (m_aTextures[State.m_Texture].m_Format == CCommandBuffer::TEXFORMAT_ALPHA);
	bool IsTexture3D = State.m_Dimension == 3;
	const CRenderShader *pShader = &m_aRenderShader[IsTexture3D ? 1 : 0];
	if(IsTexture3D != m_LastTexture3D)
	{
		glUseProgram(pShader->m_ShaderProgram);
		glUniform1i(pShader->m_IsStainedOnlyLoc, State.m_IsStainedOnly ? 1 : 0);
		m_LastStainedOnly = State.m_IsStainedOnly;
		m_LastTextureID = -1;
		m_LastTexture3D = IsTexture3D;
		if(IsTexture3D)
			glBindSampler(0, m_Sampler2DArray);
	}
	else if(m_LastStainedOnly != State.m_IsStainedOnly)
	{
		glUniform1i(pShader->m_IsStainedOnlyLoc, State.m_IsStainedOnly ? 1 : 0);
		m_LastStainedOnly = State.m_IsStainedOnly;
	}

	if(State.m_Texture >= 0 && State.m_Texture < CCommandBuffer::MAX_TEXTURES)
	{
		if(State.m_Dimension == 2 && (m_aTextures[State.m_Texture].m_State & CTexture::STATE_TEX2D))
		{
			if(m_LastTextureID != m_aTextures[State.m_Texture].m_Tex2D)
			{
				glBindTexture(GL_TEXTURE_2D, m_aTextures[State.m_Texture].m_Tex2D);
				glUniform1i(pShader->m_IsAlphaOnlyLoc, IsAlphaOnly ? 1 : 0);
				glUniform1i(pShader->m_UseTextureLoc, 1);
			}
			else
			{
				if(m_LastAlphaOnly != IsAlphaOnly)
					glUniform1i(pShader->m_IsAlphaOnlyLoc, IsAlphaOnly ? 1 : 0);
				if(!m_LastUseTexture)
					glUniform1i(pShader->m_UseTextureLoc, 1);
			}

			m_LastTextureID = m_aTextures[State.m_Texture].m_Tex2D;
		}
		else if(State.m_Dimension == 3 && (m_aTextures[State.m_Texture].m_State & CTexture::STATE_TEX2DARRAY))
		{
			if(m_LastTextureID != m_aTextures[State.m_Texture].m_Tex2DArray)
			{
				glBindTexture(GL_TEXTURE_2D_ARRAY, m_aTextures[State.m_Texture].m_Tex2DArray);
				glUniform1i(pShader->m_IsAlphaOnlyLoc, IsAlphaOnly ? 1 : 0);
				glUniform1i(pShader->m_UseTextureLoc, 1);
			}
			else
			{
				if(m_LastAlphaOnly != IsAlphaOnly)
					glUniform1i(pShader->m_IsAlphaOnlyLoc, IsAlphaOnly ? 1 : 0);
				if(!m_LastUseTexture)
					glUniform1i(pShader->m_UseTextureLoc, 1);
			}

			m_LastTextureID = m_aTextures[State.m_Texture].m_Tex2DArray;
		}
		else
		{
			dbg_msg("render", "invalid texture %d %d %d", State.m_Texture, State.m_Dimension, m_aTextures[State.m_Texture].m_State);
			return false;
		}

		// Set blend mode based on texture type
		if(m_aTextures[State.m_Texture].m_Format == CCommandBuffer::TEXFORMAT_RGBA)
			SrcBlendMode = GL_ONE;
		else
			SrcBlendMode = GL_SRC_ALPHA;
		m_LastAlphaOnly = IsAlphaOnly;
		m_LastUseTexture = true;
		HasValidTexture = true;
	}
	else
	{
		if(m_LastAlphaOnly)
			glUniform1i(pShader->m_IsAlphaOnlyLoc, 0);
		if(m_LastUseTexture)
			glUniform1i(pShader->m_UseTextureLoc, 0);

		m_LastAlphaOnly = false;
		m_LastUseTexture = false;
	}

	if(m_LastSrcBlendMode != SrcBlendMode && State.m_BlendMode != CCommandBuffer::BLEND_NONE)
	{
		// blend
		switch(State.m_BlendMode)
		{
			case CCommandBuffer::BLEND_NONE:
				// glDisable(GL_BLEND);
				break;
			case CCommandBuffer::BLEND_ALPHA:
				// glEnable(GL_BLEND);
				glBlendFunc(SrcBlendMode, GL_ONE_MINUS_SRC_ALPHA);
				break;
			default:
				dbg_msg("render", "unknown blendmode %d\n", State.m_BlendMode);
		};

		m_LastSrcBlendMode = SrcBlendMode;
	}

	// wrap mode - only set if we have a valid texture
	if(HasValidTexture)
	{
		if(State.m_Dimension == 2)
		{
			int WrapSamplerType = SAMPLER2D_REPEAT_REPEAT;
			switch(State.m_WrapModeU)
			{
				case IGraphics::WRAP_REPEAT:
					switch(State.m_WrapModeV)
					{
						case IGraphics::WRAP_REPEAT: WrapSamplerType = SAMPLER2D_REPEAT_REPEAT; break;
						case IGraphics::WRAP_CLAMP: WrapSamplerType = SAMPLER2D_REPEAT_CLAMP; break;
						default: dbg_msg("render", "unknown wrapmode v %d", State.m_WrapModeV);
					};
					break;
				case IGraphics::WRAP_CLAMP:
					switch(State.m_WrapModeV)
					{
						case IGraphics::WRAP_REPEAT: WrapSamplerType = SAMPLER2D_CLAMP_REPEAT; break;
						case IGraphics::WRAP_CLAMP: WrapSamplerType = SAMPLER2D_CLAMP_CLAMP; break;
						default: dbg_msg("render", "unknown wrapmode v %d", State.m_WrapModeV);
					};
					break;
				default: dbg_msg("render", "unknown wrapmode u %d", State.m_WrapModeU);
			};
			GLuint SamplerID = m_aSampler2D[m_aTextures[State.m_Texture].m_BasicSamplerType][WrapSamplerType];
			if(m_LastSampler != SamplerID)
				glBindSampler(0, SamplerID);
			m_LastSampler = SamplerID;
		}
	}

	// screen mapping - Set projection matrix uniform
	float OrthoMatrix[16] = {
		2.0f / (State.m_ScreenBR.x - State.m_ScreenTL.x), 0, 0, 0,
		0, 2.0f / (State.m_ScreenTL.y - State.m_ScreenBR.y), 0, 0,
		0, 0, -1, 0,
		-(State.m_ScreenBR.x + State.m_ScreenTL.x) / (State.m_ScreenBR.x - State.m_ScreenTL.x),
		-(State.m_ScreenBR.y + State.m_ScreenTL.y) / (State.m_ScreenTL.y - State.m_ScreenBR.y), 0, 1};

	if(pShader->m_ProjectionLoc != -1)
		glUniformMatrix4fv(pShader->m_ProjectionLoc, 1, GL_FALSE, OrthoMatrix);
	return true;
}

void CCommandProcessorFragment_OpenGL::Cmd_Init(const CInitCommand *pCommand)
{
	m_IsOpenGLES = pCommand->m_IsOpenGLES;
	// Create shader program
	m_aRenderShader[0].m_ShaderProgram = CreateShaderProgram(false);
	m_aRenderShader[1].m_ShaderProgram = CreateShaderProgram(true);
	m_LastSrcBlendMode = GL_NONE;
	m_LastTexture3D = false;
	m_LastAlphaOnly = false;
	m_LastStainedOnly = false;
	m_LastUseTexture = false;
	m_LastTextureID = -1;
	m_LastSampler = 0;

	glEnable(GL_BLEND);

	// set some default settings
	glActiveTexture(GL_TEXTURE0);

	glGenSamplers(1, &m_Sampler2DArray);
	glSamplerParameteri(m_Sampler2DArray, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(m_Sampler2DArray, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(m_Sampler2DArray, GL_TEXTURE_WRAP_R, GL_REPEAT);
	glSamplerParameteri(m_Sampler2DArray, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glSamplerParameteri(m_Sampler2DArray, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// generate 2D samplers
	glGenSamplers(NUM_BASIC_SAMPLERS * NUM_WRAP_SAMPLERS, (GLuint *) m_aSampler2D);
	for(int i = 0; i < NUM_WRAP_SAMPLERS; i++)
	{
		glSamplerParameteri(m_aSampler2D[SAMPLER2D_NOMIPMAPS][i], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glSamplerParameteri(m_aSampler2D[SAMPLER2D_NOMIPMAPS][i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(m_aSampler2D[SAMPLER2D_MIPMAPS][i], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glSamplerParameteri(m_aSampler2D[SAMPLER2D_MIPMAPS][i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(m_aSampler2D[SAMPLER2D_LINERMIPMAPS][i], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glSamplerParameteri(m_aSampler2D[SAMPLER2D_LINERMIPMAPS][i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	for(int i = 0; i < NUM_BASIC_SAMPLERS; i++)
	{
		// u
		glSamplerParameteri(m_aSampler2D[i][SAMPLER2D_REPEAT_REPEAT], GL_TEXTURE_WRAP_S, GL_REPEAT);
		glSamplerParameteri(m_aSampler2D[i][SAMPLER2D_REPEAT_CLAMP], GL_TEXTURE_WRAP_S, GL_REPEAT);
		glSamplerParameteri(m_aSampler2D[i][SAMPLER2D_CLAMP_CLAMP], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glSamplerParameteri(m_aSampler2D[i][SAMPLER2D_CLAMP_REPEAT], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

		// v
		glSamplerParameteri(m_aSampler2D[i][SAMPLER2D_REPEAT_REPEAT], GL_TEXTURE_WRAP_T, GL_REPEAT);
		glSamplerParameteri(m_aSampler2D[i][SAMPLER2D_REPEAT_CLAMP], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glSamplerParameteri(m_aSampler2D[i][SAMPLER2D_CLAMP_CLAMP], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glSamplerParameteri(m_aSampler2D[i][SAMPLER2D_CLAMP_REPEAT], GL_TEXTURE_WRAP_T, GL_REPEAT);
	}


	// initialize shader uniforms with default values
	for(int i = 0; i < 2; i++)
	{
		m_aRenderShader[i].m_UseTextureLoc = glGetUniformLocation(m_aRenderShader[i].m_ShaderProgram, "useTexture");
		m_aRenderShader[i].m_IsAlphaOnlyLoc = glGetUniformLocation(m_aRenderShader[i].m_ShaderProgram, "IsAlphaOnly");
		m_aRenderShader[i].m_IsStainedOnlyLoc = glGetUniformLocation(m_aRenderShader[i].m_ShaderProgram, "IsStainedOnly");
		m_aRenderShader[i].m_OurTextureLoc = glGetUniformLocation(m_aRenderShader[i].m_ShaderProgram, "ourTexture");
		m_aRenderShader[i].m_ProjectionLoc = glGetUniformLocation(m_aRenderShader[i].m_ShaderProgram, "projection");

		glUniform1i(m_aRenderShader[i].m_UseTextureLoc, 0);
		glUniform1i(m_aRenderShader[i].m_IsAlphaOnlyLoc, 0);
		glUniform1i(m_aRenderShader[i].m_IsStainedOnlyLoc, 0);
		glUniform1i(m_aRenderShader[i].m_OurTextureLoc, 0);
	}
	glUseProgram(m_aRenderShader[0].m_ShaderProgram);

	glGenVertexArrays(1, &m_PrimitiveDrawVertexID);
	glBindVertexArray(m_PrimitiveDrawVertexID);

	glGenBuffers(1, &m_PrimitiveDrawBufferID);
	glBindBuffer(GL_ARRAY_BUFFER, m_PrimitiveDrawBufferID);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(CCommandBuffer::CVertex), 0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(CCommandBuffer::CVertex), (void *) (sizeof(float) * 2));
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(CCommandBuffer::CVertex), (void *) (sizeof(float) * 5));

	glGenBuffers(1, &m_QuadDrawIndexBufferID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_QuadDrawIndexBufferID);

	unsigned int aIndices[CCommandBuffer::MAX_VERTICES / 4 * 6];
	int Primq = 0;
	for(int i = 0; i < CCommandBuffer::MAX_VERTICES / 4 * 6; i += 6)
	{
		aIndices[i] = Primq + 0;
		aIndices[i + 1] = Primq + 1;
		aIndices[i + 2] = Primq + 2;
		aIndices[i + 3] = Primq;
		aIndices[i + 4] = Primq + 2;
		aIndices[i + 5] = Primq + 3;
		Primq += 4;
	}
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * CCommandBuffer::MAX_VERTICES / 4 * 6, aIndices, GL_STATIC_DRAW);

	m_pTextureMemoryUsage = pCommand->m_pTextureMemoryUsage;
	*m_pTextureMemoryUsage = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_MaxTexSize);
	glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &m_Max2DArrayLayers);
	dbg_msg("render", "opengl max texture sizes: %d, max array texture layers: %d", m_MaxTexSize, m_Max2DArrayLayers);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(0);
}

void CCommandProcessorFragment_OpenGL::Cmd_Shutdown(const CGLShutdownCommand *pCommand)
{
	glBindVertexArray(0);
	glDeleteBuffers(1, &m_PrimitiveDrawBufferID);
	glDeleteBuffers(1, &m_QuadDrawIndexBufferID);
	glDeleteVertexArrays(1, &m_PrimitiveDrawVertexID);
	glDeleteShader(m_aRenderShader[0].m_ShaderProgram);
	glDeleteShader(m_aRenderShader[1].m_ShaderProgram);
	glDeleteSamplers(1, &m_Sampler2DArray);
	glDeleteSamplers(NUM_BASIC_SAMPLERS * NUM_WRAP_SAMPLERS, (GLuint *) m_aSampler2D);
}

void CCommandProcessorFragment_OpenGL::Cmd_Texture_Update(const CCommandBuffer::CTextureUpdateCommand *pCommand)
{
	if(m_aTextures[pCommand->m_Slot].m_State & CTexture::STATE_TEX2D)
	{
		glBindTexture(GL_TEXTURE_2D, m_aTextures[pCommand->m_Slot].m_Tex2D);
		glTexSubImage2D(GL_TEXTURE_2D, 0, pCommand->m_X, pCommand->m_Y,
			pCommand->m_Width, pCommand->m_Height,
			TexFormatToOpenGLFormat(pCommand->m_Format),
			GL_UNSIGNED_BYTE, pCommand->m_pData);
	}
	mem_free(pCommand->m_pData);
}

void CCommandProcessorFragment_OpenGL::Cmd_Texture_Destroy(const CCommandBuffer::CTextureDestroyCommand *pCommand)
{
	if(m_aTextures[pCommand->m_Slot].m_State & CTexture::STATE_TEX2D)
		glDeleteTextures(1, &m_aTextures[pCommand->m_Slot].m_Tex2D);
	if(m_aTextures[pCommand->m_Slot].m_State & CTexture::STATE_TEX2DARRAY)
		glDeleteTextures(1, &m_aTextures[pCommand->m_Slot].m_Tex2DArray);
	*m_pTextureMemoryUsage -= m_aTextures[pCommand->m_Slot].m_MemSize;
	m_aTextures[pCommand->m_Slot].m_State = CTexture::STATE_EMPTY;
	m_aTextures[pCommand->m_Slot].m_MemSize = 0;
}

void CCommandProcessorFragment_OpenGL::Cmd_Texture_Create(const CCommandBuffer::CTextureCreateCommand *pCommand)
{
	int Width = pCommand->m_Width;
	int Height = pCommand->m_Height;
	void *pTexData = pCommand->m_pData;

	// resample if needed
	if(pCommand->m_Format == CCommandBuffer::TEXFORMAT_RGBA || pCommand->m_Format == CCommandBuffer::TEXFORMAT_RGB)
	{
		if(Width > m_MaxTexSize || Height > m_MaxTexSize)
		{
			do
			{
				Width >>= 1;
				Height >>= 1;
			} while(Width > m_MaxTexSize || Height > m_MaxTexSize);

			void *pTmpData = Rescale(pCommand->m_Width, pCommand->m_Height, Width, Height, pCommand->m_Format, static_cast<const unsigned char *>(pCommand->m_pData));
			if(pTexData != pCommand->m_pData)
				mem_free(pTexData);
			pTexData = pTmpData;
		}
		else if(Width > IGraphics::NUMTILES_DIMENSION && Height > IGraphics::NUMTILES_DIMENSION && (pCommand->m_Flags & CCommandBuffer::TEXFLAG_QUALITY) == 0)
		{
			Width >>= 1;
			Height >>= 1;

			void *pTmpData = Rescale(pCommand->m_Width, pCommand->m_Height, Width, Height, pCommand->m_Format, static_cast<const unsigned char *>(pCommand->m_pData));
			if(pTexData != pCommand->m_pData)
				mem_free(pTexData);
			pTexData = pTmpData;
		}
	}

	// use premultiplied alpha for rgba textures
	if(pCommand->m_Format == CCommandBuffer::TEXFORMAT_RGBA)
	{
		unsigned char *pTexels = (unsigned char *) pTexData;
		for(int i = 0; i < Width * Height; ++i)
		{
			const float a = (pTexels[i * 4 + 3] / 255.0f);
			pTexels[i * 4 + 0] = (unsigned char) (pTexels[i * 4 + 0] * a);
			pTexels[i * 4 + 1] = (unsigned char) (pTexels[i * 4 + 1] * a);
			pTexels[i * 4 + 2] = (unsigned char) (pTexels[i * 4 + 2] * a);
		}
	}
	m_aTextures[pCommand->m_Slot].m_Format = pCommand->m_Format;

	//
	int Oglformat = TexFormatToOpenGLFormat(pCommand->m_Format);
	int StoreOglformat = TexFormatToOpenGLFormat(pCommand->m_StoreFormat);

	if(pCommand->m_Flags & CCommandBuffer::TEXFLAG_COMPRESSED)
	{
		if(m_IsOpenGLES)
		{
			switch(StoreOglformat)
			{
				case GL_RGB: StoreOglformat = GL_COMPRESSED_RGB8_ETC2; break;
				case GL_RED: StoreOglformat = GL_COMPRESSED_R11_EAC; break;
				case GL_RGBA: StoreOglformat = GL_COMPRESSED_RGBA8_ETC2_EAC; break;
				default: StoreOglformat = GL_COMPRESSED_RGBA8_ETC2_EAC;
			}
		}
		else
		{
			switch(StoreOglformat)
			{
				case GL_RGB: StoreOglformat = GL_COMPRESSED_RGB; break;
				case GL_RED: StoreOglformat = GL_COMPRESSED_RED; break;
				case GL_RGBA: StoreOglformat = GL_COMPRESSED_RGBA; break;
				default: StoreOglformat = GL_COMPRESSED_RGBA;
			}
		}
	}
	m_aTextures[pCommand->m_Slot].m_BasicSamplerType = SAMPLER2D_NOMIPMAPS;
	// 2D texture
	if(pCommand->m_Flags & CCommandBuffer::TEXFLAG_TEXTURE2D)
	{
		bool Mipmaps = !(pCommand->m_Flags & CCommandBuffer::TEXFLAG_NOMIPMAPS);
		glGenTextures(1, &m_aTextures[pCommand->m_Slot].m_Tex2D);
		m_aTextures[pCommand->m_Slot].m_State |= CTexture::STATE_TEX2D;
		glBindTexture(GL_TEXTURE_2D, m_aTextures[pCommand->m_Slot].m_Tex2D);

		if(!Mipmaps)
		{
			glTexImage2D(GL_TEXTURE_2D, 0, StoreOglformat, Width, Height, 0, Oglformat, GL_UNSIGNED_BYTE, pTexData);
		}
		else
		{
			m_aTextures[pCommand->m_Slot].m_BasicSamplerType = (pCommand->m_Flags & CCommandBuffer::TEXFLAG_LINEARMIPMAPS) ? SAMPLER2D_LINERMIPMAPS : SAMPLER2D_MIPMAPS;
			glTexImage2D(GL_TEXTURE_2D, 0, StoreOglformat, Width, Height, 0, Oglformat, GL_UNSIGNED_BYTE, pTexData);
			glGenerateMipmap(GL_TEXTURE_2D);
		}

		// calculate memory usage
		m_aTextures[pCommand->m_Slot].m_MemSize = Width * Height * pCommand->m_PixelSize;
		if(Mipmaps)
		{
			int TexWidth = Width;
			int TexHeight = Height;
			while(TexWidth > 1 && TexHeight > 1)
			{
				TexWidth >>= 1;
				TexHeight >>= 1;
				m_aTextures[pCommand->m_Slot].m_MemSize += TexWidth * TexHeight * pCommand->m_PixelSize;
			}
		}
	}

	// 2D array texture
	if(pCommand->m_Flags & CCommandBuffer::TEXFLAG_TEXTURE2DARRAY)
	{
		const int TileWidth = Width / IGraphics::NUMTILES_DIMENSION;
		const int TileHeight = Height / IGraphics::NUMTILES_DIMENSION;
		const int Depth = IGraphics::NUMTILES_DIMENSION * IGraphics::NUMTILES_DIMENSION;

		// allocate memory for 3D texture data
		const int MemSize = TileWidth * TileHeight * Depth * pCommand->m_PixelSize;
		char *pTmpData = (char *) mem_alloc(MemSize);
		mem_zero(pTmpData, MemSize);

		const int TileSize = TileWidth * TileHeight * pCommand->m_PixelSize;
		const int TileRowSize = TileWidth * pCommand->m_PixelSize;

		// copy
		for(int i = 0; i < Depth; i++)
		{
			const int px = (i % IGraphics::NUMTILES_DIMENSION) * TileWidth;
			const int py = (i / IGraphics::NUMTILES_DIMENSION) * TileHeight;

			for(int y = 0; y < TileHeight; y++)
			{
				const int SrcOffset = ((py + y) * Width + px) * pCommand->m_PixelSize;
				const int DestOffset = (i * TileSize) + (y * TileRowSize);
				mem_copy(pTmpData + DestOffset, (char *) pTexData + SrcOffset, TileRowSize);
			}
		}

		// free original data
		mem_free(pTexData);

		// create textures
		glGenTextures(1, &m_aTextures[pCommand->m_Slot].m_Tex2DArray);
		m_aTextures[pCommand->m_Slot].m_State |= CTexture::STATE_TEX2DARRAY;
		glBindTexture(GL_TEXTURE_2D_ARRAY, m_aTextures[pCommand->m_Slot].m_Tex2DArray);
		glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, StoreOglformat, TileWidth, TileHeight, Depth, 0, Oglformat, GL_UNSIGNED_BYTE, pTmpData);

		m_aTextures[pCommand->m_Slot].m_MemSize += TileWidth * TileHeight * Depth * pCommand->m_PixelSize;
		pTexData = pTmpData;
	}

	*m_pTextureMemoryUsage += m_aTextures[pCommand->m_Slot].m_MemSize;

	// free data
	mem_free(pTexData);
}

void CCommandProcessorFragment_OpenGL::Cmd_Clear(const CCommandBuffer::CClearCommand *pCommand)
{
	glClearColor(pCommand->m_Color.r, pCommand->m_Color.g, pCommand->m_Color.b, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void CCommandProcessorFragment_OpenGL::Cmd_Render(const CCommandBuffer::CRenderCommand *pCommand)
{
	if(!SetState(pCommand->m_State))
		return;

	glBufferData(GL_ARRAY_BUFFER, pCommand->m_PrimCount * 4 * sizeof(CCommandBuffer::CVertex), nullptr, GL_STREAM_DRAW);
	glBufferSubData(GL_ARRAY_BUFFER, 0, pCommand->m_PrimCount * 4 * sizeof(CCommandBuffer::CVertex), pCommand->m_pVertices);

	switch(pCommand->m_PrimType)
	{
		case CCommandBuffer::PRIMTYPE_QUADS:
			glDrawElements(GL_TRIANGLES, pCommand->m_PrimCount * 6, GL_UNSIGNED_INT, 0);
			break;
		case CCommandBuffer::PRIMTYPE_LINES:
			glDrawArrays(GL_LINES, 0, pCommand->m_PrimCount * 2);
			break;
		default:
			dbg_msg("render", "unknown primtype %d", pCommand->m_PrimType);
	};
}

void CCommandProcessorFragment_OpenGL::Cmd_Screenshot(const CCommandBuffer::CScreenshotCommand *pCommand)
{
	// fetch image data
	GLint aViewport[4] = {0, 0, 0, 0};
	glGetIntegerv(GL_VIEWPORT, aViewport);

	int w = pCommand->m_W == -1 ? aViewport[2] : pCommand->m_W;
	int h = pCommand->m_H == -1 ? aViewport[3] : pCommand->m_H;
	int x = pCommand->m_X;
	int y = aViewport[3] - pCommand->m_Y - 1 - (h - 1);

	// we allocate one more row to use when we are flipping the texture
	unsigned char *pPixelData = (unsigned char *) mem_alloc(w * (h + 1) * 3);
	unsigned char *pTempRow = pPixelData + w * h * 3;

	// fetch the pixels
	GLint Alignment;
	glGetIntegerv(GL_PACK_ALIGNMENT, &Alignment);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(x, y, w, h, GL_RGB, GL_UNSIGNED_BYTE, pPixelData);
	glPixelStorei(GL_PACK_ALIGNMENT, Alignment);

	// flip the pixel because opengl works from bottom left corner
	for(int ty = 0; ty < h / 2; ty++)
	{
		memmove(pTempRow, pPixelData + ty * w * 3, w * 3);
		memmove(pPixelData + ty * w * 3, pPixelData + (h - ty - 1) * w * 3, w * 3);
		memmove(pPixelData + (h - ty - 1) * w * 3, pTempRow, w * 3);
	}

	// fill in the information
	pCommand->m_pImage->m_Width = w;
	pCommand->m_pImage->m_Height = h;
	pCommand->m_pImage->m_Format = CImageInfo::FORMAT_RGB;
	pCommand->m_pImage->m_pData = pPixelData;
}

CCommandProcessorFragment_OpenGL::CCommandProcessorFragment_OpenGL()
{
	mem_zero(m_aTextures, sizeof(m_aTextures));
	m_pTextureMemoryUsage = 0;
	mem_zero(m_aRenderShader, sizeof(m_aRenderShader));
}

bool CCommandProcessorFragment_OpenGL::RunCommand(const CCommandBuffer::CCommand *pBaseCommand)
{
	switch(pBaseCommand->m_Cmd)
	{
		case CMD_INIT: Cmd_Init(static_cast<const CInitCommand *>(pBaseCommand)); break;
		case CMD_GL_SHUTDOWN: Cmd_Shutdown(static_cast<const CGLShutdownCommand *>(pBaseCommand)); break;
		case CCommandBuffer::CMD_TEXTURE_CREATE: Cmd_Texture_Create(static_cast<const CCommandBuffer::CTextureCreateCommand *>(pBaseCommand)); break;
		case CCommandBuffer::CMD_TEXTURE_DESTROY: Cmd_Texture_Destroy(static_cast<const CCommandBuffer::CTextureDestroyCommand *>(pBaseCommand)); break;
		case CCommandBuffer::CMD_TEXTURE_UPDATE: Cmd_Texture_Update(static_cast<const CCommandBuffer::CTextureUpdateCommand *>(pBaseCommand)); break;
		case CCommandBuffer::CMD_CLEAR: Cmd_Clear(static_cast<const CCommandBuffer::CClearCommand *>(pBaseCommand)); break;
		case CCommandBuffer::CMD_RENDER: Cmd_Render(static_cast<const CCommandBuffer::CRenderCommand *>(pBaseCommand)); break;
		case CCommandBuffer::CMD_SCREENSHOT: Cmd_Screenshot(static_cast<const CCommandBuffer::CScreenshotCommand *>(pBaseCommand)); break;
		default: return false;
	}

	return true;
}

// ------------ CCommandProcessorFragment_SDL

void CCommandProcessorFragment_SDL::Cmd_Init(const CInitCommand *pCommand)
{
	m_GLContext = pCommand->m_GLContext;
	m_pWindow = pCommand->m_pWindow;
	SDL_GL_MakeCurrent(m_pWindow, m_GLContext);
}

void CCommandProcessorFragment_SDL::Cmd_Shutdown(const CShutdownCommand *pCommand)
{
	SDL_GL_MakeCurrent(NULL, NULL);
}

void CCommandProcessorFragment_SDL::Cmd_Swap(const CCommandBuffer::CSwapCommand *pCommand)
{
	SDL_GL_SwapWindow(m_pWindow);

	if(pCommand->m_Finish)
		glFinish();
}

void CCommandProcessorFragment_SDL::Cmd_VSync(const CCommandBuffer::CVSyncCommand *pCommand)
{
	*pCommand->m_pRetOk = SDL_GL_SetSwapInterval(pCommand->m_VSync);
}

CCommandProcessorFragment_SDL::CCommandProcessorFragment_SDL()
{
}

bool CCommandProcessorFragment_SDL::RunCommand(const CCommandBuffer::CCommand *pBaseCommand)
{
	switch(pBaseCommand->m_Cmd)
	{
		case CCommandBuffer::CMD_SWAP: Cmd_Swap(static_cast<const CCommandBuffer::CSwapCommand *>(pBaseCommand)); break;
		case CCommandBuffer::CMD_VSYNC: Cmd_VSync(static_cast<const CCommandBuffer::CVSyncCommand *>(pBaseCommand)); break;
		case CMD_INIT: Cmd_Init(static_cast<const CInitCommand *>(pBaseCommand)); break;
		case CMD_SHUTDOWN: Cmd_Shutdown(static_cast<const CShutdownCommand *>(pBaseCommand)); break;
		default: return false;
	}

	return true;
}

// ------------ CCommandProcessor_SDL_OpenGL

void CCommandProcessor_SDL_OpenGL::RunBuffer(CCommandBuffer *pBuffer)
{
	for(CCommandBuffer::CCommand *pCommand = pBuffer->Head(); pCommand; pCommand = pCommand->m_pNext)
	{
		if(m_OpenGL.RunCommand(pCommand))
			continue;

		if(m_SDL.RunCommand(pCommand))
			continue;

		if(m_General.RunCommand(pCommand))
			continue;

		dbg_msg("graphics", "unknown command %d", pCommand->m_Cmd);
	}
}

// ------------ CGraphicsBackend_SDL_OpenGL
int CGraphicsBackend_SDL_OpenGL::Init(const char *pName, int *pScreen, int *pWindowWidth, int *pWindowHeight, int *pScreenWidth, int *pScreenHeight, int FsaaSamples, int Flags, int *pDesktopWidth, int *pDesktopHeight)
{
	if(!SDL_WasInit(SDL_INIT_VIDEO))
	{
		if(!SDL_InitSubSystem(SDL_INIT_VIDEO))
		{
			dbg_msg("gfx", "unable to init SDL video: %s", SDL_GetError());
			return -1;
		}
	}

	// set screen
	SDL_Rect ScreenPos;
	SDL_DisplayID *pDisplayIds = SDL_GetDisplays(&m_NumScreens);
	if(m_NumScreens > 0)
	{
		*pScreen = pDisplayIds[clamp(*pScreen, 0, m_NumScreens - 1)];
		if(!SDL_GetDisplayBounds(*pScreen, &ScreenPos))
		{
			dbg_msg("gfx", "unable to retrieve screen information: %s", SDL_GetError());
			return -1;
		}
	}
	else
	{
		dbg_msg("gfx", "unable to retrieve number of screens: %s", SDL_GetError());
		return -1;
	}

	// store desktop resolution for settings reset button
	if(!GetDesktopResolution(*pScreen, pDesktopWidth, pDesktopHeight))
	{
		dbg_msg("gfx", "unable to get desktop resolution: %s", SDL_GetError());
		return -1;
	}

	// use desktop resolution as default resolution
	if(*pWindowWidth == 0 || *pWindowHeight == 0)
	{
		*pWindowWidth = *pDesktopWidth;
		*pWindowHeight = *pDesktopHeight;
	}

	// set flags
	int SdlFlags = SDL_WINDOW_OPENGL;
	if(Flags & IGraphicsBackend::INITFLAG_HIGHDPI)
		SdlFlags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
	if(Flags & IGraphicsBackend::INITFLAG_RESIZABLE)
		SdlFlags |= SDL_WINDOW_RESIZABLE;
	if(Flags & IGraphicsBackend::INITFLAG_BORDERLESS)
		SdlFlags |= SDL_WINDOW_BORDERLESS;
	if(Flags & IGraphicsBackend::INITFLAG_FULLSCREEN)
		SdlFlags |= SDL_WINDOW_FULLSCREEN;

	if(Flags & IGraphicsBackend::INITFLAG_X11XRANDR)
		SDL_SetHint(SDL_HINT_VIDEO_X11_XRANDR, "1");

	// set gl attributes for OpenGL 3.3 Core Profile
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	if(Flags & IGraphicsBackend::INITFLAG_OPENGLES)
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	}
	else
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	}

	if(FsaaSamples)
	{
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, FsaaSamples);
	}
	else
	{
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
	}

	// calculate centered position in windowed mode
	int OffsetX = 0;
	int OffsetY = 0;
	if(!(Flags & IGraphicsBackend::INITFLAG_FULLSCREEN) && *pDesktopWidth > *pWindowWidth && *pDesktopHeight > *pWindowHeight)
	{
		OffsetX = (*pDesktopWidth - *pWindowWidth) / 2;
		OffsetY = (*pDesktopHeight - *pWindowHeight) / 2;
	}

	// create window
	m_pWindow = SDL_CreateWindow(pName, *pWindowWidth, *pWindowHeight, SdlFlags);
	if(m_pWindow == NULL)
	{
		dbg_msg("gfx", "unable to create window: %s", SDL_GetError());
		return -1;
	}
	SDL_SetWindowPosition(m_pWindow, ScreenPos.x + OffsetX, ScreenPos.y + OffsetY);
	SDL_GetWindowSize(m_pWindow, pWindowWidth, pWindowHeight);

	// create gl context
	m_GLContext = SDL_GL_CreateContext(m_pWindow);
	if(m_GLContext == NULL)
	{
		dbg_msg("gfx", "unable to create OpenGL context: %s", SDL_GetError());
		return -1;
	}

	if(Flags & IGraphicsBackend::INITFLAG_OPENGLES)
	{
		if(!gladLoadGLES2((GLADloadfunc) SDL_GL_GetProcAddress))
		{
			dbg_msg("gfx", "failed to initialize GLAD");
			return -1;
		}
		dbg_msg("gfx", "using %s", glGetString(GL_VERSION));
	}
	else
	{
		if(!gladLoadGL((GLADloadfunc) SDL_GL_GetProcAddress))
		{
			dbg_msg("gfx", "failed to initialize GLAD");
			return -1;
		}
		dbg_msg("gfx", "using OpenGL %s", glGetString(GL_VERSION));
	}
	SDL_GetWindowSizeInPixels(m_pWindow, pScreenWidth, pScreenHeight); // drawable size may differ in high dpi mode

	SDL_GL_SetSwapInterval(Flags & IGraphicsBackend::INITFLAG_VSYNC ? 1 : 0);

	SDL_GL_MakeCurrent(NULL, NULL);

	// print sdl version
	int Version = SDL_GetVersion();
	dbg_msg("gfx", "SDL version %d.%d.%d", SDL_VERSIONNUM_MAJOR(Version), SDL_VERSIONNUM_MINOR(Version), SDL_VERSIONNUM_MICRO(Version));

	// start the command processor
	m_pProcessor = new CCommandProcessor_SDL_OpenGL;
	StartProcessor(m_pProcessor);

	// issue init commands for OpenGL and SDL
	CCommandBuffer CmdBuffer(1024, 512);
	CCommandProcessorFragment_SDL::CInitCommand CmdSDL;
	CmdSDL.m_pWindow = m_pWindow;
	CmdSDL.m_GLContext = m_GLContext;
	CmdBuffer.AddCommand(CmdSDL);
	CCommandProcessorFragment_OpenGL::CInitCommand CmdOpenGL;
	CmdOpenGL.m_pTextureMemoryUsage = &m_TextureMemoryUsage;
	CmdOpenGL.m_IsOpenGLES = Flags & IGraphicsBackend::INITFLAG_OPENGLES;
	CmdBuffer.AddCommand(CmdOpenGL);
	RunBuffer(&CmdBuffer);
	WaitForIdle();

	return 0;
}

int CGraphicsBackend_SDL_OpenGL::Shutdown()
{
	// issue a shutdown command
	{
		CCommandBuffer CmdBuffer(1024, 512);
		CCommandProcessorFragment_SDL::CShutdownCommand Cmd;
		CmdBuffer.AddCommand(Cmd);
		RunBuffer(&CmdBuffer);
		WaitForIdle();
	}
	{
		CCommandBuffer CmdBuffer(1024, 512);
		CCommandProcessorFragment_OpenGL::CGLShutdownCommand Cmd;
		CmdBuffer.AddCommand(Cmd);
		RunBuffer(&CmdBuffer);
		WaitForIdle();
	}
	// stop and delete the processor
	StopProcessor();
	delete m_pProcessor;
	m_pProcessor = 0;

	SDL_GL_DestroyContext(m_GLContext);
	SDL_DestroyWindow(m_pWindow);
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	return 0;
}

int CGraphicsBackend_SDL_OpenGL::MemoryUsage() const
{
	return m_TextureMemoryUsage;
}

void CGraphicsBackend_SDL_OpenGL::Minimize()
{
	SDL_MinimizeWindow(m_pWindow);
}

void CGraphicsBackend_SDL_OpenGL::Maximize()
{
	SDL_MaximizeWindow(m_pWindow);
}

bool CGraphicsBackend_SDL_OpenGL::Fullscreen(bool State)
{
	return SDL_SetWindowFullscreen(m_pWindow, State);
}

void CGraphicsBackend_SDL_OpenGL::SetWindowBordered(bool State)
{
	SDL_SetWindowBordered(m_pWindow, State);
}

bool CGraphicsBackend_SDL_OpenGL::SetWindowScreen(int Index)
{
	if(Index >= 0 && Index < m_NumScreens)
	{
		SDL_Rect ScreenPos;
		if(SDL_GetDisplayBounds(Index, &ScreenPos) == 0)
		{
			SDL_SetWindowPosition(m_pWindow, ScreenPos.x, ScreenPos.y);
			return true;
		}
	}

	return false;
}

int CGraphicsBackend_SDL_OpenGL::GetWindowScreen()
{
	return SDL_GetDisplayForWindow(m_pWindow);
}

int CGraphicsBackend_SDL_OpenGL::GetVideoModes(CVideoMode *pModes, int MaxModes, int Screen)
{
	SDL_DisplayMode **ppModes;
	int NumModes;
	ppModes = SDL_GetFullscreenDisplayModes(GetWindowScreen(), &NumModes);
	if(NumModes < 0 || !ppModes)
	{
		dbg_msg("gfx", "unable to get the number of display modes: %s", SDL_GetError());
		return 0;
	}

	int ModesCount = 0;
	for(int i = 0; i < NumModes && ModesCount < MaxModes; i++)
	{
		if(!ppModes[i])
		{
			dbg_msg("gfx", "unable to get display mode: %s", SDL_GetError());
			continue;
		}

		bool AlreadyFound = false;
		for(int j = 0; j < ModesCount; j++)
		{
			if(pModes[j].m_Width == ppModes[i]->w && pModes[j].m_Height == ppModes[i]->h)
			{
				AlreadyFound = true;
				break;
			}
		}
		if(AlreadyFound)
			continue;

		pModes[ModesCount].m_Width = ppModes[i]->w;
		pModes[ModesCount].m_Height = ppModes[i]->h;
		ModesCount++;
	}
	return ModesCount;
}

bool CGraphicsBackend_SDL_OpenGL::GetDesktopResolution(int Index, int *pDesktopWidth, int *pDesktopHeight)
{
	const SDL_DisplayMode *pDisplayMode = SDL_GetDesktopDisplayMode(Index);
	if(!pDisplayMode)
		return false;

	*pDesktopWidth = pDisplayMode->w;
	*pDesktopHeight = pDisplayMode->h;
	return true;
}

bool CGraphicsBackend_SDL_OpenGL::WindowActive()
{
	return SDL_GetWindowFlags(m_pWindow) & SDL_WINDOW_INPUT_FOCUS;
}

bool CGraphicsBackend_SDL_OpenGL::WindowOpen()
{
	return !(SDL_GetWindowFlags(m_pWindow) & SDL_WINDOW_HIDDEN);
}

void *CGraphicsBackend_SDL_OpenGL::GetWindowHandle()
{
	return m_pWindow;
}

IGraphicsBackend *CreateGraphicsBackend() { return new CGraphicsBackend_SDL_OpenGL; }
