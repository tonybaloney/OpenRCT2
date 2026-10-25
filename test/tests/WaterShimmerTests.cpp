/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>
#include <openrct2-ui/drawing/engines/opengl/WaterShimmer.h>
#include <openrct2/config/Config.h>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;
using namespace OpenRCT2::Ui;

TEST(WaterShimmerTests, OnlyActualSurfaceSpritesAreTagged)
{
    for (uint32_t slope = 0; slope < 5; slope++)
    {
        EXPECT_TRUE(IsWaterSurfaceImage(ImageId(SPR_WATER_MASK + slope, FilterPaletteID::paletteWater).WithBlended(true)));
        EXPECT_TRUE(IsWaterSurfaceImage(ImageId(SPR_WATER_OVERLAY + slope)));
        EXPECT_TRUE(IsWaterSurfaceImage(ImageId(SPR_G2_OPAQUE_WATER_OVERLAY + slope)));
    }
    EXPECT_FALSE(IsWaterSurfaceImage(ImageId(SPR_WATER_MASK - 1, FilterPaletteID::paletteWater).WithBlended(true)));
    EXPECT_FALSE(IsWaterSurfaceImage(ImageId(SPR_WATER_OVERLAY + 5)));
    EXPECT_FALSE(IsWaterSurfaceImage(ImageId(SPR_G2_OPAQUE_WATER_OVERLAY + 5)));
}

TEST(WaterShimmerTests, PaletteMatchingAndRemappedSpritesDoNotCountAsWater)
{
    EXPECT_FALSE(IsWaterSurfaceImage(ImageId(SPR_TERRAIN_GRASS, FilterPaletteID::paletteWater).WithBlended(true)));
    EXPECT_FALSE(IsWaterSurfaceImage(ImageId(SPR_WATER_MASK)));
    EXPECT_FALSE(IsWaterSurfaceImage(ImageId(SPR_WATER_MASK, FilterPaletteID::paletteWater)));
    EXPECT_FALSE(IsWaterSurfaceImage(ImageId(SPR_WATER_MASK, FilterPaletteID::paletteWaterMarker).WithBlended(true)));
    EXPECT_FALSE(IsWaterSurfaceImage(ImageId(SPR_WATER_OVERLAY, FilterPaletteID::paletteWater)));
    EXPECT_FALSE(IsWaterSurfaceImage(
        ImageId(SPR_WATER_MASK, FilterPaletteID::paletteWater).WithSecondary(Colour::black).WithBlended(true)));
}

TEST(WaterShimmerTests, DisabledByDefault)
{
    Config::SetDefaults();
    EXPECT_FALSE(Config::Get().general.waterShimmer);
}
