/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/GameStateSnapshots.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/ParkImporter.h>
#include <openrct2/config/Config.h>
#include <openrct2/core/Crypt.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/core/MemoryStream.h>
#include <openrct2/drawing/Palette.h>
#include <openrct2/entity/EntityList.h>
#include <openrct2/entity/EntityRegistry.h>
#include <openrct2/entity/EntityTweener.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/localisation/Language.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/object/ObjectRepository.h>
#include <openrct2/park/ParkFile.h>
#include <openrct2/peep/GuestDecision.h>
#include <openrct2/platform/Platform.h>
#include <openrct2/scenario/Scenario.h>
#include <openrct2/scenes/SceneManager.h>
#include <openrct2/util/Util.h>
#include <openrct2/world/MapAnimation.h>
#include <set>
#include <sstream>

using namespace OpenRCT2;

namespace
{
    std::string Digest(const void* data, size_t length)
    {
        const auto bytes = Crypt::SHA256(data, length);
        constexpr char hex[] = "0123456789abcdef";
        std::string result;
        for (auto byte : bytes)
        {
            result += hex[byte >> 4];
            result += hex[byte & 15];
        }
        return result;
    }

    MemoryStream Save()
    {
        MemoryStream stream;
        ParkFileExporter exporter;
        exporter.Export(getGameState(), stream, 0);
        return stream;
    }

    std::string UtilityState()
    {
        std::ostringstream stream;
        stream << GetUtilRandomEngine();
        return stream.str();
    }

    json_t AnimationState()
    {
        const auto state = MapAnimations::CaptureUpdateState();
        json_t result = { { "tiles", json_t::array() }, { "temporary", json_t::array() } };
        for (const auto& tile : state.tiles)
            result["tiles"].push_back({ tile.x, tile.y });
        for (const auto& [location, type] : state.temporary)
            result["temporary"].push_back({ location.x, location.y, location.z, static_cast<uint8_t>(type) });
        return result;
    }

    json_t WorldFingerprint()
    {
        auto save = Save();
        const auto* data = static_cast<const uint8_t*>(save.GetData());
        if (save.GetLength() < 64)
            throw std::runtime_error("Truncated checkpoint.");
        uint32_t count{}, compression{};
        std::memcpy(&count, data + 12, 4);
        std::memcpy(&compression, data + 24, 4);
        if (compression != 0)
            throw std::runtime_error("Fingerprint requires uncompressed checkpoint.");
        if (64ULL + 20ULL * count > save.GetLength())
            throw std::runtime_error("Invalid uncompressed checkpoint table.");
        const auto utility = UtilityState();
        const auto animations = AnimationState().dump();
        json_t result = { { "utility_rng", Digest(utility.data(), utility.size()) },
                          { "map_update_queues", Digest(animations.data(), animations.size()) } };
        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t id{};
            uint64_t offset{}, length{};
            const auto* entry = data + 64 + i * 20;
            std::memcpy(&id, entry, 4);
            std::memcpy(&offset, entry + 4, 8);
            std::memcpy(&length, entry + 12, 8);
            if (64ULL + 20ULL * count + offset + length > save.GetLength())
                throw std::runtime_error("Invalid checkpoint chunk.");
            // Authoring contains wall-clock timestamps, not simulation state.
            if (id != 1)
                result[std::to_string(id)] = Digest(data + 64 + 20 * count + offset, length);
        }
        return result;
    }

    void Restore(MemoryStream& stream, IContext& context, const std::string& utilityState, const json_t& animations)
    {
        ShutdownGuestDecisions();
        stream.SetPosition(0);
        auto importer = ParkImporter::CreateParkFile(context.GetObjectRepository());
        auto loaded = importer->LoadFromStream(&stream, false);
        context.GetObjectManager().LoadObjects(loaded.RequiredObjects);
        MapAnimations::ClearAll();
        importer->Import(getGameState());
        // The park restores per-entity indices, not the registry's tile lookup vectors.
        getGameState().entities.resetEntitySpatialIndices();
        GameFixSaveVars();
        ResetAllSpriteQuadrantPlacements();
        Drawing::LoadPalette();
        EntityTweener::get().reset();
        MapAnimations::MarkAllTiles();
        MapAnimations::UpdateState updates;
        for (const auto& tile : animations.at("tiles"))
            updates.tiles.emplace_back(tile.at(0).get<int32_t>(), tile.at(1).get<int32_t>());
        for (const auto& animation : animations.at("temporary"))
        {
            const auto type = animation.at(3).get<uint8_t>();
            if (type > static_cast<uint8_t>(MapAnimations::TemporaryType::landEdgeDoor))
                throw std::runtime_error("Invalid checkpoint temporary animation type.");
            updates.temporary.emplace_back(
                CoordsXYZ{ animation.at(0).get<int32_t>(), animation.at(1).get<int32_t>(), animation.at(2).get<int32_t>() },
                static_cast<MapAnimations::TemporaryType>(type));
        }
        MapAnimations::RestoreUpdateState(updates);
        gGameSpeed = 1;
        std::istringstream utility(utilityState);
        utility >> GetUtilRandomEngine();
        if (!utility)
            throw std::runtime_error("Invalid checkpoint utility RNG state.");
    }

    void WriteJson(const std::filesystem::path& file, const json_t& value)
    {
        std::ofstream stream;
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        stream.open(file, std::ios::binary);
        stream << value.dump() << '\n';
    }

    Guest* FindGuest(uint16_t entity, uint32_t identity)
    {
        auto* guest = getGameState().entities.getEntity<Guest>(EntityId::FromUnderlying(entity));
        return guest && guest->peepId == identity ? guest : nullptr;
    }

    struct PreviewRng
    {
        random_engine_t::state_type state = ScenarioRandState();
        std::mt19937 utility = GetUtilRandomEngine();
        ~PreviewRng()
        {
            // Umbrella/rain eligibility peeks at ScenarioRand even in the native dry run.
            ScenarioRandSeed(state.s0, state.s1);
            GetUtilRandomEngine() = utility;
        }
    };

    std::optional<GuestDecisionSnapshot> Capture(Guest& guest)
    {
        PreviewRng guard;
        return guest.captureDecision();
    }

    GuestDecisionSnapshot SnapshotFromCase(const json_t& value)
    {
        auto* guest = FindGuest(value.at("entity_id"), value.at("guest_id"));
        if (!guest)
            throw std::runtime_error("Focal guest identity missing after restore.");
        auto snapshot = Capture(*guest);
        if (!snapshot || snapshot->prompt(true, true) != value.at("prompt").get<std::string>())
            throw std::runtime_error("Decision options/state changed across checkpoint restore.");
        return *snapshot;
    }

    std::string Partition(uint32_t value)
    {
        const auto bucket = value % 20;
        return bucket < 12 ? "train" : bucket < 15 ? "development" : bucket < 17 ? "calibration" : "test";
    }

    uint32_t Mix(uint32_t value)
    {
        value ^= value >> 16;
        value *= 0x7feb352d;
        value ^= value >> 15;
        value *= 0x846ca68b;
        return value ^ (value >> 16);
    }

    json_t GuestState(const Guest& guest)
    {
        return {
            { "hunger", guest.hunger },
            { "thirst", guest.thirst },
            { "toilet", guest.toilet },
            { "nausea", guest.nausea },
            { "happiness", guest.happiness },
            { "energy", guest.energy },
            { "state", static_cast<uint8_t>(guest.state) },
            { "x", guest.x },
            { "y", guest.y },
            { "cash", guest.cashInPocket },
            { "food_purchases", guest.amountOfFood },
            { "drink_purchases", guest.amountOfDrinks },
            { "rides", guest.guestNumRides },
            { "lost", guest.timeLost },
            { "heading", guest.guestHeadingToRideId.ToUnderlying() },
            { "current_ride", guest.currentRide.ToUnderlying() },
            { "outside", guest.outsideOfPark },
            { "leaving", guest.peepFlags.has(PeepFlag::leavingPark) },
        };
    }

    json_t Rollout(const json_t& entry, uint32_t ticks, const std::vector<uint32_t>& horizons)
    {
        const auto entity = entry.at("entity_id").get<uint16_t>();
        const auto identity = entry.at("guest_id").get<uint32_t>();
        json_t samples = json_t::array();
        const auto* initial = FindGuest(entity, identity);
        if (!initial)
            throw std::runtime_error("Missing initial guest.");
        uint32_t walking{}, queued{}, moving{}, targetContacts{}, headingChanges{};
        uint64_t hungerSum{}, thirstSum{}, toiletSum{}, nauseaSum{}, energyBurdenSum{}, happinessSum{};
        uint32_t foodConsumption{}, drinkConsumption{}, toiletRelief{}, completedRides{}, lostTicks{};
        auto previous = GuestState(*initial);
        int32_t previousX = initial->x, previousY = initial->y;
        auto previousHeading = initial->guestHeadingToRideId;
        auto previousRide = initial->currentRide;
        auto previousState = initial->state;
        uint8_t previousHunger = initial->hunger, previousThirst = initial->thirst, previousToilet = initial->toilet;
        bool hadFood = initial->hasFoodOrDrink() && !initial->hasDrink(), hadDrink = initial->hasDrink();
        const auto start = previous;
        const auto target = entry.value("forced_ride", entry.at("incumbent_id").get<uint16_t>());
        uint32_t resolutionTick{};
        std::string resolution = target == 65535 ? "no_initial_target" : "";
        uint8_t previousFoodPurchases = initial->amountOfFood, previousDrinkPurchases = initial->amountOfDrinks;
        json_t probeWorld;
        std::string terminal;
        uint32_t terminalTick{};
        for (uint32_t tick = 1; tick <= ticks; ++tick)
        {
            gameStateUpdateLogic();
            if (tick == 256)
                probeWorld = WorldFingerprint();
            if (!terminal.empty())
                continue;
            const auto* guest = FindGuest(entity, identity);
            if (!guest)
            {
                terminal = "removed_or_slot_reused";
                terminalTick = tick;
                continue;
            }
            walking += guest->state == PeepState::walking;
            queued += guest->state == PeepState::queuing || guest->state == PeepState::queuingFront;
            moving += guest->x != previousX || guest->y != previousY;
            headingChanges += guest->guestHeadingToRideId != previousHeading;
            if (entry.contains("forced_ride"))
                targetContacts += guest->currentRide.ToUnderlying() == entry["forced_ride"].get<uint16_t>()
                    && guest->state != PeepState::walking;
            hungerSum += 255 - guest->hunger;
            thirstSum += 255 - guest->thirst;
            toiletSum += guest->toilet;
            nauseaSum += guest->nausea;
            energyBurdenSum += std::clamp(128 - static_cast<int>(guest->energy), 0, 96);
            happinessSum += guest->happiness;
            lostTicks += guest->timeLost != 0;
            foodConsumption += hadFood && guest->hunger > previousHunger;
            drinkConsumption += hadDrink && guest->thirst > previousThirst;
            toiletRelief += previousState == PeepState::onRide && guest->toilet < previousToilet;
            completedRides += previousState == PeepState::onRide && guest->state == PeepState::leavingRide
                && guest->currentRide == previousRide;
            if (resolution.empty())
            {
                const bool atTarget = guest->currentRide.ToUnderlying() == target;
                const bool activeAtTarget = atTarget
                    && (guest->state == PeepState::queuing || guest->state == PeepState::queuingFront
                        || guest->state == PeepState::enteringRide || guest->state == PeepState::onRide
                        || guest->state == PeepState::buying || guest->state == PeepState::leavingRide);
                if (previousRide.ToUnderlying() == target && previousState == PeepState::onRide
                    && guest->state == PeepState::leavingRide)
                    resolution = "target_ride_or_facility_exit";
                else if (
                    atTarget
                    && (guest->amountOfFood != previousFoodPurchases || guest->amountOfDrinks != previousDrinkPurchases))
                    resolution = "target_purchase";
                else if (!activeAtTarget && guest->guestHeadingToRideId.ToUnderlying() != target)
                    resolution = "native_abandoned_initial_target";
                if (!resolution.empty())
                    resolutionTick = tick;
            }
            previousX = guest->x;
            previousY = guest->y;
            previousHeading = guest->guestHeadingToRideId;
            previousRide = guest->currentRide;
            previousState = guest->state;
            previousHunger = guest->hunger;
            previousThirst = guest->thirst;
            previousToilet = guest->toilet;
            hadFood = guest->hasFoodOrDrink() && !guest->hasDrink();
            hadDrink = guest->hasDrink();
            previousFoodPurchases = guest->amountOfFood;
            previousDrinkPurchases = guest->amountOfDrinks;
            if (std::find(horizons.begin(), horizons.end(), tick) != horizons.end())
            {
                previous = GuestState(*guest);
                samples.push_back(
                    { { "tick", tick },
                      { "guest", previous },
                      { "walking_ticks", walking },
                      { "queue_ticks", queued },
                      { "moving_ticks", moving },
                      { "target_contact_ticks", targetContacts },
                      { "heading_changes", headingChanges },
                      { "initial_target_resolution", resolution },
                      { "initial_target_resolution_tick", resolutionTick },
                      { "integrals",
                        { { "hunger_burden", hungerSum },
                          { "thirst_burden", thirstSum },
                          { "toilet_burden", toiletSum },
                          { "nausea_burden", nauseaSum },
                          { "energy_burden", energyBurdenSum },
                          { "happiness", happinessSum } } } });
            }
            if (guest->outsideOfPark)
            {
                previous = GuestState(*guest);
                terminal = "departed";
                terminalTick = tick;
                if (resolution.empty())
                {
                    resolution = "departed_without_target_completion";
                    resolutionTick = tick;
                }
            }
        }
        return {
            { "start", start },
            { "end", previous },
            { "samples", samples },
            { "terminal", terminal },
            { "terminal_tick", terminalTick },
            { "observed_ticks", terminalTick ? terminalTick : ticks },
            { "simulated_ticks", ticks },
            { "walking_ticks", walking },
            { "queue_ticks", queued },
            { "moving_ticks", moving },
            { "target_contact_ticks", targetContacts },
            { "heading_changes", headingChanges },
            { "events",
              { { "food_consumption_updates", foodConsumption },
                { "drink_consumption_updates", drinkConsumption },
                { "toilet_relief_updates", toiletRelief },
                { "ride_exit_transitions", completedRides },
                { "nonzero_time_lost_ticks", lostTicks } } },
            { "initial_target_resolution", resolution },
            { "initial_target_resolution_tick", resolutionTick },
            { "probe_world_256", probeWorld },
            { "terminal_measurement",
              { { "tick", terminalTick },
                { "walking_ticks", walking },
                { "queue_ticks", queued },
                { "integrals",
                  { { "hunger_burden", hungerSum },
                    { "thirst_burden", thirstSum },
                    { "toilet_burden", toiletSum },
                    { "nausea_burden", nauseaSum },
                    { "energy_burden", energyBurdenSum },
                    { "happiness", happinessSum } } } } },
            { "final_world", WorldFingerprint() },
        };
    }
} // namespace

TEST(GuestDecisionDataset, UtilityRandomEngineRestoresExactly)
{
    const auto original = GetUtilRandomEngine();
    GetUtilRandomEngine().seed(74101);
    const auto state = UtilityState();
    const auto expected = UtilRand();
    std::istringstream stream(state);
    stream >> GetUtilRandomEngine();
    EXPECT_TRUE(stream);
    EXPECT_EQ(UtilRand(), expected);
    GetUtilRandomEngine() = original;
}

TEST(GuestDecisionDataset, MapUpdateQueuesRestoreExactly)
{
    const auto original = MapAnimations::CaptureUpdateState();
    const auto before = AnimationState();
    MapAnimations::UpdateState state;
    state.tiles.emplace_back(4, 5);
    state.temporary.emplace_back(CoordsXYZ{ 128, 160, 16 }, MapAnimations::TemporaryType::onRidePhoto);
    MapAnimations::RestoreUpdateState(state);
    const auto restored = AnimationState();
    EXPECT_EQ(restored["tiles"], json_t::array({ { 4, 5 } }));
    EXPECT_EQ(restored["temporary"], json_t::array({ { 128, 160, 16, 0 } }));
    MapAnimations::RestoreUpdateState(original);
    EXPECT_EQ(AnimationState(), before);
}

TEST(GuestDecisionDataset, DISABLED_Offline)
{
    const auto configPath = Platform::GetEnvironmentVariable("OPENRCT2_D3_DATASET_CONFIG");
    if (configPath.empty())
        GTEST_SKIP() << "Use scripts\\produce-d3-dataset.mjs for this opt-in offline dataset task.";
    std::ifstream configStream(std::filesystem::u8path(configPath));
    json_t config;
    configStream >> config;
    const auto output = std::filesystem::u8path(config.at("output").get<std::string>());
    std::filesystem::create_directories(output);
    const auto operation = config.at("operation").get<std::string>();
    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    struct Cleanup
    {
        std::string user = gCustomUserDataPath;
        ~Cleanup()
        {
            ShutdownGuestDecisions();
            gCustomUserDataPath = user;
        }
    } cleanup;
    gCustomUserDataPath = (output / "profile").string();
    std::filesystem::create_directories(output / "profile");
    Config::Get().general.language = LANGUAGE_ENGLISH_UK;
    Config::Get().interface.guestThoughts = false;
    Config::Get().interface.guestDecisions = false;
    auto context = CreateContext();
    ASSERT_TRUE(context->Initialise());
    if (operation == "capture")
    {
        ASSERT_TRUE(context->LoadParkFromFile(config.at("park").get<std::string>()));
        GameLoadInit();
        auto dependencies = json_t::array();
        const auto& repository = context->GetObjectRepository();
        const auto* items = repository.GetObjects();
        for (size_t i = 0; i < repository.GetNumObjects(); ++i)
        {
            if (!items[i].LoadedObject)
                continue;
            std::ifstream object(std::filesystem::u8path(items[i].Path), std::ios::binary);
            ASSERT_TRUE(object.is_open());
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(object)), std::istreambuf_iterator<char>());
            dependencies.push_back(
                { { "identifier", items[i].Identifier },
                  { "file", items[i].Path },
                  { "sha256", Digest(data.data(), data.size()) } });
        }
        WriteJson(output / "object-dependencies.json", dependencies);
        const auto episode = config.at("episode").get<uint32_t>();
        ScenarioRandSeed(Mix(episode + 74101), Mix(episode + 97613));
        GetUtilRandomEngine().seed(Mix(episode + 38471));
        const auto warmup = config.at("warmup_ticks").get<uint32_t>();
        for (uint32_t i = 0; i < warmup; ++i)
            gameStateUpdateLogic();
        const auto split = config.at("split").get<std::string>();
        const auto maximum = config.at("maximum_cases").get<uint32_t>();
        const auto interval = config.at("capture_interval_ticks").get<uint32_t>();
        const auto windows = config.at("capture_windows").get<uint32_t>();
        ASSERT_GT(interval, 0u);
        ASSERT_LE(warmup, 48000u);
        ASSERT_LE(maximum, 5000u);
        ASSERT_LE(static_cast<uint64_t>(interval) * windows, 200000u);
        std::vector<json_t> cases;
        std::set<uint32_t> seen;
        for (uint32_t window = 0; window < windows && cases.size() < maximum; ++window)
        {
            for (uint32_t i = 0; i < interval; ++i)
                gameStateUpdateLogic();
            std::vector<GuestDecisionSnapshot> choices;
            const auto beforeCapture = WorldFingerprint();
            for (auto* guest : EntityList<Guest>())
            {
                if (Partition(Mix(guest->peepId)) != split || seen.contains(guest->peepId))
                    continue;
                if (auto snapshot = Capture(*guest))
                    choices.push_back(std::move(*snapshot));
            }
            ASSERT_EQ(WorldFingerprint(), beforeCapture) << "Candidate preview changed simulation state.";
            std::stable_sort(choices.begin(), choices.end(), [episode, window](const auto& a, const auto& b) {
                return Mix(a.guest.guestId ^ (episode * 1709 + window)) < Mix(b.guest.guestId ^ (episode * 1709 + window));
            });
            if (choices.empty())
                continue;
            auto checkpoint = Save();
            const auto utilityState = UtilityState();
            const auto animationState = AnimationState();
            const auto fingerprint = WorldFingerprint();
            constexpr uint32_t probeTicks = 256;
            for (uint32_t i = 0; i < probeTicks; ++i)
                gameStateUpdateLogic();
            const auto uninterrupted = WorldFingerprint();
            auto uninterruptedSave = Save();
            auto snapshots = CreateGameStateSnapshots();
            auto& uninterruptedEntities = snapshots->CreateSnapshot();
            snapshots->Capture(uninterruptedEntities);
            Restore(checkpoint, *context, utilityState, animationState);
            ASSERT_EQ(WorldFingerprint(), fingerprint);
            for (uint32_t i = 0; i < probeTicks; ++i)
                gameStateUpdateLogic();
            const auto replayed = WorldFingerprint();
            if (replayed != uninterrupted)
            {
                auto restoredSave = Save();
                std::ofstream sourceFile(output / "uninterrupted-diagnostic.park", std::ios::binary);
                sourceFile.write(static_cast<const char*>(uninterruptedSave.GetData()), uninterruptedSave.GetLength());
                std::ofstream restoredFile(output / "restored-diagnostic.park", std::ios::binary);
                restoredFile.write(static_cast<const char*>(restoredSave.GetData()), restoredSave.GetLength());
                auto& replayedEntities = snapshots->CreateSnapshot();
                snapshots->Capture(replayedEntities);
                snapshots->LogCompareDataToFile(
                    (output / "entity-replay-diff.txt").string(), snapshots->Compare(uninterruptedEntities, replayedEntities));
                WriteJson(
                    output / "uninterrupted-replay-mismatch.json",
                    { { "uninterrupted", uninterrupted }, { "restored", replayed } });
                FAIL() << "Restored native continuation differs from the uninterrupted capture world.";
            }
            Restore(checkpoint, *context, utilityState, animationState);
            ASSERT_EQ(WorldFingerprint(), fingerprint);
            const auto hash = Digest(checkpoint.GetData(), checkpoint.GetLength());
            const auto file = output / (hash + ".park");
            if (!std::filesystem::exists(file))
            {
                std::ofstream stream(file, std::ios::binary);
                stream.write(static_cast<const char*>(checkpoint.GetData()), checkpoint.GetLength());
                ASSERT_TRUE(stream.good());
            }
            const auto perWindow = config.value("cases_per_checkpoint", 4u);
            for (size_t i = 0; i < choices.size() && i < perWindow && cases.size() < maximum; ++i)
            {
                const auto& snapshot = choices[i];
                const auto features = json_t::parse(snapshot.anonymousFeatures());
                auto optionText = json_t::array();
                for (const auto& option : features["options"])
                    optionText.push_back(option.dump());
                json_t mapping = json_t::array();
                for (const auto& option : snapshot.candidates)
                    mapping.push_back({ { "ride_id", option.rideId }, { "name", option.name } });
                cases.push_back(
                    { { "schema", "neutral-anonymous-v1" },
                      { "episode", episode },
                      { "split", split },
                      { "checkpoint", file.string() },
                      { "checkpoint_sha256", hash },
                      { "utility_rng_state", utilityState },
                      { "map_update_queues", animationState },
                      { "world", fingerprint },
                      { "uninterrupted_probe", { { "ticks", probeTicks }, { "world", uninterrupted }, { "matched", true } } },
                      { "tick", snapshot.tick },
                      { "entity_id", snapshot.guest.entityId },
                      { "guest_id", snapshot.guest.guestId },
                      { "incumbent_id", snapshot.heading },
                      { "features", features },
                      { "state_json", features["state"].dump() },
                      { "option_json", optionText },
                      { "prompt", snapshot.prompt(true, true) },
                      { "audit_options", mapping } });
                seen.insert(snapshot.guest.guestId);
            }
        }
        ASSERT_FALSE(cases.empty());
        WriteJson(output / "cases.json", cases);
        std::cout << "Captured " << cases.size() << " anonymous decision groups.\n";
    }
    else
    {
        ASSERT_EQ(operation, "branch");
        auto* sceneManager = context->GetSceneManager();
        sceneManager->setActiveScene(sceneManager->getGameScene());
        const auto entry = config.at("case");
        const auto file = entry.at("checkpoint").get<std::string>();
        std::ifstream input(std::filesystem::u8path(file), std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        ASSERT_EQ(Digest(bytes.data(), bytes.size()), entry.at("checkpoint_sha256").get<std::string>());
        MemoryStream checkpoint(bytes);
        Restore(checkpoint, *context, entry.at("utility_rng_state").get<std::string>(), entry.at("map_update_queues"));
        const auto restored = WorldFingerprint();
        if (restored != entry.at("world"))
        {
            WriteJson(output / "restore-mismatch.json", { { "expected", entry.at("world") }, { "actual", restored } });
            FAIL() << "Full simulation chunks changed across checkpoint restore.";
        }
        auto snapshot = SnapshotFromCase(entry);
        ASSERT_EQ(WorldFingerprint(), restored) << "Capturing a decision changed serialized simulation state.";
        const auto horizons = config.at("horizons").get<std::vector<uint32_t>>();
        ASSERT_FALSE(horizons.empty());
        ASSERT_TRUE(std::is_sorted(horizons.begin(), horizons.end()));
        const auto ticks = horizons.back();
        ASSERT_LE(ticks, 48000u);
        const auto replica = config.at("replica").get<uint32_t>();
        const auto option = config.at("option").get<int>();
        ASSERT_LE(replica, 7u);
        ASSERT_GE(option, -1);
        ASSERT_LT(option, static_cast<int>(snapshot.candidates.size()));
        if (replica)
            ScenarioRandSeed(Mix(snapshot.tick + replica * 173 + 241), Mix(snapshot.tick + replica * 619 + 991));
        const auto rng = ScenarioRandState();
        auto intervention = entry;
        if (option >= 0)
        {
            PreviewRng guard;
            std::string rejection;
            if (!FindGuest(snapshot.guest.entityId, snapshot.guest.guestId)
                     ->applyDecision(snapshot, snapshot.candidates[option].rideId, &rejection))
            {
                WriteJson(
                    output / "branch-invalid.json",
                    { { "status", "invalid" }, { "reason", rejection }, { "option", option }, { "replica", replica } });
                return;
            }
            intervention["forced_ride"] = snapshot.candidates[option].rideId;
        }
        auto result = Rollout(intervention, ticks, horizons);
        result["replica"] = replica;
        result["option"] = option;
        result["initial_rng"] = { rng.s0, rng.s1 };
        if (!replica && option == -1)
        {
            ASSERT_TRUE(entry.at("uninterrupted_probe").at("matched").get<bool>());
            ASSERT_EQ(result.at("probe_world_256"), entry.at("uninterrupted_probe").at("world"))
                << "Cold native replay differs from the uninterrupted source-world probe.";
        }
        WriteJson(output / "branch.json", result);
    }
}
