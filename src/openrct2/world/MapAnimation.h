/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "Location.hpp"

#include <cstdint>
#include <utility>
#include <vector>

struct CoordsXYZ;
struct TileCoordsXY;

namespace OpenRCT2::MapAnimations
{
    enum class TemporaryType : uint8_t
    {
        onRidePhoto,
        landEdgeDoor,
    };

    struct UpdateState
    {
        std::vector<TileCoordsXY> tiles;
        std::vector<std::pair<CoordsXYZ, TemporaryType>> temporary;
    };

    UpdateState CaptureUpdateState();
    void RestoreUpdateState(const UpdateState& state);

    void MarkTileForInvalidation(TileCoordsXY coords);
    void MarkTileForUpdate(TileCoordsXY coords);
    void CreateTemporary(const CoordsXYZ& coords, TemporaryType type);
    void MarkAllTiles();
    void InvalidateAndUpdateAll();
    void ClearAll();
    void ShiftAll(TileCoordsXY amount);
} // namespace OpenRCT2::MapAnimations
