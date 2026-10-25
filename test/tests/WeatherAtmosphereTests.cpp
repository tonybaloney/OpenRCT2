/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>
#include <openrct2-ui/drawing/engines/opengl/WeatherAtmosphere.h>

using namespace OpenRCT2;
using namespace OpenRCT2::Ui;

TEST(WeatherAtmosphereTests, DisabledAndDryFramesHaveNoEffect)
{
    WeatherAtmosphere atmosphere;
    atmosphere.BeginFrame(8, 6, Weather::Level::heavy, false, false);
    atmosphere.AddRegion(0, 0, 8, 6);
    EXPECT_TRUE(atmosphere.GetMask().empty());
    EXPECT_EQ(0, atmosphere.GetMist());
    EXPECT_EQ(0, atmosphere.GetRain());
    atmosphere.BeginFrame(8, 6, Weather::Level::none, true, false);
    EXPECT_TRUE(atmosphere.GetMask().empty());
}

TEST(WeatherAtmosphereTests, HeavyWeatherHasMoreMistAndSnowHasNoRainStreaks)
{
    WeatherAtmosphere atmosphere;
    atmosphere.BeginFrame(8, 6, Weather::Level::light, true, false);
    const auto lightMist = atmosphere.GetMist();
    const auto lightRain = atmosphere.GetRain();
    atmosphere.BeginFrame(8, 6, Weather::Level::heavy, true, false);
    EXPECT_GT(atmosphere.GetMist(), lightMist);
    EXPECT_GT(atmosphere.GetRain(), lightRain);
    EXPECT_LE(atmosphere.GetMist(), 0.18f);
    atmosphere.BeginFrame(8, 6, Weather::Level::heavy, true, true);
    EXPECT_TRUE(atmosphere.IsSnow());
    EXPECT_EQ(0, atmosphere.GetRain());
    EXPECT_GT(atmosphere.GetMist(), 0);
}

TEST(WeatherAtmosphereTests, OnlyWeatherRegionsAreMarkedAndEdgesAreClipped)
{
    WeatherAtmosphere atmosphere;
    atmosphere.BeginFrame(8, 6, Weather::Level::heavy, true, false);
    atmosphere.AddRegion(-2, -1, 5, 4);
    atmosphere.AddRegion(6, 4, 10, 10);
    atmosphere.AddRegion(12, 12, 1, 1);
    const auto mask = atmosphere.GetMask();
    ASSERT_EQ(48u, mask.size());
    for (int32_t y = 0; y < 6; y++)
    {
        for (int32_t x = 0; x < 8; x++)
            EXPECT_EQ((x < 3 && y < 3) || (x >= 6 && y >= 4) ? 255 : 0, mask[y * 8 + x]);
    }
}

TEST(WeatherAtmosphereTests, FramesAndResizesDoNotKeepStaleCoverage)
{
    WeatherAtmosphere atmosphere;
    atmosphere.BeginFrame(8, 6, Weather::Level::heavy, true, false);
    atmosphere.AddRegion(0, 0, 8, 6);
    atmosphere.BeginFrame(8, 6, Weather::Level::heavy, true, false);
    EXPECT_TRUE(std::all_of(atmosphere.GetMask().begin(), atmosphere.GetMask().end(), [](auto v) { return v == 0; }));
    atmosphere.BeginFrame(3, 2, Weather::Level::heavy, true, false);
    ASSERT_EQ(6u, atmosphere.GetMask().size());
    atmosphere.AddRegion(1, 0, 2, 2);
    EXPECT_EQ(0, atmosphere.GetMask()[0]);
    EXPECT_EQ(255, atmosphere.GetMask()[5]);
    atmosphere.BeginFrame(3, 2, Weather::Level::heavy, false, false);
    EXPECT_TRUE(atmosphere.GetMask().empty());
    atmosphere.BeginFrame(0, 0, Weather::Level::heavy, true, false);
    EXPECT_TRUE(atmosphere.GetMask().empty());
}
