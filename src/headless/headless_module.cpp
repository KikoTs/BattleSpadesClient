#include "battlespades/headless/headless_module.hpp"

namespace battlespades::headless {

std::string_view HeadlessModule::name() const noexcept {
    return "headless";
}

bool HeadlessModule::start() {
    ticks_observed_ = 0U;
    running_ = true;
    return true;
}

core::TickDecision HeadlessModule::tick(const core::TickContext&) {
    ++ticks_observed_;
    return core::TickDecision::continue_running;
}

void HeadlessModule::stop() noexcept {
    running_ = false;
}

std::uint64_t HeadlessModule::ticks_observed() const noexcept {
    return ticks_observed_;
}

bool HeadlessModule::is_running() const noexcept {
    return running_;
}

} // namespace battlespades::headless
