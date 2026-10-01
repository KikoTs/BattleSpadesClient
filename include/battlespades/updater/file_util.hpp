#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::updater {

/// Steam files, manifests and JSON are UTF-8; std::filesystem::path(std::string)
/// would use the ANSI code page on Windows instead.
[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view text);
[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);
/// UTF-8 with forward slashes, for relative paths in journals and manifests.
[[nodiscard]] std::string generic_utf8(const std::filesystem::path& path);

[[nodiscard]] std::optional<std::string> read_text_file(const std::filesystem::path& file,
                                                        std::string& error);
[[nodiscard]] std::optional<std::vector<std::uint8_t>> read_binary_file(const std::filesystem::path& file,
                                                                        std::string& error);
/**
 * Writes beside the target and renames over it, so a crash never leaves a
 * half-written Steam file. The rename replaces the destination atomically on
 * NTFS (MoveFileExW with MOVEFILE_REPLACE_EXISTING).
 */
bool write_file_atomic(const std::filesystem::path& file, std::string_view bytes, std::string& error);

/// Copies `file` into `backup_dir` as "<stem>-<label><ext>", never overwriting.
[[nodiscard]] std::optional<std::filesystem::path>
backup_file(const std::filesystem::path& file, const std::filesystem::path& backup_dir,
            std::string_view label, std::string& error);

} // namespace battlespades::updater
