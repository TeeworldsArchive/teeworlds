#version 450

layout (location = 0) in vec3 TexCoord;
layout (location = 1) in vec4 Color;

layout (location = 0) out vec4 FragColor;

// SDL_GPU fragment sampler slot 0 (SPIR-V set 2)
layout (set = 2, binding = 0) uniform sampler2DArray ourTexture;

// SDL_GPU fragment uniform slot 0 (SPIR-V set 3)
// Layout must match CQuadFragmentUniforms in backend_sdlgpu.h.
layout (set = 3, binding = 0) uniform UBO {
    int useTexture;
    int IsAlphaOnly;
    int IsStainedOnly;
    int IsSDF;
    float SDFGain;
    float SDFOutlineOffset;
    // std140 pads this vec4 to 16 bytes, matching the C++ mirror
    vec4 SDFOutlineColor;
} ubo;

// FreeType encodes the signed distance as 128 * (d / spread + 1).
const float SDFEdge = 128.0 / 255.0;

float SDFCoverage(float Value)
{
    return clamp(0.5 + (Value - SDFEdge) * ubo.SDFGain, 0.0, 1.0);
}

vec4 SDFTextColor()
{
    float Distance = texture(ourTexture, TexCoord.xyz).r;
    float Glyph = SDFCoverage(Distance);

    // moving the edge towards lower values grows the glyph outwards
    float Outline = SDFCoverage(Distance + ubo.SDFOutlineOffset);

    // the outline fades with the text alpha
    float OutlineAlpha = ubo.SDFOutlineColor.a * Color.a * Outline;
    vec3 Rgb = ubo.SDFOutlineColor.rgb * OutlineAlpha;
    float Alpha = OutlineAlpha;

    float GlyphAlpha = Color.a * Glyph;
    Rgb = Color.rgb * GlyphAlpha + Rgb * (1.0 - GlyphAlpha);
    Alpha = GlyphAlpha + Alpha * (1.0 - GlyphAlpha);

    // premultiplied output, matching the other alpha paths
    return vec4(Rgb, Alpha);
}

void main()
{
    if(ubo.useTexture != 0)
    {
        if(ubo.IsSDF != 0)
        {
            FragColor = SDFTextColor();
        }
        else if(ubo.IsAlphaOnly != 0)
        {
            vec4 texColor = texture(ourTexture, TexCoord.xyz);
            FragColor = vec4(Color.rgb * texColor.r * Color.a, texColor.r * Color.a);
        }
        else if(ubo.IsStainedOnly != 0)
        {
            vec4 texColor = texture(ourTexture, TexCoord.xyz);
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
            FragColor = texture(ourTexture, TexCoord.xyz) * Color;
        }
    }
    else
    {
        // No texture, use vertex color directly
        FragColor = Color;
    }
}
