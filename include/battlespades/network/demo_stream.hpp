#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace battlespades::network {

inline constexpr std::uint64_t demo_maximum_bytes{1024ULL * 1024ULL * 1024ULL};
inline constexpr std::uint32_t demo_maximum_packet_bytes{4U * 1024U * 1024U};
inline constexpr std::uint64_t demo_maximum_time_us{24ULL * 60ULL * 60ULL * 1'000'000ULL};

struct DemoPacket final {
    std::uint64_t time_us{};
    std::vector<std::byte> bytes;
};

/** Streaming, bounded server-to-client packet archive. No credentials or outgoing packets. */
class DemoWriter final {
public:
    DemoWriter() = default;
    ~DemoWriter();
    DemoWriter(const DemoWriter&) = delete;
    DemoWriter& operator=(const DemoWriter&) = delete;
    [[nodiscard]] bool open(const std::filesystem::path& path, std::uint16_t protocol);
    [[nodiscard]] bool append(std::uint64_t time_us, std::span<const std::byte> packet);
    [[nodiscard]] bool finish();
    [[nodiscard]] bool active() const noexcept { return file_ != nullptr && error_.empty(); }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
private:
    std::FILE* file_{};
    std::uint64_t written_{};
    std::uint64_t last_time_{};
    std::uint64_t flushed_time_{};
    std::string error_;
};

/** Also reads ZeroSpades/aos_replay v1 .dem streams (protocol 3 or 4). */
class DemoReader final {
public:
    [[nodiscard]] bool open(const std::filesystem::path& path);
    [[nodiscard]] std::optional<DemoPacket> next();
    [[nodiscard]] std::uint16_t protocol() const noexcept { return protocol_; }
    [[nodiscard]] bool finished() const noexcept { return finished_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
private:
    std::ifstream file_;
    std::uint64_t remaining_{};
    std::uint64_t last_time_{};
    std::uint16_t protocol_{};
    bool legacy_{};
    bool finished_{};
    std::string error_;
};

} // namespace battlespades::network
