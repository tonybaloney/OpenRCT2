/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <algorithm>
#include <openrct2/world/Weather.h>
#include <span>
#include <vector>

namespace OpenRCT2::Ui
{
    class WeatherAtmosphere
    {
        std::vector<uint8_t> _mask;
        int32_t _width{};
        int32_t _height{};
        float _mist{};
        float _rain{};
        bool _snow{};

    public:
        void BeginFrame(int32_t width, int32_t height, Weather::Level level, bool enabled, bool snow)
        {
            _width = std::max(0, width);
            _height = std::max(0, height);
            _mist = _rain = 0;
            _snow = snow;
            if (!enabled || level == Weather::Level::none || _width == 0 || _height == 0)
                return;

            const bool heavy = level == Weather::Level::heavy;
            _mist = snow ? (heavy ? 0.18f : 0.10f) : (heavy ? 0.14f : 0.07f);
            _rain = snow ? 0 : (heavy ? 0.40f : 0.25f);
            _mask.resize(static_cast<size_t>(_width) * _height);
            std::fill(_mask.begin(), _mask.end(), 0);
        }

        void AddRegion(int32_t x, int32_t y, int32_t width, int32_t height)
        {
            if (_mist == 0)
                return;

            const auto left = std::clamp(x, 0, _width);
            const auto top = std::clamp(y, 0, _height);
            const auto right = std::clamp(x + width, left, _width);
            const auto bottom = std::clamp(y + height, top, _height);
            for (auto row = top; row < bottom; row++)
            {
                auto start = _mask.begin() + static_cast<size_t>(row) * _width;
                std::fill(start + left, start + right, 255);
            }
        }

        std::span<const uint8_t> GetMask() const
        {
            return _mist == 0 ? std::span<const uint8_t>{} : _mask;
        }

        float GetMist() const
        {
            return _mist;
        }
        float GetRain() const
        {
            return _rain;
        }
        bool IsSnow() const
        {
            return _snow;
        }
    };
} // namespace OpenRCT2::Ui
