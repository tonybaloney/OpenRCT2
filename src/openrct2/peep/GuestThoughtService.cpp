/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "GuestThoughtService.h"

#include "../Diagnostic.h"

#include <algorithm>
#include <exception>
#include <stdexcept>

namespace OpenRCT2
{
    GuestThoughtService::GuestThoughtService(Factory factory, std::chrono::milliseconds cooldown)
        : _factory(std::move(factory))
        , _cooldown(cooldown)
    {
    }

    GuestThoughtService::~GuestThoughtService()
    {
        {
            std::lock_guard lock(_mutex);
            _stop = true;
            if (_activeCancellation)
                *_activeCancellation = true;
            _pending.clear();
        }
        _changed.notify_one();
        if (_worker.joinable())
            _worker.join();
    }

    uint64_t GuestThoughtService::subscribe()
    {
        std::lock_guard lock(_mutex);
        const auto ticket = ++_nextTicket;
        _states.emplace(ticket, State{});
        return ticket;
    }

    void GuestThoughtService::update(uint64_t ticket, GuestNarrativeSnapshot snapshot)
    {
        std::lock_guard lock(_mutex);
        const auto it = _states.find(ticket);
        if (it == _states.end())
            return;
        auto& state = it->second;
        const auto key = snapshot.semanticKey();
        if (state.revision != 0 && state.key == key)
            return;
        state.key = key;
        state.snapshot = std::move(snapshot);
        ++state.revision;
        state.result = {};
        if (_activeTicket == ticket && _activeCancellation)
            *_activeCancellation = true;
        if (std::find(_pending.begin(), _pending.end(), ticket) == _pending.end())
        {
            if (_pending.size() >= 32)
            {
                state.result.status = Status::unavailable;
                LOG_WARNING("Guest thought queue is full.");
                return;
            }
            _pending.push_back(ticket);
        }
        if (!_worker.joinable())
            _worker = std::thread(&GuestThoughtService::run, this);
        _changed.notify_one();
    }

    GuestThoughtService::Result GuestThoughtService::read(uint64_t ticket)
    {
        std::lock_guard lock(_mutex);
        const auto it = _states.find(ticket);
        return it == _states.end() ? Result{ Status::unavailable, {} } : it->second.result;
    }

    void GuestThoughtService::release(uint64_t ticket)
    {
        std::lock_guard lock(_mutex);
        _states.erase(ticket);
        _pending.erase(std::remove(_pending.begin(), _pending.end(), ticket), _pending.end());
        if (_activeTicket == ticket && _activeCancellation)
            *_activeCancellation = true;
        _changed.notify_one();
    }

    void GuestThoughtService::reset()
    {
        std::lock_guard lock(_mutex);
        _states.clear();
        _pending.clear();
        if (_activeCancellation)
            *_activeCancellation = true;
        _changed.notify_one();
    }

    void GuestThoughtService::run()
    {
        std::unique_ptr<IGuestThoughtBackend> backend;
        std::unique_lock lock(_mutex);
        while (!_stop)
        {
            if (_pending.empty())
            {
                _changed.wait(lock, [&] { return _stop || !_pending.empty(); });
                continue;
            }
            const auto now = std::chrono::steady_clock::now();
            auto eligible = _pending.end();
            auto wake = std::chrono::steady_clock::time_point::max();
            for (auto it = _pending.begin(); it != _pending.end(); ++it)
            {
                const auto next = _states.at(*it).nextAllowed;
                if (next <= now)
                {
                    eligible = it;
                    break;
                }
                wake = std::min(wake, next);
            }
            if (eligible == _pending.end())
            {
                _changed.wait_until(lock, wake);
                continue;
            }
            const auto ticket = *eligible;
            _pending.erase(eligible);
            auto& state = _states.at(ticket);
            const auto revision = state.revision;
            const auto snapshot = state.snapshot;
            state.nextAllowed = now + _cooldown;
            _activeTicket = ticket;
            auto cancelled = std::make_shared<std::atomic_bool>(false);
            _activeCancellation = cancelled;
            lock.unlock();
            Result result;
            try
            {
                if (!backend)
                    backend = _factory();
                if (!backend)
                    throw std::runtime_error("Guest thought backend was not created.");
                const auto raw = backend->generate(snapshot, *cancelled);
                if (!*cancelled)
                {
                    auto compact = CompactGuestNarrative(raw);
                    if (!compact || !IsGuestNarrativeGrounded(snapshot, *compact))
                        throw std::runtime_error("Generated guest thought was empty, invalid, or unrelated to its state.");
                    result = { Status::ready, std::move(*compact) };
                }
            }
            catch (const std::exception& e)
            {
                result.status = Status::unavailable;
                if (!*cancelled)
                    LOG_ERROR("Guest thoughts: %s", e.what());
            }
            lock.lock();
            const auto current = _states.find(ticket);
            if (!*cancelled && current != _states.end() && current->second.revision == revision)
                current->second.result = std::move(result);
            _activeTicket = 0;
            _activeCancellation.reset();
        }
        lock.unlock();
        backend.reset();
    }
} // namespace OpenRCT2
