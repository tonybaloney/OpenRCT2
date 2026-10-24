/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>

#ifdef ENABLE_GUEST_THOUGHTS
    #include <algorithm>
    #include <charconv>
    #include <filesystem>
    #include <fstream>
    #include <iostream>
    #include <openrct2-ui/guest/LlamaGuestThoughtBackend.h>
    #include <openrct2/Context.h>
    #include <openrct2/Game.h>
    #include <openrct2/GameState.h>
    #include <openrct2/OpenRCT2.h>
    #include <openrct2/config/Config.h>
    #include <openrct2/core/Json.hpp>
    #include <openrct2/core/Path.hpp>
    #include <openrct2/entity/EntityList.h>
    #include <openrct2/entity/Guest.h>
    #include <openrct2/localisation/Language.h>
    #include <openrct2/network/Network.h>
    #include <openrct2/peep/GuestDecision.h>
    #include <openrct2/platform/Platform.h>
    #include <openrct2/ride/RideData.h>
    #include <openrct2/ride/RideManager.hpp>

using namespace OpenRCT2;
using Clock = std::chrono::steady_clock;

namespace
{
    std::ofstream OpenOutput(const std::filesystem::path& path)
    {
        std::ofstream stream;
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        stream.open(path, std::ios::binary);
        return stream;
    }

    json_t Metrics()
    {
        const auto& state = getGameState();
        size_t guests{}, hungry{}, thirsty{}, sick{}, toilet{}, queued{}, walking{}, leaving{};
        uint64_t happiness{}, energy{}, hunger{}, thirst{}, nausea{};
        for (const auto* guest : EntityList<Guest>())
        {
            if (guest->outsideOfPark)
                continue;
            ++guests;
            happiness += guest->happiness;
            energy += guest->energy;
            hunger += guest->hunger;
            thirst += guest->thirst;
            nausea += guest->nausea;
            hungry += guest->hunger <= 10 && !guest->hasFoodOrDrink();
            thirsty += guest->thirst <= 25 && !guest->hasFoodOrDrink();
            sick += guest->nausea >= 140;
            toilet += guest->toilet >= 160;
            queued += guest->state == PeepState::queuing || guest->state == PeepState::queuingFront;
            walking += guest->state == PeepState::walking;
            leaving += guest->peepFlags.has(PeepFlag::leavingPark);
        }
        uint64_t rideCustomers{}, shopCustomers{};
        for (const auto& ride : RideManager(getGameState()))
        {
            if (ride.getRideTypeDescriptor().flags.has(RtdFlag::isShopOrFacility))
                shopCustomers += ride.totalCustomers;
            else
                rideCustomers += ride.totalCustomers;
        }
        const auto mean = [guests](uint64_t total) { return guests ? static_cast<double>(total) / guests : 0.0; };
        return {
            { "tick", state.currentTicks },
            { "guests", guests },
            { "park_rating", state.park.rating },
            { "cash_raw_tenths", state.park.cash },
            { "admissions", state.park.totalAdmissions },
            { "ride_customers", rideCustomers },
            { "shop_customers", shopCustomers },
            { "mean_happiness", mean(happiness) },
            { "mean_energy", mean(energy) },
            { "mean_hunger", mean(hunger) },
            { "mean_thirst", mean(thirst) },
            { "mean_nausea", mean(nausea) },
            { "hungry", hungry },
            { "thirsty", thirsty },
            { "sick", sick },
            { "toilet_need", toilet },
            { "queuing", queued },
            { "walking", walking },
            { "leaving", leaving },
        };
    }

    json_t ClassifierResult(const GuestDecisionSnapshot& snapshot, const std::vector<float>& probabilities)
    {
        const auto choice = SelectGuestDecision(probabilities, snapshot.candidates.size());
        const auto best = static_cast<size_t>(
            std::max_element(probabilities.begin(), probabilities.end()) - probabilities.begin());
        float second{};
        for (size_t i = 0; i < probabilities.size(); ++i)
            if (i != best)
                second = std::max(second, probabilities[i]);
        return {
            { "probabilities", probabilities },
            { "confidence", probabilities[best] },
            { "margin", probabilities[best] - second },
            { "winner_id", snapshot.candidates[best].rideId },
            { "passes_confidence", choice.has_value() },
            { "winner_is_marked_incumbent", snapshot.candidates[best].rideId == snapshot.heading },
        };
    }

    GuestDecisionSnapshot MarkIncumbent(GuestDecisionSnapshot snapshot, uint16_t heading)
    {
        snapshot.heading = heading;
        for (auto& candidate : snapshot.candidates)
        {
            auto attributes = json_t::parse(candidate.description);
            attributes["already_heading_here"] = candidate.rideId == heading;
            candidate.description = attributes.dump();
        }
        return snapshot;
    }
} // namespace

TEST(GuestDecisionExperiment, DISABLED_ComparePark)
{
    const auto park = Platform::GetEnvironmentVariable("OPENRCT2_DECISION_EXPERIMENT_PARK");
    if (park.empty())
        GTEST_SKIP() << "Use scripts\\compare-guest-decisions.ps1 to run this opt-in real-time experiment.";
    const auto output = std::filesystem::u8path(Platform::GetEnvironmentVariable("OPENRCT2_DECISION_EXPERIMENT_OUTPUT"));
    const auto secondsText = Platform::GetEnvironmentVariable("OPENRCT2_DECISION_EXPERIMENT_SECONDS");
    const auto model = Platform::GetEnvironmentVariable("OPENRCT2_DECISION_EXPERIMENT_MODEL");
    uint32_t seconds{};
    const auto parsed = std::from_chars(secondsText.data(), secondsText.data() + secondsText.size(), seconds);
    ASSERT_EQ(parsed.ec, std::errc{});
    ASSERT_EQ(parsed.ptr, secondsText.data() + secondsText.size());
    ASSERT_GE(seconds, 2u);
    ASSERT_LE(seconds, 3600u);
    ASSERT_TRUE(model == "d3-lite" || model == "d3-edge");
    ASSERT_TRUE(std::filesystem::is_directory(output));
    const auto directory = Path::GetDirectory(Platform::GetCurrentExecutablePath());
    auto createBackend = [directory, model] {
        return Ui::CreateLlamaGuestDecisionBackend(
            Path::Combine(directory, "guest-thoughts"), Path::Combine(directory, "models", "guest-decisions", model));
    };
    struct Cleanup
    {
        std::string userPath = gCustomUserDataPath;
        int32_t language = Config::Get().general.language;
        bool guestThoughts = Config::Get().interface.guestThoughts;
        bool guestDecisions = Config::Get().interface.guestDecisions;
        ~Cleanup()
        {
            ShutdownGuestDecisions();
            gCustomUserDataPath = userPath;
            Config::Get().general.language = language;
            Config::Get().interface.guestThoughts = guestThoughts;
            Config::Get().interface.guestDecisions = guestDecisions;
        }
    } cleanup;
    std::filesystem::create_directory(output / "profile");
    gCustomUserDataPath = Path::Combine(Platform::GetEnvironmentVariable("OPENRCT2_DECISION_EXPERIMENT_OUTPUT"), "profile");
    Config::Get().general.language = LANGUAGE_ENGLISH_UK;
    std::vector<GuestDecisionRecord> ablationSamples;
    json_t report = { { "model", model }, { "seconds_per_variant", seconds }, { "ticks_per_second", kGameUpdateFPS } };
    json_t initial;
    for (const bool enabled : { false, true })
    {
        ShutdownGuestDecisions();
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;
        auto context = CreateContext();
        Config::Get().general.language = LANGUAGE_ENGLISH_UK;
        Config::Get().interface.guestThoughts = false;
        Config::Get().interface.guestDecisions = enabled;
        ASSERT_TRUE(context->Initialise());
        ASSERT_TRUE(context->LoadParkFromFile(park));
        GameLoadInit();
        ASSERT_EQ(Network::GetMode(), Network::Mode::none);
        ASSERT_EQ(gLegacyScene, LegacyScene::playing);
        auto startMetrics = Metrics();
        const auto& state = getGameState();
        const json_t identity = {
            { "metrics", startMetrics },
            { "entity_checksum", getGameState().entities.getAllEntitiesChecksum().toString() },
            { "rng", { state.scenarioRand.state().s0, state.scenarioRand.state().s1 } },
        };
        if (!enabled)
            initial = identity;
        else
            ASSERT_EQ(identity, initial) << "A/B variants must start from the same imported state and RNG.";
        auto metrics = OpenOutput(output / (enabled ? "model-metrics.jsonl" : "native-metrics.jsonl"));
        auto trace = OpenOutput(output / (enabled ? "model-decisions.jsonl" : "native-decisions.jsonl"));
        uint32_t completed{};
        if (enabled)
            ConfigureGuestDecisions(createBackend);
        SetGuestDecisionTraceSink([&](const GuestDecisionRecord& record) {
            trace << SerializeGuestDecisionTrace(record) << '\n';
            if (record.outcome != GuestDecisionOutcome::pending)
            {
                ++completed;
                const bool hasIncumbent = std::any_of(
                    record.snapshot.candidates.begin(), record.snapshot.candidates.end(),
                    [&](const auto& candidate) { return candidate.rideId == record.snapshot.heading; });
                if (hasIncumbent && completed % 7 == 0 && ablationSamples.size() < 64)
                    ablationSamples.push_back(record);
            }
        });
        std::array<uint32_t, kMaxEntities> identities;
        identities.fill(UINT32_MAX);
        std::array<uint16_t, kMaxEntities> headings{};
        const auto trackHeadings = [&] {
            uint64_t changed{};
            for (const auto* guest : EntityList<Guest>())
            {
                const auto id = guest->id.ToUnderlying();
                const auto heading = guest->guestHeadingToRideId.ToUnderlying();
                if (identities[id] == guest->peepId && headings[id] != heading)
                    ++changed;
                identities[id] = guest->peepId;
                headings[id] = heading;
            }
            return changed;
        };
        trackHeadings();
        metrics << startMetrics.dump() << '\n';
        const auto start = Clock::now();
        double logicMilliseconds{};
        uint32_t slowTicks{};
        uint64_t headingChanges{};
        json_t averages = json_t::object();
        for (uint32_t tick = 1; tick <= seconds * kGameUpdateFPS; ++tick)
        {
            const auto before = Clock::now();
            gameStateUpdateLogic();
            const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - before).count();
            logicMilliseconds += elapsed;
            slowTicks += elapsed > 25;
            headingChanges += trackHeadings();
            if (tick % kGameUpdateFPS == 0)
            {
                auto sample = Metrics();
                sample["elapsed_seconds"] = tick / kGameUpdateFPS;
                metrics << sample.dump() << '\n';
                for (const auto* key : { "mean_happiness", "hungry", "thirsty", "sick", "toilet_need", "queuing", "guests" })
                    averages[key] = averages.value(key, 0.0) + sample[key].get<double>() / seconds;
                if (tick % (kGameUpdateFPS * 30) == 0)
                {
                    metrics.flush();
                    trace.flush();
                    const auto stats = GetGuestDecisionStats();
                    std::cout << (enabled ? "model" : "native") << " " << tick / kGameUpdateFPS
                              << "s accepted=" << stats.applied << " fallback=" << stats.fallback << '\n'
                              << std::flush;
                }
            }
            if (enabled)
                ASSERT_NE(GetGuestDecisionStats().status, GuestDecisionStatus::unavailable);
            ASSERT_EQ(GetGuestDecisionStats().traceFailures, 0u);
            std::this_thread::sleep_until(start + std::chrono::milliseconds(25 * tick));
        }
        const auto stats = GetGuestDecisionStats();
        report[enabled ? "model_on" : "native_off"] = {
            { "start", startMetrics },
            { "end", Metrics() },
            { "time_mean", averages },
            { "heading_changes", headingChanges },
            { "wall_seconds", std::chrono::duration<double>(Clock::now() - start).count() },
            { "mean_logic_tick_ms", logicMilliseconds / (seconds * kGameUpdateFPS) },
            { "ticks_over_25ms", slowTicks },
            { "submitted", stats.submitted },
            { "accepted", stats.applied },
            { "low_confidence", stats.fallback },
            { "stale", stats.stale },
            { "pending", stats.outstanding },
        };
        ShutdownGuestDecisions();
        trace.flush();
        metrics.flush();
        auto progress = OpenOutput(output / "comparison.json");
        progress << report.dump(2) << '\n';
    }
    report["initial_state"] = initial;
    auto ablations = OpenOutput(output / "incumbent-ablation.jsonl");
    if (!ablationSamples.empty())
    {
        auto backend = createBackend();
        const std::atomic_bool cancelled{ false };
        for (const auto& record : ablationSamples)
        {
            const auto hidden = MarkIncumbent(record.snapshot, 65535);
            const auto other = std::find_if(
                record.snapshot.candidates.begin(), record.snapshot.candidates.end(),
                [&](const auto& candidate) { return candidate.rideId != record.snapshot.heading; });
            ASSERT_NE(other, record.snapshot.candidates.end());
            const auto swapped = MarkIncumbent(record.snapshot, other->rideId);
            ablations << json_t(
                             {
                                 { "guest_id", record.guestId },
                                 { "tick", record.snapshot.tick },
                                 { "original_incumbent", record.snapshot.heading },
                                 { "swapped_incumbent", swapped.heading },
                                 { "original", ClassifierResult(record.snapshot, record.probabilities) },
                                 { "hidden", ClassifierResult(hidden, backend->decide(hidden, cancelled)) },
                                 { "swapped", ClassifierResult(swapped, backend->decide(swapped, cancelled)) },
                             })
                             .dump()
                      << '\n';
        }
    }
    report["ablation_samples"] = ablationSamples.size();
    auto summary = OpenOutput(output / "comparison.json");
    summary << report.dump(2) << '\n';
}
#else
TEST(GuestDecisionExperiment, DISABLED_ComparePark)
{
    GTEST_SKIP() << "Requires ENABLE_GUEST_THOUGHTS and the installed CUDA decision model.";
}
#endif
