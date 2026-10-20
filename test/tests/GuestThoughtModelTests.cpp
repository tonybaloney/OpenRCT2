/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>

#ifdef ENABLE_GUEST_THOUGHTS
    #include <algorithm>
    #include <chrono>
    #include <iostream>
    #include <openrct2-ui/guest/LlamaGuestThoughtBackend.h>
    #include <openrct2/core/Json.hpp>
    #include <openrct2/core/Path.hpp>
    #include <openrct2/core/String.hpp>
    #include <openrct2/entity/Peep.h>
    #include <openrct2/peep/GuestThoughtService.h>
    #include <openrct2/platform/Platform.h>

using namespace OpenRCT2;

static std::unique_ptr<IGuestThoughtBackend> Backend(bool cpu = false)
{
    const auto directory = Path::GetDirectory(Platform::GetCurrentExecutablePath());
    return Ui::CreateLlamaGuestThoughtBackend(
        Path::Combine(directory, "guest-thoughts"), Path::Combine(directory, "models", "qwen2.5-1.5b-instruct-q8_0.gguf"), cpu);
}

TEST(GuestThoughtModel, DISABLED_PairedStates)
{
    auto backend = Backend();
    const std::atomic_bool cancelled = false;
    GuestNarrativeSnapshot healthy;
    healthy.happiness = 200;
    healthy.energy = 100;
    healthy.hunger = 200;
    healthy.thirst = 200;
    healthy.activity = static_cast<uint8_t>(PeepState::walking);
    std::vector<GuestNarrativeSnapshot> cases{ healthy };
    auto hungry = healthy;
    hungry.hunger = 8;
    cases.push_back(hungry);
    cases.push_back(healthy);
    auto thirsty = healthy;
    thirsty.thirst = 10;
    cases.push_back(thirsty);
    cases.push_back(healthy);
    auto sick = healthy;
    sick.nausea = 200;
    cases.push_back(sick);
    cases.push_back(healthy);
    auto tired = healthy;
    tired.happiness = 75;
    tired.energy = 52;
    cases.push_back(tired);
    cases.push_back(healthy);
    auto toilet = healthy;
    toilet.toilet = 200;
    cases.push_back(toilet);
    auto mixed = hungry;
    mixed.happiness = 75;
    mixed.energy = 52;
    cases.push_back(mixed);
    auto queue = healthy;
    queue.activity = static_cast<uint8_t>(PeepState::queuing);
    queue.queueTime = 512;
    queue.thoughts = { "waiting too long in a queue" };
    cases.push_back(queue);
    auto ride = healthy;
    ride.activity = static_cast<uint8_t>(PeepState::onRide);
    ride.thoughts = { "enjoyed a ride" };
    cases.push_back(ride);
    auto unhappy = healthy;
    unhappy.happiness = 50;
    cases.push_back(unhappy);
    auto lost = healthy;
    lost.thoughts = { "cannot find a ride (ride: Terror-dactyl)", "too crowded", "the park is clean" };
    cases.push_back(lost);
    for (const auto& snapshot : cases)
    {
        const auto start = std::chrono::steady_clock::now();
        const auto raw = backend->generate(snapshot, cancelled);
        const auto compact = CompactGuestNarrative(raw);
        std::cout << "needs=" << json_t(snapshot.needs()).dump() << " raw=" << raw
                  << " compact=" << compact.value_or("<rejected>") << " elapsed_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()
                  << std::endl;
        ASSERT_TRUE(compact);
        EXPECT_TRUE(IsGuestNarrativeGrounded(snapshot, *compact)) << *compact;
        EXPECT_LE(String::lengthOf(compact->c_str()), kGuestNarrativeMaxCharacters);
        EXPECT_LT(static_cast<size_t>(std::count(compact->begin(), compact->end(), ' ')), kGuestNarrativeMaxWords);
        EXPECT_EQ(compact->find("How about you"), std::string::npos);
    }
}

TEST(GuestThoughtModel, DISABLED_CpuFallback)
{
    auto backend = Backend(true);
    GuestNarrativeSnapshot snapshot;
    snapshot.happiness = 200;
    snapshot.energy = 100;
    snapshot.thirst = 200;
    snapshot.hunger = 8;
    const std::atomic_bool cancelled = false;
    const auto compact = CompactGuestNarrative(backend->generate(snapshot, cancelled));
    ASSERT_TRUE(compact);
    EXPECT_TRUE(IsGuestNarrativeGrounded(snapshot, *compact)) << *compact;
}

TEST(GuestThoughtModel, DISABLED_MissingModelAndPrecancelledRequest)
{
    const auto directory = Path::GetDirectory(Platform::GetCurrentExecutablePath());
    auto backend = Ui::CreateLlamaGuestThoughtBackend(
        Path::Combine(directory, "guest-thoughts"), Path::Combine(directory, "models", "missing-test-model.gguf"), false);
    const std::atomic_bool cancelled = true;
    EXPECT_TRUE(backend->generate({}, cancelled).empty());
    const std::atomic_bool active = false;
    EXPECT_THROW(backend->generate({}, active), std::runtime_error);
}
#endif
