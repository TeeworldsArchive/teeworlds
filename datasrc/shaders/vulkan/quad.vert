#version 450

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aTexCoord;
layout (location = 2) in vec4 aColor;

layout (location = 0) out vec3 TexCoord;
layout (location = 1) out vec4 Color;

// SDL_GPU vertex uniform slot 0 (SPIR-V set 1)
layout (set = 1, binding = 0) uniform UBO {
    mat4 projection;
} ubo;

void main()
{
    gl_Position = ubo.projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
    Color = aColor;
}
