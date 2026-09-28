#version 450

layout (location = 0) in vec3 TexCoord;
layout (location = 1) in vec4 Color;

layout (location = 0) out vec4 FragColor;

// SDL_GPU fragment sampler slot 0 (SPIR-V set 2)
layout (set = 2, binding = 0) uniform sampler2DArray ourTexture;

// SDL_GPU fragment uniform slot 0 (SPIR-V set 3)
layout (set = 3, binding = 0) uniform UBO {
    int useTexture;
    int IsAlphaOnly;
    int IsStainedOnly;
} ubo;

void main()
{
    if(ubo.useTexture != 0)
    {
        vec4 texColor = texture(ourTexture, TexCoord.xyz);
        if(ubo.IsAlphaOnly != 0)
        {
            FragColor = vec4(Color.rgb * texColor.r * Color.a, texColor.r * Color.a);
        }
        else if(ubo.IsStainedOnly != 0)
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
