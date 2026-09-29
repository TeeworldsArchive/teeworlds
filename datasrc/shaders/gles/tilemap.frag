#version 300 es
precision highp float;
precision highp int;
precision highp sampler2DArray;

out vec4 FragColor;

// xy is the layer space position in tile units, z is unused
in vec3 TexCoord;
// premultiplied layer color
in vec4 Color;

uniform sampler2DArray ourTexture; // tileset, texture unit 0
uniform sampler2DArray tileData;   // tile indices/flags, texture unit 1
uniform vec2 MapSize;
uniform int PassMode;  // 0 = opaque tiles, 1 = transparent tiles, 2 = all tiles
uniform int LayerIndex; // array layer inside tileData
uniform bool ColorOpaque; // whether the layer color is fully opaque

void main()
{
    // clamping repeats the layer edge, matching TILERENDERFLAG_EXTEND
    ivec2 Tile = clamp(ivec2(floor(TexCoord.xy)), ivec2(0), ivec2(MapSize) - 1);
    vec4 Data = texelFetch(tileData, ivec3(Tile, LayerIndex), 0);
    if(PassMode == 3) // TILEMAP_PASS_DATA_DEBUG
    {
        FragColor = vec4(Data.r, Data.g, 0.0, 1.0);
        return;
    }
    // round() avoids the .5 tie of uint(x + 0.5), which some drivers round up to even
    uint Index = uint(round(Data.r * 255.0));
    if(Index == 0u)
        discard;

    uint Flags = uint(round(Data.g * 255.0));
    // an opaque tile only takes the unblended pass when the layer color is opaque too
    bool Opaque = (Flags & 4u) != 0u && ColorOpaque;
    if(PassMode == 0 && !Opaque)
        discard;
    if(PassMode == 1 && Opaque)
        discard;

    vec2 UV = fract(TexCoord.xy);
    // same flag order as the CPU path: rotate first, then flip
    if((Flags & 8u) != 0u) UV = vec2(UV.y, 1.0 - UV.x); // TILEFLAG_ROTATE
    if((Flags & 1u) != 0u) UV.x = 1.0 - UV.x;          // TILEFLAG_VFLIP
    if((Flags & 2u) != 0u) UV.y = 1.0 - UV.y;          // TILEFLAG_HFLIP

    FragColor = texture(ourTexture, vec3(UV, float(Index))) * Color;
}
