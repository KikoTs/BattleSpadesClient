#include "battlespades/network/demo_stream.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <system_error>

namespace battlespades::network {
namespace {
constexpr std::array<char, 8U> magic{'B', 'S', 'D', 'E', 'M', 'O', 1, 0};

bool valid_protocol(std::uint16_t protocol) noexcept {
    return protocol == 3U || protocol == 4U || protocol == 168U;
}

template<typename T> void write_integer(std::FILE* stream, T value) {
    for (std::size_t i{}; i < sizeof(T); ++i)
        std::fputc(static_cast<int>((value >> (i * 8U)) & 255U), stream);
}

template<typename T> T read_integer(std::istream& stream) {
    std::uint64_t result{};
    for (std::size_t i{}; i < sizeof(T); ++i)
        result |= static_cast<std::uint64_t>(static_cast<unsigned char>(stream.get())) << (i * 8U);
    return static_cast<T>(result);
}
} // namespace

DemoWriter::~DemoWriter() { static_cast<void>(finish()); }

bool DemoWriter::open(const std::filesystem::path& path, std::uint16_t protocol) {
    if (file_ != nullptr) { error_ = "demo writer is already open"; return false; }
    error_.clear();
    if (!valid_protocol(protocol)) { error_ = "unsupported demo protocol"; return false; }
    // Exclusive creation is atomic even when two recorder processes choose
    // the same filename concurrently. Never truncate another recording.
#ifdef _WIN32
    static_cast<void>(_wfopen_s(&file_, path.c_str(), L"wbx"));
#else
    file_ = std::fopen(path.c_str(), "wbx");
#endif
    if (file_ == nullptr) { error_ = "demo destination already exists or cannot be created"; return false; }
    static_cast<void>(std::fwrite(magic.data(), 1U, magic.size(), file_));
    write_integer(file_, protocol);
    write_integer<std::uint16_t>(file_, 0U);
    written_ = 12U;
    last_time_ = 0U;
    flushed_time_ = 0U;
    if (std::ferror(file_) != 0) { error_ = "cannot write demo header"; return false; }
    return true;
}

bool DemoWriter::append(std::uint64_t time_us, std::span<const std::byte> packet) {
    if (!active()) return false;
    if (packet.empty() || packet.size() > demo_maximum_packet_bytes || time_us < last_time_) {
        error_ = "invalid demo packet size or timestamp";
        return false;
    }
    if (time_us > demo_maximum_time_us) {
        static_cast<void>(finish());
        error_ = "demo recording reached its 24-hour duration limit";
        return false;
    }
    // Reserve the explicit end marker even when the disk budget is reached.
    if (written_ + 24U + packet.size() > demo_maximum_bytes) {
        static_cast<void>(finish());
        error_ = "demo recording reached its 1 GiB size limit";
        return false;
    }
    write_integer(file_, time_us);
    write_integer(file_, static_cast<std::uint32_t>(packet.size()));
    static_cast<void>(std::fwrite(packet.data(), 1U, packet.size(), file_));
    written_ += 12U + packet.size();
    last_time_ = time_us;
    if (time_us - flushed_time_ >= 1'000'000U) {
        if (std::fflush(file_) != 0) error_ = "cannot flush demo packet (disk full or I/O error)";
        flushed_time_ = time_us;
    }
    if (std::ferror(file_) != 0) error_ = "cannot write demo packet (disk full or I/O error)";
    return error_.empty();
}

bool DemoWriter::finish() {
    if (file_ == nullptr) return error_.empty();
    if (error_.empty()) {
        write_integer(file_, last_time_);
        write_integer<std::uint32_t>(file_, 0U);
        if (std::fflush(file_) != 0 || std::ferror(file_) != 0) error_ = "cannot finish demo file";
    }
    if (std::fclose(file_) != 0 && error_.empty()) error_ = "cannot close demo file";
    file_ = nullptr;
    return error_.empty();
}

bool DemoReader::open(const std::filesystem::path& path) {
    file_.close();
    file_.clear();
    error_.clear();
    finished_ = false;
    last_time_ = 0U;
    protocol_ = 0U;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        error_ = "demo must be an accessible regular file";
        return false;
    }
    remaining_ = std::filesystem::file_size(path, ec);
    if (ec || remaining_ < 2U || remaining_ > demo_maximum_bytes) {
        error_ = "demo file is empty, inaccessible, or larger than 1 GiB";
        return false;
    }
    file_.open(path, std::ios::binary);
    if (!file_) { error_ = "cannot open demo file"; return false; }
    const auto first = read_integer<std::uint8_t>(file_);
    legacy_ = first == 1U;
    if (legacy_) {
        protocol_ = read_integer<std::uint8_t>(file_);
        remaining_ -= 2U;
        if (protocol_ != 3U && protocol_ != 4U) error_ = "unsupported legacy demo protocol";
    } else {
        if (remaining_ < 24U) { error_ = "truncated demo header or end marker"; return false; }
        file_.seekg(0);
        std::array<char, 8U> header{};
        file_.read(header.data(), static_cast<std::streamsize>(header.size()));
        protocol_ = read_integer<std::uint16_t>(file_);
        const auto flags = read_integer<std::uint16_t>(file_);
        remaining_ -= 12U;
        if (header != magic || !valid_protocol(protocol_) || flags != 0U)
            error_ = "unsupported demo version, protocol, or flags";
    }
    if (!file_ && error_.empty()) error_ = "cannot read demo header";
    return error_.empty();
}

std::optional<DemoPacket> DemoReader::next() {
    if (!error_.empty() || finished_ || !file_.is_open()) return std::nullopt;
    if (remaining_ == 0U && legacy_) { finished_ = true; return std::nullopt; }
    const auto header_bytes = legacy_ ? 6U : 12U;
    if (remaining_ < header_bytes) { error_ = "truncated demo record or missing end marker"; return std::nullopt; }
    DemoPacket packet;
    std::uint32_t size{};
    if (legacy_) {
        const auto seconds = std::bit_cast<float>(read_integer<std::uint32_t>(file_));
        if (!std::isfinite(seconds) || seconds < 0.0F || seconds > 86'400.0F) {
            error_ = "invalid legacy demo timestamp";
            return std::nullopt;
        }
        packet.time_us = static_cast<std::uint64_t>(static_cast<double>(seconds) * 1'000'000.0);
        size = read_integer<std::uint16_t>(file_);
    } else {
        packet.time_us = read_integer<std::uint64_t>(file_);
        size = read_integer<std::uint32_t>(file_);
    }
    remaining_ -= header_bytes;
    if (!file_ || packet.time_us < last_time_ || packet.time_us > demo_maximum_time_us ||
        size > demo_maximum_packet_bytes || size > remaining_) {
        error_ = "invalid or truncated demo record";
        return std::nullopt;
    }
    if (size == 0U) {
        if (legacy_ || remaining_ != 0U) error_ = "invalid demo end marker";
        else finished_ = true;
        return std::nullopt;
    }
    packet.bytes.resize(size);
    file_.read(reinterpret_cast<char*>(packet.bytes.data()), static_cast<std::streamsize>(size));
    if (!file_) { error_ = "cannot read demo payload"; return std::nullopt; }
    remaining_ -= size;
    last_time_ = packet.time_us;
    return packet;
}
} // namespace battlespades::network
