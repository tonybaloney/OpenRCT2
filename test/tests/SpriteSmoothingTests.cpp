/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "Scale2xReference.h"

#include <array>
#include <gtest/gtest.h>
#include <openrct2-ui/drawing/engines/opengl/SpriteSmoothing.h>
#include <openrct2/config/Config.h>

using OpenRCT2::Test::Scale2xReference;

TEST(SpriteSmoothingTests, DisabledByDefault)
{
    OpenRCT2::Config::SetDefaults();
    EXPECT_FALSE(OpenRCT2::Config::Get().general.spriteSmoothing);
}

TEST(SpriteSmoothingTests, OnlyEnabledZoomedInSpritesAndNeverScrollingText)
{
    using namespace OpenRCT2::Ui;
    const ImageId sprite(1);
    for (int8_t zoom = -2; zoom <= 3; zoom++)
    {
        EXPECT_FALSE(ShouldSmoothSprite(false, ZoomLevel{ zoom }, sprite));
        EXPECT_EQ(ShouldSmoothSprite(true, ZoomLevel{ zoom }, sprite), zoom < 0);
        EXPECT_FALSE(ShouldSmoothSprite(true, ZoomLevel{ zoom }, ImageId(SPR_SCROLLING_TEXT_DEFAULT)));
        for (uint32_t i = SPR_SCROLLING_TEXT_START; i < SPR_SCROLLING_TEXT_END; i++)
            EXPECT_FALSE(ShouldSmoothSprite(true, ZoomLevel{ zoom }, ImageId(i)));
    }
}

TEST(SpriteSmoothingTests, DiagonalAndTransparentQuadrantsUseSourceIndices)
{
    const std::array<uint8_t, 9> diagonal = { 0, 202, 0, 202, 0, 7, 0, 7, 0 };
    EXPECT_EQ(Scale2xReference(diagonal, 3, 3, 2, 2), 202);
    EXPECT_EQ(Scale2xReference(diagonal, 3, 3, 3, 2), 0);
    EXPECT_EQ(Scale2xReference(diagonal, 3, 3, 2, 3), 0);
    EXPECT_EQ(Scale2xReference(diagonal, 3, 3, 3, 3), 7);

    const std::array<uint8_t, 9> corner = { 0, 0, 0, 0, 214, 214, 0, 214, 214 };
    EXPECT_EQ(Scale2xReference(corner, 3, 3, 2, 2), 0);
    EXPECT_EQ(Scale2xReference(corner, 3, 3, 3, 2), 214);
    EXPECT_EQ(Scale2xReference(corner, 3, 3, 2, 3), 214);
    EXPECT_EQ(Scale2xReference(corner, 3, 3, 3, 3), 214);
}

TEST(SpriteSmoothingTests, ClampBordersAndPreserveSolidPixelsAndThinLines)
{
    const std::array<uint8_t, 1> single = { 255 };
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 2; x++)
            EXPECT_EQ(Scale2xReference(single, 1, 1, x, y), 255);

    const std::array<uint8_t, 9> line = { 0, 46, 0, 0, 46, 0, 0, 46, 0 };
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 6; x++)
            EXPECT_EQ(Scale2xReference(line, 3, 3, x, y), line[(y / 2) * 3 + x / 2]);
}

TEST(SpriteSmoothingTests, TileEdgesNeitherLoseCoverageNorExpandIntoAdjacentTiles)
{
    using OpenRCT2::Test::Scale2xCoverageReference;
    const std::array<uint8_t, 9> edge = { 0, 0, 0, 0, 214, 214, 0, 214, 214 };
    ASSERT_EQ(Scale2xReference(edge, 3, 3, 2, 2), 0);
    EXPECT_EQ(Scale2xCoverageReference(edge, 3, 3, 2, 2), 214);

    const std::array<uint8_t, 9> outside = { 0, 202, 0, 202, 0, 7, 0, 7, 0 };
    ASSERT_EQ(Scale2xReference(outside, 3, 3, 2, 2), 202);
    EXPECT_EQ(Scale2xCoverageReference(outside, 3, 3, 2, 2), 0);
}

TEST(SpriteSmoothingTests, EveryNeighbourhoodPreservesCoverageAndStillSmoothsOpaqueColours)
{
    using OpenRCT2::Test::Scale2xCoverageReference;
    constexpr std::array<uint8_t, 3> colours = { 0, 46, 202 };
    bool smoothed = false;
    for (int pattern = 0; pattern < 19683; pattern++)
    {
        std::array<uint8_t, 9> source{};
        auto remaining = pattern;
        for (auto& pixel : source)
        {
            pixel = colours[remaining % 3];
            remaining /= 3;
        }
        for (int y = 2; y < 4; y++)
        {
            for (int x = 2; x < 4; x++)
            {
                const auto canonical = Scale2xReference(source, 3, 3, x, y);
                const auto filtered = Scale2xCoverageReference(source, 3, 3, x, y);
                EXPECT_EQ(filtered == 0, source[4] == 0);
                EXPECT_EQ(filtered, (canonical == 0) == (source[4] == 0) ? canonical : source[4]);
                smoothed |= filtered != source[4];
            }
        }
    }
    EXPECT_TRUE(smoothed);
}
