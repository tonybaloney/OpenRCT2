/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "GLSLTypes.h"
#include "OpenGLShaderProgram.h"

#include <openrct2/drawing/PaletteType.h>
#include <span>

namespace OpenRCT2::Ui
{
    class ApplyPaletteShader final : public OpenGLShaderProgram
    {
    private:
        GLint uTexture{ -1 };
        GLint uPalette{ -1 };
        GLint uLightTexture{ -1 };
        GLint uLightPalette{ -1 };
        GLint uLightingEnabled{ -1 };
        GLint uWeatherTexture{ -1 };
        GLint uWeather{ -1 };

        GLint vPosition{ -1 };
        GLint vTextureCoordinate{ -1 };

        GLuint _vbo{};
        GLuint _vao{};
        GLuint _lightTexture{};
        GLuint _lightPaletteTexture{};
        uint32_t _lightWidth{};
        uint32_t _lightHeight{};
        GLuint _weatherTexture{};
        uint32_t _weatherWidth{};
        uint32_t _weatherHeight{};

    public:
        ApplyPaletteShader();
        ~ApplyPaletteShader() override;

        static void SetTexture(GLuint texture);
        void SetPalette(const vec4* glPalette);
        void SetLighting(
            std::span<const uint8_t> lightMap, uint32_t width, uint32_t height, const Drawing::GamePalette& palette);
        void DisableLighting();
        void SetWeather(
            std::span<const uint8_t> mask, uint32_t width, uint32_t height, float mist, float rain, float time, bool snow);

        void Draw();

    private:
        void GetLocations();
    };
} // namespace OpenRCT2::Ui
