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
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace OpenRCT2
{
    struct IGuestThoughtBackend
    {
        virtual ~IGuestThoughtBackend() = default;
        virtual std::string generate(const GuestNarrativeSnapshot&, const std::atomic_bool& cancelled) = 0;
    };

    class GuestThoughtService
    {
    public:
        enum class Status
        {
            pending,
            ready,
            unavailable,
        };
        struct Result
        {
            Status status = Status::pending;
            std::string text;
        };
        using Factory = std::function<std::unique_ptr<IGuestThoughtBackend>()>;
        explicit GuestThoughtService(Factory factory, std::chrono::milliseconds cooldown = std::chrono::seconds(5));
        ~GuestThoughtService();
        uint64_t subscribe();
        void update(uint64_t ticket, GuestNarrativeSnapshot snapshot);
        Result read(uint64_t ticket);
        void release(uint64_t ticket);
        void reset();

    private:
        struct State
        {
            GuestNarrativeSnapshot snapshot;
            std::string key;
            uint64_t revision{};
            Result result;
            std::chrono::steady_clock::time_point nextAllowed{};
        };
        void run();
        Factory _factory;
        std::chrono::milliseconds _cooldown;
        std::mutex _mutex;
        std::condition_variable _changed;
        std::map<uint64_t, State> _states;
        std::deque<uint64_t> _pending;
        std::thread _worker;
        std::shared_ptr<std::atomic_bool> _activeCancellation;
        uint64_t _activeTicket{}, _nextTicket{};
        bool _stop{};
    };
} // namespace OpenRCT2
