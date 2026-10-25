/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <openrct2/drawing/FilterPaletteIds.h>
#include <openrct2/drawing/ImageId.hpp>
#include <openrct2/paint/tile_element/Paint.Surface.h>

namespace OpenRCT2::Ui
{
    inline bool IsWaterSurfaceImage(const ImageId image)
    {
        const auto index = image.GetIndex();
        return (index >= SPR_WATER_MASK && index < SPR_WATER_OVERLAY && image.IsBlended() && image.IsRemap()
                && image.GetRemap() == EnumValue(Drawing::FilterPaletteID::paletteWater))
            || (!image.HasPrimary() && !image.HasSecondary() && !image.IsBlended()
                && ((index >= SPR_WATER_OVERLAY && index < SPR_WATER_OVERLAY + 5)
                    || (index >= SPR_G2_OPAQUE_WATER_OVERLAY && index < SPR_G2_OPAQUE_WATER_OVERLAY + 5)));
    }
} // namespace OpenRCT2::Ui
