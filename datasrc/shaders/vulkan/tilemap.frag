#version 450

// xy is the layer space position in tile units, z is unused
layout (location = 0) in vec3 TexCoord;
// premultiplied layer color
layout (location = 1) in vec4 Color;

layout (location = 0) out vec4 FragColor;

// SDL_GPU fragment sampler slots 0 and 1 (SPIR-V set 2)
layout (set = 2, binding = 0) uniform sampler2DArray ourTexture;
layout (set = 2, binding = 1) uniform sampler2DArray tileData;

// SDL_GPU fragment uniform slot 0 (SPIR-V set 3)
layout (set = 3, binding = 0) uniform UBO {
    vec4 MapSizeTileSize; // xy = map size, z = tile size, w unused
    ivec4 Params;         // x = pass mode, y = array layer, z = layer color is opaque
} ubo;

void main()
{
    ivec2 Tile = clamp(ivec2(floor(TexCoord.xy)), ivec2(0), ivec2(ubo.MapSizeTileSize.xy) - 1);
    vec4 Data = texelFetch(tileData, ivec3(Tile, ubo.Params.y), 0);
    if(ubo.Params.x == 3) // TILEMAP_PASS_DATA_DEBUG
    {
        FragColor = vec4(Data.r, Data.g, 0.0, 1.0);
        return;
    }
    // round() avoids the .5 tie of uint(x + 0.5); drivers that round ties to
    // even would otherwise bump every odd index up by one
    uint Index = uint(round(Data.r * 255.0));
    if(Index == 0u)
        discard;

    uint Flags = uint(round(Data.g * 255.0));
    // an opaque tile only takes the unblended pass when the layer color is opaque too
    bool ColorOpaque = ubo.Params.z != 0;
    bool Opaque = (Flags & 4u) != 0u && ColorOpaque;
    if(ubo.Params.x == 0 && !Opaque)
        discard;
    if(ubo.Params.x == 1 && Opaque)
        discard;

    vec2 UV = fract(TexCoord.xy);
    // same flag order as the CPU path: rotate first, then flip
    if((Flags & 8u) != 0u) UV = vec2(UV.y, 1.0 - UV.x); // TILEFLAG_ROTATE
    if((Flags & 1u) != 0u) UV.x = 1.0 - UV.x;          // TILEFLAG_VFLIP
    if((Flags & 2u) != 0u) UV.y = 1.0 - UV.y;          // TILEFLAG_HFLIP

    FragColor = texture(ourTexture, vec3(UV, float(Index))) * Color;
}
