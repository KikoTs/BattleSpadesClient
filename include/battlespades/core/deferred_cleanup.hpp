#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace battlespades::core {

/** Reserve before creating a resource; retire it without blocking the UI.
 * Capacity covers live resources AND queued cleanup, so repeated start/cancel
 * cannot accumulate unbounded processes, cleanup jobs, or detached threads.
 */
class DeferredCleanupQueue final {
    struct State final {
        std::mutex mutex;
        std::condition_variable wake;
        std::deque<std::packaged_task<void()>> jobs;
        std::size_t reservations{};
        bool active{};
        bool closing{};
    };

public:
    class Reservation final {
    public:
        Reservation() = default;
        ~Reservation() { reset(); }
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;
        Reservation(Reservation&& other) noexcept : state_{std::move(other.state_)} {}
        Reservation& operator=(Reservation&& other) noexcept {
            if (this != &other) { reset(); state_ = std::move(other.state_); }
            return *this;
        }
        [[nodiscard]] explicit operator bool() const noexcept { return state_ != nullptr; }

        void reset() noexcept {
            if (!state_) return;
            const auto state = std::move(state_);
            std::lock_guard lock{state->mutex};
            --state->reservations;
            state->wake.notify_all();
        }

        /** A stopped queue drains inline; only application teardown may stop it. */
        void defer(std::packaged_task<void()> cleanup) {
            if (!cleanup.valid()) { reset(); return; }
            if (!state_) { cleanup(); return; }
            const auto state = state_;
            {
                std::lock_guard lock{state->mutex};
                if (!state->closing) {
                    state->jobs.push_back(std::move(cleanup));
                    state_.reset(); // The queued job now owns the reservation.
                    state->wake.notify_all();
                    return;
                }
            }
            cleanup();
            cleanup = {}; // Destroy resource captures before releasing capacity.
            reset();
        }

    private:
        friend class DeferredCleanupQueue;
        explicit Reservation(std::shared_ptr<State> state) : state_{std::move(state)} {}
        std::shared_ptr<State> state_;
    };

    explicit DeferredCleanupQueue(std::size_t capacity = 2U)
        : state_{std::make_shared<State>()}, capacity_{capacity},
          worker_{[state = state_] {
              std::unique_lock lock{state->mutex};
              for (;;) {
                  state->wake.wait(lock, [&] { return state->closing || !state->jobs.empty(); });
                  if (state->jobs.empty()) return;
                  auto task = std::move(state->jobs.front());
                  state->jobs.pop_front();
                  state->active = true;
                  lock.unlock();
                  // packaged_task contains callback exceptions, keeping later
                  // cleanup alive even if one external operation throws.
                  task();
                  task = {};
                  lock.lock();
                  --state->reservations;
                  state->active = false;
                  state->wake.notify_all();
              }
          }} {}

    ~DeferredCleanupQueue() {
        {
            std::lock_guard lock{state_->mutex};
            state_->closing = true;
        }
        state_->wake.notify_all();
        worker_.join();
    }
    DeferredCleanupQueue(const DeferredCleanupQueue&) = delete;
    DeferredCleanupQueue& operator=(const DeferredCleanupQueue&) = delete;

    [[nodiscard]] std::optional<Reservation> try_reserve() {
        std::lock_guard lock{state_->mutex};
        if (state_->closing || state_->reservations >= capacity_) return std::nullopt;
        ++state_->reservations;
        return Reservation{state_};
    }

    /** Application shutdown only; menu cancellation never waits here. */
    void drain() {
        std::unique_lock lock{state_->mutex};
        state_->wake.wait(lock, [&] { return state_->jobs.empty() && !state_->active; });
    }

private:
    std::shared_ptr<State> state_;
    std::size_t capacity_;
    std::thread worker_;
};

} // namespace battlespades::core
