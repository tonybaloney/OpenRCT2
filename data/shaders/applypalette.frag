#version 330 core

uniform vec4 uPalette[256];
uniform usampler2D uTexture;
uniform usampler2D uLightTexture;
uniform usampler2D uLightPalette;
uniform bool uLightingEnabled;

in vec2 fTextureCoordinate;

out vec4 oColour;

void main()
{
    uint index = texture(uTexture, fTextureCoordinate).r;
    vec4 colour = uPalette[index];
    if (uLightingEnabled)
    {
        // Light-map rows start at the top of the screen, unlike framebuffer textures.
        uint intensity = texture(uLightTexture, vec2(fTextureCoordinate.x, 1.0 - fTextureCoordinate.y)).r;
        uvec3 light = texelFetch(uLightPalette, ivec2(int(index), 0), 0).rgb;
        uvec3 base = uvec3(round(colour.rgb * 255.0));
        uvec3 exposure = (light * intensity * 6u) >> 8u;
        uvec3 lit = base + ((uvec3(255u) - base) * exposure) / (uvec3(255u) + exposure);
        colour.rgb = vec3(lit) / 255.0;
    }
    oColour = colour;
}
