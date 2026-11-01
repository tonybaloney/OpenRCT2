/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../drawing/Colour.h"
#include "GuestNarrative.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace OpenRCT2
{
    constexpr size_t kGuestDecisionMaxCandidates = 8;

    struct GuestDecisionCandidate
    {
        uint16_t rideId{};
        std::string description;
        std::string name;
    };

    struct GuestDecisionSnapshot
    {
        GuestNarrativeSnapshot guest;
        uint16_t heading = 65535;
        uint32_t tick{};
        uint8_t minimumIntensity{}, maximumIntensity{}, nauseaTolerance{};
        std::vector<GuestDecisionCandidate> candidates;

        std::string prompt(bool omitIncumbentHints = false, bool anonymousChoices = false) const;
        std::string anonymousFeatures() const;
        std::string stateJson(bool omitIncumbentHints = false) const;
        std::string freshnessKey() const;
    };

    struct IGuestDecisionBackend
    {
        virtual ~IGuestDecisionBackend() = default;
        virtual void prepare(const std::atomic_bool&)
        {
        }
        virtual std::vector<float> decide(const GuestDecisionSnapshot&, const std::atomic_bool& cancelled) = 0;
    };

    enum class GuestDecisionStatus
    {
        disabled,
        loading,
        ready,
        unavailable,
        suspended,
    };

    enum class GuestDecisionCohort : uint8_t
    {
        baseline,
        fineTuned,
    };

    GuestDecisionCohort AssignGuestDecisionCohort(uint32_t guestId);
    std::optional<GuestDecisionCohort> GetGuestDecisionCohort(uint32_t guestId);
    Drawing::Colour GetGuestDecisionShirtColour(uint32_t guestId, Drawing::Colour original);

    std::optional<size_t> SelectGuestDecision(const std::vector<float>& probabilities, size_t expected);

    class GuestDecisionService
    {
    public:
        using Factory = std::function<std::unique_ptr<IGuestDecisionBackend>()>;
        struct Result
        {
            GuestDecisionSnapshot snapshot;
            std::optional<size_t> choice;
            std::chrono::steady_clock::time_point submitted;
            std::vector<float> probabilities;
            GuestDecisionCohort cohort = GuestDecisionCohort::baseline;
        };
        static constexpr size_t kCapacity = 8;

        explicit GuestDecisionService(Factory factory, Factory comparisonFactory = {});
        ~GuestDecisionService();
        bool submit(GuestDecisionSnapshot snapshot, GuestDecisionCohort cohort = GuestDecisionCohort::baseline);
        std::optional<Result> poll();
        void reset();
        void reconfigure(Factory factory, Factory comparisonFactory = {});
        GuestDecisionStatus status();
        size_t outstanding();

    private:
        void run();
        std::mutex _mutex;
        std::condition_variable _changed;
        Factory _factory;
        Factory _comparisonFactory;
        std::deque<Result> _pending, _completed;
        std::shared_ptr<std::atomic_bool> _cancellation;
        std::thread _worker;
        uint64_t _epoch{}, _backendRevision{};
        GuestDecisionStatus _status;
        bool _stop{}, _active{};
    };

    struct GuestDecisionStats
    {
        struct CohortStats
        {
            uint32_t submitted{}, applied{}, stale{}, fallback{};
        };
        GuestDecisionStatus status = GuestDecisionStatus::disabled;
        uint32_t submitted{}, applied{}, stale{}, fallback{};
        size_t outstanding{};
        uint32_t traceFailures{};
        std::array<CohortStats, 2> cohorts{};
    };

    enum class GuestDecisionOutcome
    {
        pending,
        applied,
        unchanged,
        lowConfidence,
        stale,
    };

    struct GuestDecisionRecord
    {
        uint16_t entityId{};
        uint32_t guestId{};
        std::string guestName;
        GuestDecisionSnapshot snapshot;
        std::vector<float> probabilities;
        std::optional<size_t> choice;
        GuestDecisionOutcome outcome = GuestDecisionOutcome::pending;
        std::chrono::steady_clock::time_point updated;
        uint32_t elapsedMilliseconds{};
        std::optional<uint16_t> headingAtApply;
        std::string rejection;
        std::optional<GuestDecisionCohort> cohort;
        std::string modelId;
    };

    using GuestDecisionTraceSink = std::function<void(const GuestDecisionRecord&)>;
    void SetGuestDecisionTraceSink(GuestDecisionTraceSink sink);
    std::string SerializeGuestDecisionTrace(const GuestDecisionRecord& record, bool omitIncumbentHints = false);
    constexpr size_t kGuestDecisionHistoryCapacity = 128;
    const std::deque<GuestDecisionRecord>& GetGuestDecisionHistory();
    const GuestDecisionRecord* GetGuestDecisionRecord(uint16_t entityId, uint32_t guestId);

    void ConfigureGuestDecisions(
        GuestDecisionService::Factory factory, GuestDecisionService::Factory comparisonFactory = {}, bool comparison = false,
        std::string modelId = {});
    void ResetGuestDecisions();
    void ShutdownGuestDecisions();
    void UpdateGuestDecisions();
    GuestDecisionStats GetGuestDecisionStats();
} // namespace OpenRCT2
