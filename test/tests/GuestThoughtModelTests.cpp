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
    #include <cstdlib>
    #include <filesystem>
    #include <iostream>
    #include <openrct2-ui/guest/LlamaGuestThoughtBackend.h>
    #include <openrct2/core/File.h>
    #include <openrct2/core/Json.hpp>
    #include <openrct2/core/Path.hpp>
    #include <openrct2/core/String.hpp>
    #include <openrct2/entity/Peep.h>
    #include <openrct2/peep/GuestDecision.h>
    #include <openrct2/peep/GuestThoughtService.h>
    #include <openrct2/platform/Platform.h>

using namespace OpenRCT2;

TEST(GuestDecisionModel, DISABLED_InstalledD3Cuda)
{
    const auto directory = Path::GetDirectory(Platform::GetCurrentExecutablePath());
    for (const auto* model : { "d3-edge", "d3-lite" })
    {
        auto backend = Ui::CreateLlamaGuestDecisionBackend(
            Path::Combine(directory, "guest-thoughts"), Path::Combine(directory, "models", "guest-decisions", model));
        const std::atomic_bool cancelled = false;
        GuestDecisionSnapshot snapshot;
        snapshot.guest.happiness = 200;
        snapshot.guest.energy = 100;
        snapshot.guest.hunger = 200;
        snapshot.guest.thirst = 200;
        snapshot.guest.activity = static_cast<uint8_t>(PeepState::walking);
        snapshot.maximumIntensity = 10;
        snapshot.nauseaTolerance = 3;
        snapshot.candidates = { { 1, "Visit an exciting gentle ride, no queue, nearby, free admission." },
                                { 2, "Buy food at a nearby food stall, free meal, no queue." } };
        std::vector<float> healthyProbabilities;
        for (const bool hungry : { false, true, false })
        {
            snapshot.guest.hunger = hungry ? 8 : 200;
            const auto start = std::chrono::steady_clock::now();
            const auto scores = backend->decide(snapshot, cancelled);
            ASSERT_EQ(scores.size(), 2u);
            const auto chosen = std::distance(scores.begin(), std::max_element(scores.begin(), scores.end()));
            std::cout << model << " hungry=" << hungry << " chosen=" << chosen << " probabilities=" << json_t(scores).dump()
                      << " elapsed_ms="
                      << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()
                      << std::endl;
            if (hungry)
            {
                // This deliberately unrestricted probe measures state sensitivity,
                // not a guarantee of judgement. Native need gating is tested separately.
                ASSERT_EQ(healthyProbabilities.size(), scores.size());
                EXPECT_GT(std::abs(scores[1] - healthyProbabilities[1]), 0.005f);
            }
            else
            {
                if (!healthyProbabilities.empty())
                    EXPECT_EQ(scores, healthyProbabilities);
                healthyProbabilities = scores;
                EXPECT_EQ(chosen, 0);
            }
        }
    }
}

class GuestThoughtModelCatalog : public testing::Test
{
protected:
    std::filesystem::path temporary;
    std::string directory;
    json_t catalog;

    void SetUp() override
    {
        temporary = std::filesystem::temp_directory_path()
            / ("openrct2-thought-catalog-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(temporary));
        const auto utf8 = temporary.u8string();
        directory.assign(utf8.begin(), utf8.end());
        catalog = {
            { "models",
              { { { "label", "Test base model" },
                  { "instruct", false },
                  { "files",
                    { { { "name", "first.gguf" }, { "size", 4 } }, { { "name", "second.gguf" }, { "size", 4 } } } } } } }
        };
    }

    void TearDown() override
    {
        std::filesystem::remove_all(temporary);
    }

    void Save()
    {
        Json::WriteToFile(Path::Combine(directory, "guest-thought-models.json"), catalog);
    }
};

TEST_F(GuestThoughtModelCatalog, AvailabilityRequiresAllShardsAndExactSizes)
{
    Save();
    auto models = Ui::LoadGuestThoughtModels(directory);
    ASSERT_EQ(models.size(), 1u);
    EXPECT_EQ(models[0].label, "Test base model");
    EXPECT_EQ(models[0].bytes, 8u);
    EXPECT_FALSE(models[0].instruct);
    EXPECT_FALSE(models[0].allowCpuFallback);
    EXPECT_FALSE(models[0].firstPersonPrefill);
    EXPECT_FALSE(models[0].available);
    EXPECT_EQ(models[0].path, Path::Combine(directory, "first.gguf"));
    File::WriteAllBytes(Path::Combine(directory, "first.gguf"), "test", 4);
    EXPECT_FALSE(Ui::LoadGuestThoughtModels(directory)[0].available);
    File::WriteAllBytes(Path::Combine(directory, "second.gguf"), "test", 4);
    EXPECT_TRUE(Ui::LoadGuestThoughtModels(directory)[0].available);
    File::WriteAllBytes(Path::Combine(directory, "second.gguf"), "bad", 3);
    EXPECT_FALSE(Ui::LoadGuestThoughtModels(directory)[0].available);
}

TEST_F(GuestThoughtModelCatalog, DecisionCatalogRejectsUnknownAndDuplicateModelIds)
{
    catalog = { { "models", { { { "id", "d3-edge" }, { "label", "Edge" } }, { { "id", "d3-lite" }, { "label", "Lite" } } } } };
    const auto file = Path::Combine(directory, "guest-decision-models.json");
    Json::WriteToFile(file, catalog);
    auto models = Ui::LoadGuestDecisionModels(directory);
    ASSERT_EQ(models.size(), 2u);
    EXPECT_FALSE(models[0].available);
    EXPECT_FALSE(models[1].available);
    catalog["models"][1]["id"] = "../outside";
    Json::WriteToFile(file, catalog);
    EXPECT_THROW(Ui::LoadGuestDecisionModels(directory), std::runtime_error);
    catalog["models"][1]["id"] = "d3-edge";
    Json::WriteToFile(file, catalog);
    EXPECT_THROW(Ui::LoadGuestDecisionModels(directory), std::runtime_error);
}

TEST_F(GuestThoughtModelCatalog, MissingAndEmptyCatalogsFailExplicitly)
{
    EXPECT_THROW(Ui::LoadGuestThoughtModels(directory), std::exception);
    catalog["models"] = json_t::array();
    Save();
    EXPECT_THROW(Ui::LoadGuestThoughtModels(directory), std::runtime_error);
}

TEST_F(GuestThoughtModelCatalog, EmptyFilesAndNonLocalFilenamesFailExplicitly)
{
    for (const auto* name : { "../outside.gguf", "..\\outside.gguf", ".", ".." })
    {
        catalog["models"][0]["files"][0]["name"] = name;
        Save();
        EXPECT_THROW(Ui::LoadGuestThoughtModels(directory), std::runtime_error) << name;
    }
    catalog["models"][0]["files"] = json_t::array();
    Save();
    EXPECT_THROW(Ui::LoadGuestThoughtModels(directory), std::runtime_error);
}

static std::unique_ptr<IGuestThoughtBackend> Backend(bool cpu = false)
{
    const auto directory = Path::GetDirectory(Platform::GetCurrentExecutablePath());
    return Ui::CreateLlamaGuestThoughtBackend(
        Path::Combine(directory, "guest-thoughts"), Path::Combine(directory, "models", "qwen2.5-1.5b-instruct-q8_0.gguf"),
        { .forceCpu = cpu });
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

TEST(GuestThoughtModel, DISABLED_ParagraphCuda)
{
    const auto directory = Path::GetDirectory(Platform::GetCurrentExecutablePath());
    auto backend = Ui::CreateLlamaGuestThoughtBackend(
        Path::Combine(directory, "guest-thoughts"), Path::Combine(directory, "models", "qwen2.5-1.5b-instruct-q8_0.gguf"),
        { .allowCpuFallback = false });
    GuestNarrativeSnapshot snapshot;
    snapshot.happiness = 200;
    snapshot.energy = 100;
    snapshot.thirst = 200;
    snapshot.hunger = 8;
    snapshot.activity = static_cast<uint8_t>(PeepState::walking);
    const std::atomic_bool cancelled = false;
    const auto raw = backend->generate(snapshot, cancelled);
    const auto paragraph = CompactGuestNarrative(raw);
    std::cout << "paragraph=" << paragraph.value_or("<rejected>") << std::endl;
    ASSERT_TRUE(paragraph);
    EXPECT_TRUE(IsGuestNarrativeGrounded(snapshot, *paragraph)) << *paragraph;
    EXPECT_GT(String::lengthOf(paragraph->c_str()), 96u);
    EXPECT_LE(String::lengthOf(paragraph->c_str()), kGuestNarrativeMaxCharacters);
    const auto spaces = static_cast<size_t>(std::count(paragraph->begin(), paragraph->end(), ' '));
    EXPECT_GE(spaces, 16u);
    EXPECT_LT(spaces, kGuestNarrativeMaxWords);
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
        Path::Combine(directory, "guest-thoughts"), Path::Combine(directory, "models", "missing-test-model.gguf"));
    const std::atomic_bool cancelled = true;
    EXPECT_TRUE(backend->generate({}, cancelled).empty());
    const std::atomic_bool active = false;
    EXPECT_THROW(backend->generate({}, active), std::runtime_error);
}

TEST(GuestThoughtModel, DISABLED_GpuHeadroomIsRequiredWithoutCpuFallback)
{
    const auto directory = Path::GetDirectory(Platform::GetCurrentExecutablePath());
    auto backend = Ui::CreateLlamaGuestThoughtBackend(
        Path::Combine(directory, "guest-thoughts"), Path::Combine(directory, "models", "qwen2.5-1.5b-instruct-q8_0.gguf"),
        { .allowCpuFallback = false, .modelBytes = UINT64_MAX });
    const std::atomic_bool cancelled = false;
    try
    {
        backend->generate({}, cancelled);
        FAIL() << "A model larger than GPU memory must not load or fall back to CPU.";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_STREQ(e.what(), "Not enough free GPU memory for this model and its inference context.");
    }
}

TEST(GuestThoughtModel, DISABLED_InstalledCatalogCuda)
{
    const auto directory = Path::GetDirectory(Platform::GetCurrentExecutablePath());
    const auto models = Ui::LoadGuestThoughtModels(Path::Combine(directory, "models"));
    const std::atomic_bool cancelled = false;
    const auto* filter = std::getenv("OPENRCT2_GUEST_THOUGHT_TEST_MODEL");
    size_t tested{};
    for (const auto& model : models)
    {
        if (filter && Path::GetFileName(model.path) != filter)
            continue;
        ++tested;
        SCOPED_TRACE(model.label);
        std::cout << "Loading " << model.label << " (" << model.bytes << " bytes), CUDA required" << std::endl;
        ASSERT_TRUE(model.available) << model.path;
        auto backend = Ui::CreateLlamaGuestThoughtBackend(
            Path::Combine(directory, "guest-thoughts"), model.path,
            { .instruct = model.instruct,
              .allowCpuFallback = false,
              .modelBytes = model.bytes,
              .firstPersonPrefill = model.firstPersonPrefill });
        GuestNarrativeSnapshot snapshot;
        snapshot.happiness = 200;
        snapshot.energy = 100;
        snapshot.hunger = 200;
        snapshot.thirst = 200;
        snapshot.activity = static_cast<uint8_t>(PeepState::walking);
        for (const auto hunger : { 200, 8, 200 })
        {
            snapshot.hunger = static_cast<uint8_t>(hunger);
            const auto start = std::chrono::steady_clock::now();
            const auto raw = backend->generate(snapshot, cancelled);
            const auto compact = CompactGuestNarrative(raw);
            std::cout << model.label << " feeling=" << snapshot.feeling() << " raw=" << raw
                      << " compact=" << compact.value_or("<rejected>") << " elapsed_ms="
                      << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()
                      << std::endl;
            ASSERT_TRUE(compact);
            EXPECT_TRUE(IsGuestNarrativeGrounded(snapshot, *compact)) << *compact;
        }
    }
    EXPECT_GT(tested, 0u) << "No catalog entry matched OPENRCT2_GUEST_THOUGHT_TEST_MODEL.";
}
#endif
