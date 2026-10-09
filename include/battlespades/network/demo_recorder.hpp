#pragma once

#include "battlespades/network/enet_protocol168_client.hpp"

#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace battlespades::network {

struct DemoRecorderOptions final {
    EnetProtocol168Config transport;
    std::string name{"DemoRecorder"};
    std::string password;
    /** Zero means until interrupted/disconnected or the recording limit is reached. */
    std::uint32_t duration_seconds{};
    bool help{};
};

[[nodiscard]] std::optional<DemoRecorderOptions> parse_demo_recorder_options(
    std::span<const std::string_view> arguments, std::string& error);
[[nodiscard]] std::string_view demo_recorder_usage() noexcept;

struct DemoRecorderResult final {
    bool recorded{};
    std::size_t received_packets{};
    std::string error;
};

/** No window, rendering assets, identity service, or master-server lookup. */
[[nodiscard]] DemoRecorderResult run_demo_recorder(
    const DemoRecorderOptions& options,
    const std::function<bool()>& stop_requested,
    const std::function<void(std::string_view)>& report = {});

} // namespace battlespades::network
