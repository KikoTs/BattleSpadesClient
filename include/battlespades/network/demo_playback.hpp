#pragma once

#include "battlespades/network/demo_stream.hpp"
#include "battlespades/network/live_protocol168_connection.hpp"

namespace battlespades::network {

struct DemoPlaybackUpdate final {
    std::unique_ptr<Protocol168WorldBootstrap> bootstrap;
    std::vector<std::vector<std::byte>> packets;
    std::shared_ptr<const Protocol168InitialInfo> initial_info;
    Protocol168LoadingProgress loading;
    std::uint64_t map_generation{};
    bool map_started{};
    bool finished{};
    std::string error;
};

/** Sequential spectator replay: rebuild maps and ordered authoritative packets. */
class DemoPlayback final {
public:
    DemoPlayback();
    ~DemoPlayback();
    [[nodiscard]] bool open(const std::filesystem::path& path);
    [[nodiscard]] DemoPlaybackUpdate advance(std::uint64_t time_us, std::size_t budget = 128U);
    [[nodiscard]] GameProtocol protocol() const noexcept;
    [[nodiscard]] const std::string& error() const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace battlespades::network
