/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>
#include <openrct2/peep/GuestThoughtService.h>
#include <stdexcept>

using namespace OpenRCT2;
using namespace std::chrono_literals;

namespace
{
    struct BackendState
    {
        std::atomic_int calls{};
        std::atomic_int cancellations{};
        std::atomic_bool block{};
        std::thread::id thread;
    };

    class FakeBackend final : public IGuestThoughtBackend
    {
        std::shared_ptr<BackendState> _state;

    public:
        explicit FakeBackend(std::shared_ptr<BackendState> state)
            : _state(std::move(state))
        {
        }
        std::string generate(const GuestNarrativeSnapshot& snapshot, const std::atomic_bool& cancelled) override
        {
            _state->thread = std::this_thread::get_id();
            ++_state->calls;
            while (_state->block && !cancelled)
                std::this_thread::sleep_for(1ms);
            if (cancelled)
            {
                ++_state->cancellations;
                return {};
            }
            return snapshot.hunger <= 10 ? "I need food." : "I am happy.";
        }
    };

    template<typename Predicate>
    bool Wait(Predicate predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (!predicate() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
        return predicate();
    }

    GuestNarrativeSnapshot Snapshot()
    {
        GuestNarrativeSnapshot snapshot;
        snapshot.happiness = 200;
        snapshot.energy = 100;
        snapshot.hunger = 200;
        snapshot.thirst = 200;
        return snapshot;
    }
} // namespace

TEST(GuestThoughtService, LazyWorkerAndSemanticDeduplication)
{
    auto state = std::make_shared<BackendState>();
    GuestThoughtService service([state] { return std::make_unique<FakeBackend>(state); }, 0ms);
    const auto ticket = service.subscribe();
    EXPECT_EQ(state->calls, 0);
    auto snapshot = Snapshot();
    service.update(ticket, snapshot);
    ASSERT_TRUE(Wait([&] { return service.read(ticket).status == GuestThoughtService::Status::ready; }));
    EXPECT_EQ(service.read(ticket).text, "I am happy.");
    EXPECT_NE(state->thread, std::this_thread::get_id());
    --snapshot.hunger;
    service.update(ticket, snapshot);
    EXPECT_EQ(state->calls, 1);
    EXPECT_EQ(service.read(ticket).status, GuestThoughtService::Status::ready);
}

TEST(GuestThoughtService, SupersededResultCannotLeakToNewState)
{
    auto state = std::make_shared<BackendState>();
    state->block = true;
    GuestThoughtService service([state] { return std::make_unique<FakeBackend>(state); }, 0ms);
    const auto ticket = service.subscribe();
    auto snapshot = Snapshot();
    service.update(ticket, snapshot);
    ASSERT_TRUE(Wait([&] { return state->calls == 1; }));
    snapshot.hunger = 8;
    service.update(ticket, snapshot);
    EXPECT_EQ(service.read(ticket).status, GuestThoughtService::Status::pending);
    ASSERT_TRUE(Wait([&] { return state->cancellations == 1; }));
    state->block = false;
    ASSERT_TRUE(Wait([&] { return service.read(ticket).status == GuestThoughtService::Status::ready; }));
    EXPECT_EQ(service.read(ticket).text, "I need food.");
}

TEST(GuestThoughtService, ReleaseAndResetInvalidateTickets)
{
    auto state = std::make_shared<BackendState>();
    state->block = true;
    GuestThoughtService service([state] { return std::make_unique<FakeBackend>(state); }, 0ms);
    const auto first = service.subscribe();
    service.update(first, Snapshot());
    ASSERT_TRUE(Wait([&] { return state->calls == 1; }));
    service.release(first);
    const auto second = service.subscribe();
    EXPECT_NE(first, second);
    service.update(second, Snapshot());
    ASSERT_TRUE(Wait([&] { return state->cancellations == 1; }));
    service.reset();
    EXPECT_EQ(service.read(first).status, GuestThoughtService::Status::unavailable);
    EXPECT_EQ(service.read(second).status, GuestThoughtService::Status::unavailable);
    service.update(second, Snapshot());
}

TEST(GuestThoughtService, BoundedPendingQueueAndMultipleWindows)
{
    auto state = std::make_shared<BackendState>();
    state->block = true;
    GuestThoughtService service([state] { return std::make_unique<FakeBackend>(state); }, 0ms);
    const auto active = service.subscribe();
    service.update(active, Snapshot());
    ASSERT_TRUE(Wait([&] { return state->calls == 1; }));
    std::vector<uint64_t> tickets;
    for (int i = 0; i < 33; ++i)
    {
        const auto ticket = service.subscribe();
        tickets.push_back(ticket);
        service.update(ticket, Snapshot());
    }
    EXPECT_EQ(service.read(tickets.back()).status, GuestThoughtService::Status::unavailable);
    state->block = false;
    ASSERT_TRUE(Wait([&] { return service.read(tickets[31]).status == GuestThoughtService::Status::ready; }));
    EXPECT_EQ(state->calls, 33);
}

TEST(GuestThoughtService, CooldownCoalescesAndDoesNotRegenerateUnchangedState)
{
    auto state = std::make_shared<BackendState>();
    GuestThoughtService service([state] { return std::make_unique<FakeBackend>(state); }, 100ms);
    const auto ticket = service.subscribe();
    auto snapshot = Snapshot();
    service.update(ticket, snapshot);
    ASSERT_TRUE(Wait([&] { return service.read(ticket).status == GuestThoughtService::Status::ready; }));
    snapshot.hunger = 8;
    service.update(ticket, snapshot);
    snapshot.hunger = 200;
    service.update(ticket, snapshot);
    ASSERT_TRUE(Wait([&] { return service.read(ticket).status == GuestThoughtService::Status::ready; }));
    EXPECT_EQ(state->calls, 2);
    EXPECT_EQ(service.read(ticket).text, "I am happy.");
}

TEST(GuestThoughtService, ShutdownCancelsWithoutDraining)
{
    auto state = std::make_shared<BackendState>();
    state->block = true;
    {
        GuestThoughtService service([state] { return std::make_unique<FakeBackend>(state); }, 0ms);
        const auto ticket = service.subscribe();
        service.update(ticket, Snapshot());
        ASSERT_TRUE(Wait([&] { return state->calls == 1; }));
        service.update(service.subscribe(), Snapshot());
    }
    EXPECT_EQ(state->calls, 1);
    EXPECT_EQ(state->cancellations, 1);
}

TEST(GuestThoughtService, BackendFailureIsExplicit)
{
    GuestThoughtService service(
        []() -> std::unique_ptr<IGuestThoughtBackend> { throw std::runtime_error("test model missing"); }, 0ms);
    const auto ticket = service.subscribe();
    service.update(ticket, Snapshot());
    ASSERT_TRUE(Wait([&] { return service.read(ticket).status == GuestThoughtService::Status::unavailable; }));
    EXPECT_TRUE(service.read(ticket).text.empty());
}
