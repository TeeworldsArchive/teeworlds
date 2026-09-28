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

void main()
{
    if(useTexture)
    {
        vec4 texColor = texture(ourTexture, TexCoord.xyz);
        if(IsAlphaOnly)
        {
            FragColor = vec4(Color.rgb * texColor.r * Color.a, texColor.r * Color.a);
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
