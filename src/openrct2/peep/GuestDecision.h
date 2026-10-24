/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "GuestNarrative.h"

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

        std::string prompt() const;
        std::string stateJson() const;
        std::string freshnessKey() const;
    };

    struct IGuestDecisionBackend
    {
        virtual ~IGuestDecisionBackend() = default;
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
        };
        static constexpr size_t kCapacity = 8;

        explicit GuestDecisionService(Factory factory);
        ~GuestDecisionService();
        bool submit(GuestDecisionSnapshot snapshot);
        std::optional<Result> poll();
        void reset();
        void reconfigure(Factory factory);
        GuestDecisionStatus status();
        size_t outstanding();

    private:
        void run();
        std::mutex _mutex;
        std::condition_variable _changed;
        Factory _factory;
        std::deque<Result> _pending, _completed;
        std::shared_ptr<std::atomic_bool> _cancellation;
        std::thread _worker;
        uint64_t _epoch{}, _backendRevision{};
        GuestDecisionStatus _status;
        bool _stop{}, _active{};
    };

    struct GuestDecisionStats
    {
        GuestDecisionStatus status = GuestDecisionStatus::disabled;
        uint32_t submitted{}, applied{}, stale{}, fallback{};
        size_t outstanding{};
        uint32_t traceFailures{};
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
    };

    using GuestDecisionTraceSink = std::function<void(const GuestDecisionRecord&)>;
    void SetGuestDecisionTraceSink(GuestDecisionTraceSink sink);
    std::string SerializeGuestDecisionTrace(const GuestDecisionRecord& record);
    constexpr size_t kGuestDecisionHistoryCapacity = 128;
    const std::deque<GuestDecisionRecord>& GetGuestDecisionHistory();
    const GuestDecisionRecord* GetGuestDecisionRecord(uint16_t entityId, uint32_t guestId);

    void ConfigureGuestDecisions(GuestDecisionService::Factory factory);
    void ResetGuestDecisions();
    void ShutdownGuestDecisions();
    void UpdateGuestDecisions();
    GuestDecisionStats GetGuestDecisionStats();
} // namespace OpenRCT2
