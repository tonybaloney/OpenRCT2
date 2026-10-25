/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>
#include <openrct2/Game.h>
#include <openrct2/drawing/LightFX.h>
#include <openrct2/drawing/PaletteIndex.h>
#include <openrct2/drawing/RenderTarget.h>
#include <openrct2/entity/EntityBase.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/interface/WindowBase.h>
#include <openrct2/world/Weather.h>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;

class LightFXTests : public testing::Test
{
protected:
    std::vector<std::unique_ptr<WindowBase>> _windows;
    float _dayNightCycle{};
    decltype(Weather::gLightningFlash) _lightningFlash{};

    void SetUp() override
    {
        _windows = std::move(gWindowList);
        gWindowList.clear();
        _dayNightCycle = gDayNightCycle;
        _lightningFlash = Weather::gLightningFlash;
        LightFx::Init();
    }

    void TearDown() override
    {
        gWindowList = std::move(_windows);
        gDayNightCycle = _dayNightCycle;
        Weather::gLightningFlash = _lightningFlash;
        LightFx::Init();
    }

    static void AddScreenLight(int32_t x, int32_t y, uint8_t id = 0)
    {
        // The existing screen-space sentinel bypasses map occlusion for an isolated light-map test.
        constexpr int32_t screenSpaceZ = 0x7FFF;
        EntityBase entity{};
        LightFx::Add3DLight(
            entity, id, { screenSpaceZ + y - x / 2, screenSpaceZ + y + x / 2, screenSpaceZ }, LightFx::LightType::lantern3);
    }
};

TEST_F(LightFXTests, LanternHasSoftRadialFalloff)
{
    EXPECT_GT(LightFx::CalculateLanternIntensity(0, 0), 80);
    EXPECT_GT(LightFx::CalculateLanternIntensity(64, 0), 0);
    auto previous = LightFx::CalculateLanternIntensity(0, 0);
    for (int32_t radius = 1; radius <= 128; radius++)
    {
        const auto intensity = LightFx::CalculateLanternIntensity(radius, 0);
        EXPECT_LE(intensity, previous);
        EXPECT_EQ(intensity, LightFx::CalculateLanternIntensity(0, -radius));
        previous = intensity;
    }
    EXPECT_EQ(0, previous);
    EXPECT_EQ(0, LightFx::CalculateLanternIntensity(128, 128));
}

TEST_F(LightFXTests, MixingPreservesUnlitPixelsAndProtectsHighlights)
{
    EXPECT_EQ(37, LightFx::MixLight(37, 255, 0));
    EXPECT_EQ(37, LightFx::MixLight(37, 0, 255));
    EXPECT_EQ(254, LightFx::MixLight(250, 255, 255));
    EXPECT_EQ(255, LightFx::MixLight(255, 255, 255));
    for (uint32_t intensity = 0; intensity <= 255; intensity++)
    {
        const auto exposure = (80u * intensity * 6u) >> 8;
        EXPECT_EQ(37u + 218u * exposure / (255u + exposure), LightFx::MixLight(37, 80, static_cast<uint8_t>(intensity)));
    }
}

TEST_F(LightFXTests, DaytimePaletteIsUnchanged)
{
    gDayNightCycle = 0;
    Weather::gLightningFlash = 0;
    uint8_t red = 100, green = 110, blue = 120;
    LightFx::ApplyPaletteFilter(100, &red, &green, &blue);
    EXPECT_EQ(100, red);
    EXPECT_EQ(110, green);
    EXPECT_EQ(120, blue);
    EXPECT_EQ(0, LightFx::GetPalette()[100].red);
    EXPECT_EQ(0, LightFx::GetPalette()[100].green);
    EXPECT_EQ(0, LightFx::GetPalette()[100].blue);
}

TEST_F(LightFXTests, NightLightingIsWarmEvenOnDarkSurfaces)
{
    gDayNightCycle = 1;
    Weather::gLightningFlash = 0;
    uint8_t red = 0, green = 0, blue = 0;
    LightFx::ApplyPaletteFilter(100, &red, &green, &blue);
    const auto& light = LightFx::GetPalette()[100];
    EXPECT_GT(light.red, light.green);
    EXPECT_GT(light.green, light.blue);
    EXPECT_GT(light.blue, 0);
}

TEST_F(LightFXTests, LightningClearsTheLightPalette)
{
    gDayNightCycle = 1;
    Weather::gLightningFlash = 0;
    uint8_t red = 100, green = 100, blue = 100;
    LightFx::ApplyPaletteFilter(100, &red, &green, &blue);
    ASSERT_GT(LightFx::GetPalette()[100].red, 0);

    Weather::gLightningFlash = 1;
    red = green = blue = 100;
    LightFx::ApplyPaletteFilter(100, &red, &green, &blue);
    EXPECT_EQ(100, red);
    EXPECT_EQ(100, green);
    EXPECT_EQ(100, blue);
    EXPECT_EQ(0, LightFx::GetPalette()[100].red);
}

TEST_F(LightFXTests, SmallLightMapsKeepTheLampCentred)
{
    RenderTarget rt;
    rt.width = 9;
    rt.height = 7;
    Viewport vp;
    vp.width = rt.width;
    vp.height = rt.height;
    AddScreenLight(4, 3);
    const auto map = LightFx::RenderLightMap(vp, rt);
    ASSERT_EQ(63u, map.size());
    EXPECT_EQ(LightFx::CalculateLanternIntensity(0, 0), map[3 * 9 + 4]);
    EXPECT_EQ(LightFx::CalculateLanternIntensity(-4, -3), map[0]);
    EXPECT_EQ(LightFx::CalculateLanternIntensity(4, 3), map[62]);
}

TEST_F(LightFXTests, LightMapsClipToViewportAndWindows)
{
    RenderTarget rt;
    rt.width = 80;
    rt.height = 60;
    Viewport vp;
    vp.pos = { 10, 10 };
    vp.width = 60;
    vp.height = 40;
    auto window = std::make_unique<WindowBase>();
    window->classification = WindowClass::options;
    window->windowPos = { 35, 25 };
    window->width = 10;
    window->height = 10;
    gWindowList.push_back(std::move(window));
    AddScreenLight(30, 20);
    const auto map = LightFx::RenderLightMap(vp, rt);
    EXPECT_GT(map[20 * 80 + 30], 0);
    EXPECT_EQ(0, map[30 * 80 + 40]);
    EXPECT_EQ(0, map[5 * 80 + 40]);
    EXPECT_EQ(0, map[30 * 80 + 5]);
    EXPECT_EQ(0, map[55 * 80 + 40]);
    EXPECT_EQ(0, map[30 * 80 + 75]);
}

TEST_F(LightFXTests, LightMapsResizeAndDoNotKeepStaleLights)
{
    RenderTarget rt;
    rt.width = rt.height = 16;
    Viewport vp;
    vp.width = vp.height = 16;
    AddScreenLight(8, 8);
    ASSERT_GT(LightFx::RenderLightMap(vp, rt)[8 * 16 + 8], 0);
    rt.width = vp.width = 7;
    rt.height = vp.height = 5;
    const auto map = LightFx::RenderLightMap(vp, rt);
    ASSERT_EQ(35u, map.size());
    EXPECT_TRUE(std::all_of(map.begin(), map.end(), [](auto intensity) { return intensity == 0; }));
}

TEST_F(LightFXTests, SoftwareCompositingRespectsSourceAndDestinationPitch)
{
    std::array<PaletteIndex, 12> pixels{};
    for (auto offset : { 0, 1, 2, 6, 7, 8 })
        pixels[offset] = PaletteIndex::pi10;
    RenderTarget rt;
    rt.bits = pixels.data();
    rt.width = 3;
    rt.height = 2;
    rt.pitch = 3;
    Viewport vp;
    vp.width = rt.width;
    vp.height = rt.height;
    std::array<uint32_t, 256> palette{};
    std::array<uint32_t, 256> lightPalette{};
    palette[10] = 0x00706050;
    std::array<uint32_t, 10> output{};
    output.fill(0xDEADBEEF);
    LightFx::RenderToTexture(vp, output.data(), 5 * sizeof(uint32_t), rt, palette.data(), lightPalette.data());
    for (auto offset : { 0, 1, 2, 5, 6, 7 })
        EXPECT_EQ(palette[10], output[offset]);
    for (auto offset : { 3, 4, 8, 9 })
        EXPECT_EQ(0xDEADBEEF, output[offset]);
}

TEST_F(LightFXTests, OverlappingLightsSaturateWithoutWrapping)
{
    RenderTarget rt;
    rt.width = rt.height = 16;
    Viewport vp;
    vp.width = vp.height = 16;
    for (uint8_t id = 0; id < 4; id++)
        AddScreenLight(8, 8, id);
    EXPECT_EQ(255, LightFx::RenderLightMap(vp, rt)[8 * 16 + 8]);
}

TEST_F(LightFXTests, CompletelyOffscreenLightsLeaveSmallMapsUnchanged)
{
    RenderTarget rt;
    rt.width = 5;
    rt.height = 3;
    Viewport vp;
    vp.width = rt.width;
    vp.height = rt.height;
    AddScreenLight(-10000, -10000, 0);
    AddScreenLight(10000, 10000, 1);
    AddScreenLight(-128, 0, 2);
    AddScreenLight(0, -128, 3);
    const auto map = LightFx::RenderLightMap(vp, rt);
    EXPECT_TRUE(std::all_of(map.begin(), map.end(), [](auto intensity) { return intensity == 0; }));
}

TEST_F(LightFXTests, ZoomingInScalesTheLightHalo)
{
    RenderTarget rt;
    rt.width = rt.height = 32;
    Viewport vp;
    vp.width = vp.height = 32;
    vp.zoom = ZoomLevel{ -1 };
    AddScreenLight(16, 16);
    const auto map = LightFx::RenderLightMap(vp, rt);
    EXPECT_EQ(LightFx::CalculateLanternIntensity(4, 0), map[16 * 32 + 24]);
}

TEST_F(LightFXTests, SoftwareCompositingUsesTheWarmLightMap)
{
    std::array<PaletteIndex, 6> pixels{};
    pixels.fill(PaletteIndex::pi10);
    RenderTarget rt;
    rt.bits = pixels.data();
    rt.width = 3;
    rt.height = 2;
    Viewport vp;
    vp.width = rt.width;
    vp.height = rt.height;
    std::array<uint32_t, 256> palette{}, lightPalette{};
    palette[10] = 0x00404040;
    lightPalette[10] = 0x00050C12;
    std::array<uint32_t, 6> output{};
    AddScreenLight(2, 1);
    LightFx::RenderToTexture(vp, output.data(), 3 * sizeof(uint32_t), rt, palette.data(), lightPalette.data());
    for (int32_t y = 0; y < rt.height; y++)
    {
        for (int32_t x = 0; x < rt.width; x++)
        {
            const auto intensity = LightFx::CalculateLanternIntensity(x - 2, y - 1);
            const auto red = LightFx::MixLight(64, 18, intensity);
            const auto green = LightFx::MixLight(64, 12, intensity);
            const auto blue = LightFx::MixLight(64, 5, intensity);
            EXPECT_EQ(static_cast<uint32_t>(red | (green << 8) | (blue << 16)), output[y * rt.width + x]);
        }
    }
}
