#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace battlespades::updater {

/**
 * Minimal, self-contained ZIP reader (stored + deflate, RFC 1951).
 *
 * Windows' bundled tar.exe opens archives through the ANSI code page and
 * cannot open "C:\Users\Тодор\...\package.zip"; this extractor works on
 * std::filesystem::path (UTF-16 on Windows) throughout. Member names are
 * UTF-8 (general-purpose flag bit 11) or CP437-compatible ASCII. Every
 * member's CRC-32 is verified. Absolute names, drive letters, ".." and
 * symbolic links are refused before anything is written.
 */
using ExtractProgress = std::function<bool(std::uint64_t done, std::uint64_t total)>;   // false = cancel

bool extract_zip_archive(const std::filesystem::path& archive, const std::filesystem::path& destination,
                         std::string& error, const ExtractProgress& progress = {});

/// Raw DEFLATE decoder (exposed for tests).
bool inflate_raw(std::span<const std::uint8_t> input, std::vector<std::uint8_t>& output,
                 std::uint64_t expected_size, std::string& error);

} // namespace battlespades::updater
