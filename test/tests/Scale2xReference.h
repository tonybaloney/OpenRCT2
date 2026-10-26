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
#include <cstdint>
#include <span>

namespace OpenRCT2::Test
{
    // Independent CPU oracle for the indexed drawrect shader; coordinates are in the 2x output.
    inline uint8_t Scale2xReference(std::span<const uint8_t> source, int width, int height, int x, int y)
    {
        const auto sample = [&](int sx, int sy) {
            return source[std::clamp(sy, 0, height - 1) * width + std::clamp(sx, 0, width - 1)];
        };
        const int sx = x / 2;
        const int sy = y / 2;
        const auto b = sample(sx, sy - 1);
        const auto d = sample(sx - 1, sy);
        const auto e = sample(sx, sy);
        const auto f = sample(sx + 1, sy);
        const auto h = sample(sx, sy + 1);
        if (b == h || d == f)
            return e;
        if (y % 2 == 0)
            return x % 2 == 0 ? (d == b ? d : e) : (b == f ? f : e);
        return x % 2 == 0 ? (d == h ? d : e) : (h == f ? f : e);
    }
} // namespace OpenRCT2::Test
