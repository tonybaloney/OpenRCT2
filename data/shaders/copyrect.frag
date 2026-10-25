#version 330 core

in vec2 fTextureCoordinate;
out uint fragColor;

uniform usampler2D uTexture;

void main()
{
    fragColor = texture(uTexture, fTextureCoordinate).r;
}
