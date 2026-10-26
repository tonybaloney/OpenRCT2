/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <openrct2/SpriteIds.h>
#include <openrct2/drawing/ImageId.hpp>
#include <openrct2/interface/ZoomLevel.h>

namespace OpenRCT2::Ui
{
    inline bool ShouldSmoothSprite(bool enabled, ZoomLevel zoom, ImageId image)
    {
        const auto index = image.GetIndex();
        const bool scrollingText = (index >= SPR_SCROLLING_TEXT_START && index < SPR_SCROLLING_TEXT_END)
            || (index >= SPR_SCROLLING_TEXT_LEGACY_START && index <= SPR_SCROLLING_TEXT_DEFAULT);
        return enabled && zoom < ZoomLevel{ 0 } && !scrollingText;
    }
} // namespace OpenRCT2::Ui
