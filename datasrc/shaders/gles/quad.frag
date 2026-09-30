#version 300 es
precision highp float;
precision highp int;

out vec4 FragColor;

in vec3 TexCoord;
in vec4 Color;

uniform highp sampler2DArray ourTexture;
uniform bool useTexture;
uniform bool IsAlphaOnly;
uniform bool IsStainedOnly;
uniform bool IsSDF;
uniform float SDFGain;
uniform float SDFOutlineOffset;
uniform vec4 SDFOutlineColor;

// FreeType encodes the signed distance as 128 * (d / spread + 1).
const float SDFEdge = 128.0 / 255.0;

float SDFCoverage(float Value)
{
    return clamp(0.5 + (Value - SDFEdge) * SDFGain, 0.0, 1.0);
}

vec4 SDFTextColor()
{
    float Distance = texture(ourTexture, TexCoord.xyz).r;
    float Glyph = SDFCoverage(Distance);

    // moving the edge towards lower values grows the glyph outwards
    float Outline = SDFCoverage(Distance + SDFOutlineOffset);

    // the outline fades with the text alpha
    float OutlineAlpha = SDFOutlineColor.a * Color.a * Outline;
    vec3 Rgb = SDFOutlineColor.rgb * OutlineAlpha;
    float Alpha = OutlineAlpha;

    float GlyphAlpha = Color.a * Glyph;
    Rgb = Color.rgb * GlyphAlpha + Rgb * (1.0 - GlyphAlpha);
    Alpha = GlyphAlpha + Alpha * (1.0 - GlyphAlpha);

    // premultiplied output, matching the other alpha paths
    return vec4(Rgb, Alpha);
}

void main()
{
    if(useTexture)
    {
        if(IsSDF)
        {
            FragColor = SDFTextColor();
        }
        else if(IsAlphaOnly)
        {
            vec4 texColor = texture(ourTexture, TexCoord.xyz);
            FragColor = vec4(Color.rgb * texColor.r * Color.a, texColor.r * Color.a);
        }
        else if(IsStainedOnly)
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
