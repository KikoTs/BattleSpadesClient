#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace battlespades::updater {

/**
 * Self-contained FIPS 180-4 SHA-256.
 *
 * The launcher is linked against the static CRT and system DLLs only, so it
 * can replace every DLL of the client folder during an update. It therefore
 * cannot use libsodium.dll; this small implementation is also portable, which
 * keeps the verification path unit-testable on every platform.
 */
class Sha256 {
public:
    Sha256() noexcept;
    void update(std::span<const std::uint8_t> bytes) noexcept;
    void update(std::string_view text) noexcept;
    [[nodiscard]] std::array<std::uint8_t, 32> finish() noexcept;

private:
    void transform(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t length_{};
    std::size_t buffered_{};
};

[[nodiscard]] std::string to_hex(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::string sha256_hex(std::string_view data);
/// Lower-case hex digest of a file, or nullopt with `error` set.
[[nodiscard]] std::optional<std::string> sha256_hex_file(const std::filesystem::path& file,
                                                         std::string& error);
/// Case-insensitive comparison of two 64-digit hex digests.
[[nodiscard]] bool same_sha256(std::string_view left, std::string_view right) noexcept;
[[nodiscard]] bool is_sha256_hex(std::string_view text) noexcept;

} // namespace battlespades::updater
