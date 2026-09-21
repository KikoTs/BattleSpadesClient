#include "battlespades/core/deferred_cleanup.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}
}

int main() {
    using battlespades::core::DeferredCleanupQueue;
    using namespace std::chrono_literals;
    try {
        DeferredCleanupQueue queue{2U};
        auto first = queue.try_reserve();
        auto second = queue.try_reserve();
        expect(first && second && !queue.try_reserve(), "Live resources must reserve bounded cleanup capacity");
        std::promise<void> started;
        std::promise<void> release;
        const auto released = release.get_future().share();
        const auto ui_thread = std::this_thread::get_id();
        std::atomic_bool ran_off_ui{};
        std::atomic_bool completed{};
        first->defer(std::packaged_task<void()>{[&] {
            ran_off_ui = std::this_thread::get_id() != ui_thread;
            started.set_value();
            // Bounded wait prevents a broken inline implementation hanging CI.
            static_cast<void>(released.wait_for(2s));
            completed = true;
        }});
        const auto observed_start = started.get_future().wait_for(2s);
        const bool returned_while_cleanup_blocked = !completed.load();
        const bool capacity_still_reserved = !queue.try_reserve();
        release.set_value();
        queue.drain();
        expect(observed_start == std::future_status::ready && ran_off_ui && returned_while_cleanup_blocked,
               "Menu cancellation must return while shutdown is still blocked on another thread");
        expect(capacity_still_reserved, "Queued cleanup must retain its live-resource reservation");
        first = queue.try_reserve();
        expect(first.has_value(), "Completed cleanup must release capacity");
        std::atomic_uint completions{};
        first->defer(std::packaged_task<void()>{[] { throw std::runtime_error{"failed external cleanup"}; }});
        second->defer(std::packaged_task<void()>{[&] { ++completions; }});
        queue.drain();
        expect(completions == 1U, "A cleanup exception must not strand later jobs");
        for (unsigned attempt = 0; attempt < 200U; ++attempt) {
            auto ticket = queue.try_reserve();
            expect(ticket.has_value(), "Repeated host attempts must not leak reservations");
            if (attempt % 2U == 0U) ticket->defer(std::packaged_task<void()>{[&] { ++completions; }});
            else ticket->reset();
            queue.drain();
        }
        expect(completions == 101U, "Each deferred cleanup must run once");
        std::atomic_bool drained{};
        {
            DeferredCleanupQueue retiring{1U};
            auto ticket = retiring.try_reserve();
            ticket->defer(std::packaged_task<void()>{[&] { drained = true; }});
        }
        expect(drained, "Application destruction must drain owned cleanup jobs");
        {
            DeferredCleanupQueue moving{2U};
            auto source = moving.try_reserve();
            auto destination = moving.try_reserve();
            DeferredCleanupQueue::Reservation moved{std::move(*source)};
            expect(!*source && moved && !moving.try_reserve(), "Move construction must transfer exactly one reservation");
            source.reset();
            expect(!moving.try_reserve(), "Destroying a moved-from optional must not release live capacity");
            moved = std::move(*destination);
            expect(!*destination && moved, "Move assignment must empty the source reservation");
            auto released_slot = moving.try_reserve();
            expect(released_slot && !moving.try_reserve(), "Move assignment must release only the overwritten slot");
            destination.reset();
            expect(!moving.try_reserve(), "Destroying a moved-from assigned source released capacity twice");
            moved.reset();
            auto final_slot = moving.try_reserve();
            expect(final_slot && !moving.try_reserve(), "Moved reservation must release its remaining slot exactly once");
        }
        {
            DeferredCleanupQueue capture_queue{1U};
            std::promise<void> destructor_started;
            std::promise<void> destructor_release;
            std::atomic_bool capture_destroyed{};
            struct Resource final {
                std::promise<void>& started;
                std::shared_future<void> release;
                std::atomic_bool& destroyed;
                ~Resource() {
                    started.set_value();
                    static_cast<void>(release.wait_for(2s));
                    destroyed = true;
                }
            };
            auto ticket = capture_queue.try_reserve();
            auto resource = std::unique_ptr<Resource>{new Resource{
                destructor_started, destructor_release.get_future().share(), capture_destroyed}};
            ticket->defer(std::packaged_task<void()>{[resource = std::move(resource)] { static_cast<void>(resource); }});
            const auto destruction_observed = destructor_started.get_future().wait_for(2s);
            const bool reserved_during_destruction = !capture_queue.try_reserve();
            destructor_release.set_value();
            capture_queue.drain();
            expect(destruction_observed == std::future_status::ready && reserved_during_destruction,
                   "Returning from cleanup must not admit another host before resource captures are destroyed");
            expect(capture_destroyed && capture_queue.try_reserve().has_value(),
                   "Finishing capture destruction must release capacity");
            auto empty = capture_queue.try_reserve();
            empty->defer({});
            expect(!*empty && capture_queue.try_reserve().has_value(), "Empty cleanup must release its reservation safely");
        }
        {
            std::optional<DeferredCleanupQueue::Reservation> survivor;
            {
                DeferredCleanupQueue shutting_down{1U};
                survivor = shutting_down.try_reserve();
                expect(survivor.has_value(), "Shutdown survivor must hold a reservation");
            }
            // A surviving resource can retire after application queue teardown.
            // This documented shutdown-only fallback executes inline.
            bool ran_inline{};
            survivor->defer(std::packaged_task<void()>{[&] { ran_inline = std::this_thread::get_id() == ui_thread; }});
            expect(ran_inline && !*survivor, "A reservation outliving its queue must clean up safely inline");
            survivor.reset();
        }
        std::cout << "deferred host cleanup: nonblocking cancel, bounded admission, 200 cycles, exception recovery, moves and capture lifetimes passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
