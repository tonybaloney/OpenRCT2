/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifndef DISABLE_OPENGL

    #include "ApplyPaletteShader.h"

using namespace OpenRCT2::Ui;

namespace
{
    struct VDStruct
    {
        GLfloat position[2];
        GLfloat texturecoordinate[2];
    };
} // namespace

constexpr VDStruct kVertexData[4] = {
    { -1.0f, -1.0f, 0.0f, 0.0f },
    { 1.0f, -1.0f, 1.0f, 0.0f },
    { -1.0f, 1.0f, 0.0f, 1.0f },
    { 1.0f, 1.0f, 1.0f, 1.0f },
};

ApplyPaletteShader::ApplyPaletteShader()
    : OpenGLShaderProgram("applypalette")
{
    GetLocations();

    glCall(glGenBuffers, 1, &_vbo);
    glCall(glGenVertexArrays, 1, &_vao);

    glCall(glBindBuffer, GL_ARRAY_BUFFER, _vbo);
    glCall(glBufferData, GL_ARRAY_BUFFER, sizeof(kVertexData), kVertexData, GL_STATIC_DRAW);

    glCall(glBindVertexArray, _vao);
    glCall(
        glVertexAttribPointer, vPosition, 2, GL_FLOAT, GL_FALSE, glSizeOf<VDStruct>(),
        reinterpret_cast<void*>(offsetof(VDStruct, position)));
    glCall(
        glVertexAttribPointer, vTextureCoordinate, 2, GL_FLOAT, GL_FALSE, glSizeOf<VDStruct>(),
        reinterpret_cast<void*>(offsetof(VDStruct, texturecoordinate)));

    glCall(glEnableVertexAttribArray, vPosition);
    glCall(glEnableVertexAttribArray, vTextureCoordinate);

    Use();
    glCall(glUniform1i, uTexture, 0);
    glCall(glUniform1i, uLightTexture, 1);
    glCall(glUniform1i, uLightPalette, 2);
    DisableLighting();

    glCall(glGenTextures, 1, &_lightTexture);
    glCall(glGenTextures, 1, &_lightPaletteTexture);
    for (auto texture : { _lightTexture, _lightPaletteTexture })
    {
        OpenGLAPI::SetTexture(1, GL_TEXTURE_2D, texture);
        glCall(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glCall(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glCall(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glCall(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    OpenGLAPI::SetTexture(2, GL_TEXTURE_2D, _lightPaletteTexture);
    glCall(glTexImage2D, GL_TEXTURE_2D, 0, GL_RGBA8UI, 256, 1, 0, GL_BGRA_INTEGER, GL_UNSIGNED_BYTE, nullptr);
}

ApplyPaletteShader::~ApplyPaletteShader()
{
    glCall(glDeleteBuffers, 1, &_vbo);
    glCall(glDeleteVertexArrays, 1, &_vao);
    glCall(glDeleteTextures, 1, &_lightTexture);
    glCall(glDeleteTextures, 1, &_lightPaletteTexture);
}

void ApplyPaletteShader::GetLocations()
{
    uTexture = GetUniformLocation("uTexture");
    uPalette = GetUniformLocation("uPalette");
    uLightTexture = GetUniformLocation("uLightTexture");
    uLightPalette = GetUniformLocation("uLightPalette");
    uLightingEnabled = GetUniformLocation("uLightingEnabled");

    vPosition = GetAttributeLocation("vPosition");
    vTextureCoordinate = GetAttributeLocation("vTextureCoordinate");
}

void ApplyPaletteShader::SetTexture(GLuint texture)
{
    OpenGLAPI::SetTexture(0, GL_TEXTURE_2D, texture);
}

void ApplyPaletteShader::SetPalette(const vec4* glPalette)
{
    glCall(glUniform4fv, uPalette, 256, reinterpret_cast<const GLfloat*>(glPalette));
}

void ApplyPaletteShader::SetLighting(
    std::span<const uint8_t> lightMap, uint32_t width, uint32_t height, const Drawing::GamePalette& palette)
{
    OpenGLAPI::SetTexture(1, GL_TEXTURE_2D, _lightTexture);
    glCall(glPixelStorei, GL_UNPACK_ALIGNMENT, 1);
    if (_lightWidth != width || _lightHeight != height)
    {
        glCall(glTexImage2D, GL_TEXTURE_2D, 0, GL_R8UI, width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, lightMap.data());
        _lightWidth = width;
        _lightHeight = height;
    }
    else
    {
        glCall(glTexSubImage2D, GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED_INTEGER, GL_UNSIGNED_BYTE, lightMap.data());
    }

    OpenGLAPI::SetTexture(2, GL_TEXTURE_2D, _lightPaletteTexture);
    glCall(glTexSubImage2D, GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_BGRA_INTEGER, GL_UNSIGNED_BYTE, palette.data());
    glCall(glUniform1i, uLightingEnabled, GL_TRUE);
}

void ApplyPaletteShader::DisableLighting()
{
    glCall(glUniform1i, uLightingEnabled, GL_FALSE);
}

void ApplyPaletteShader::Draw()
{
    glCall(glBindVertexArray, _vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

#endif /* DISABLE_OPENGL */
