#include "battlespades/assets/asset_install.hpp"

#include <nlohmann/json.hpp>
#include <sodium.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <ranges>
#include <set>
#include <sstream>
#include <string_view>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace battlespades::assets {
namespace {

constexpr std::size_t maximum_manifest_files{100'000U};
constexpr std::size_t hash_buffer_size{1U << 20U};
constexpr int installer_cancel_exit_code{2};
constexpr std::size_t maximum_discovery_children{64U};
constexpr std::size_t maximum_reported_candidates{8U};

// Steam language depots do not always install these alongside an English
// copy. Import them only when the player's selected Windows installation has
// the exact recovered retail bytes; they remain optional and are never
// redistributed by this repository.
const std::array optional_language_fonts{
    AssetManifestEntry{
        "fonts/Gen_Shin_Gothic_Monospace_Bold.ttf",
        4'952'384U,
        "6c2e1490357ab477c7e9663244689dd365b2aaaffb9ef88a01c6bdaa99cc9250",
    },
    AssetManifestEntry{
        "fonts/NotoSansJP-SemiBold.ttf",
        5'726'852U,
        "4881d1b63b7300452b9385f69a70d00c3607ff0728c35a0c5ce99705b97c2b2a",
    },
};

/// UTF-8 for messages and comparisons. path::string() converts through the
/// ANSI code page and throws for names outside it (e.g. a Cyrillic Steam
/// library under a Western code page), which aborted source discovery.
[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

[[nodiscard]] std::string lowercase_ascii(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] std::optional<std::filesystem::path>
environment_path(const char* name) {
#if defined(_WIN32)
    // Wide API: the ANSI copy cannot hold characters outside the code page.
    const std::wstring wide_name(name, name + std::strlen(name));
    wchar_t* value{};
    std::size_t size{};
    if (_wdupenv_s(&value, &size, wide_name.c_str()) != 0 || value == nullptr ||
        *value == L'\0') {
        std::free(value);
        return std::nullopt;
    }
    std::filesystem::path result{std::wstring{value}};
    std::free(value);
    return result;
#else
    const auto* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::filesystem::path{value};
#endif
}

void append_standard_layouts(std::vector<std::filesystem::path>& candidates,
                             const std::filesystem::path& root) {
    candidates.push_back(root);
    candidates.push_back(root / "src");
    candidates.push_back(root / "client" / "src");
}

[[nodiscard]] bool contains_legacy_macos_bundle(
    const std::filesystem::path& path) {
    return std::ranges::any_of(path, [](const std::filesystem::path& component) {
        return lowercase_ascii(path_text(component.extension())) == ".app";
    });
}

[[nodiscard]] bool resembles_windows_install_name(std::string_view value) {
    std::string compact;
    compact.reserve(value.size());
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0) {
            compact.push_back(static_cast<char>(std::tolower(byte)));
        }
    }
    return compact.find("aceofspades") != std::string::npos ||
           compact.starts_with("aos");
}

void append_steam_layouts(std::vector<std::filesystem::path>& candidates,
                          const std::filesystem::path& root) {
    constexpr std::array install_names{
        std::string_view{"aceofspades"},
        std::string_view{"Ace of Spades"},
        std::string_view{"Ace of Spades Battle Builder"},
    };
    for (const auto name : install_names) {
        const auto install = std::filesystem::path{name};
        append_standard_layouts(candidates, root / install);
        append_standard_layouts(
            candidates, root / "steamapps" / "common" / install);
        append_standard_layouts(
            candidates, root / "Steam" / "steamapps" / "common" / install);
    }
}

[[nodiscard]] bool valid_hash(std::string_view value) noexcept {
    return value.size() == crypto_hash_sha256_BYTES * 2U &&
           std::ranges::all_of(value, [](unsigned char character) {
               return std::isxdigit(character) != 0;
           });
}

[[nodiscard]] bool safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    return std::ranges::all_of(path, [](const std::filesystem::path& component) {
        return component != "." && component != ".." && !component.empty();
    });
}

[[nodiscard]] bool path_below(const std::filesystem::path& root,
                              const std::filesystem::path& candidate) {
    auto root_iterator = root.begin();
    auto candidate_iterator = candidate.begin();
    for (; root_iterator != root.end() && candidate_iterator != candidate.end();
         ++root_iterator, ++candidate_iterator) {
#if defined(_WIN32)
        // Wide, case-insensitive ordinal comparison (NTFS semantics).
        // component.string() went through the ANSI code page and threw for
        // e.g. a Cyrillic Steam library, failing every asset check.
        const auto& left = root_iterator->native();
        const auto& right = candidate_iterator->native();
        if (CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()), right.c_str(),
                                 static_cast<int>(right.size()), TRUE) != CSTR_EQUAL) {
            return false;
        }
#else
        if (*root_iterator != *candidate_iterator) {
            return false;
        }
#endif
    }
    return root_iterator == root.end() && candidate_iterator != candidate.end();
}

[[nodiscard]] std::string hash_to_hex(
    const std::array<unsigned char, crypto_hash_sha256_BYTES>& hash) {
    constexpr std::array hex_digits{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    std::string result(hash.size() * 2U, '0');
    for (std::size_t index = 0; index < hash.size(); ++index) {
        const auto byte = hash[index];
        result[index * 2U] = hex_digits[byte >> 4U];
        result[index * 2U + 1U] = hex_digits[byte & 0x0FU];
    }
    return result;
}

[[nodiscard]] std::optional<std::string>
sha256_file(const std::filesystem::path& path, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "cannot open asset for hashing: " + path_text(path);
        return std::nullopt;
    }

    crypto_hash_sha256_state state{};
    if (crypto_hash_sha256_init(&state) != 0) {
        error = "could not initialize SHA-256";
        return std::nullopt;
    }
    std::vector<char> buffer(hash_buffer_size);
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = stream.gcount();
        if (count > 0 &&
            crypto_hash_sha256_update(
                &state,
                reinterpret_cast<const unsigned char*>(buffer.data()),
                static_cast<unsigned long long>(count)) != 0) {
            error = "could not update SHA-256 for: " + path_text(path);
            return std::nullopt;
        }
    }
    if (stream.bad()) {
        error = "failed while reading asset: " + path_text(path);
        return std::nullopt;
    }

    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    if (crypto_hash_sha256_final(&state, digest.data()) != 0) {
        error = "could not finalize SHA-256 for: " + path_text(path);
        return std::nullopt;
    }
    error.clear();
    return hash_to_hex(digest);
}

[[nodiscard]] std::filesystem::path unique_sibling(const std::filesystem::path& target,
                                                   std::string_view label) {
    static std::atomic<std::uint64_t> sequence{};
    const auto clock = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto suffix = std::to_string(clock) + "-" +
                        std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed));
    return target.parent_path() /
           (target.filename().native() + std::filesystem::path{"." + std::string{label} + "-" + suffix}.native());
}

[[nodiscard]] bool copy_and_hash(const std::filesystem::path& source,
                                 const std::filesystem::path& target,
                                 const AssetManifestEntry& entry,
                                 std::string& error) {
    std::error_code code;
    const auto source_status = std::filesystem::symlink_status(source, code);
    if (code || std::filesystem::is_symlink(source_status) ||
        !std::filesystem::is_regular_file(source_status)) {
        error = "source asset is missing, redirected, or not a file: " + path_text(source);
        return false;
    }

    std::filesystem::create_directories(target.parent_path(), code);
    if (code) {
        error = "cannot create asset destination directory: " + code.message();
        return false;
    }

    std::ifstream input(source, std::ios::binary);
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    if (!input || !output) {
        error = "cannot open asset copy pair: " + path_text(source) + " -> " + path_text(target);
        return false;
    }

    crypto_hash_sha256_state state{};
    if (crypto_hash_sha256_init(&state) != 0) {
        error = "could not initialize SHA-256 while copying assets";
        return false;
    }
    std::vector<char> buffer(hash_buffer_size);
    std::uint64_t copied{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count <= 0) {
            continue;
        }
        const auto unsigned_count = static_cast<std::uint64_t>(count);
        if (copied > std::numeric_limits<std::uint64_t>::max() - unsigned_count) {
            error = "asset byte count overflow while copying: " + path_text(source);
            return false;
        }
        copied += unsigned_count;
        output.write(buffer.data(), count);
        if (!output ||
            crypto_hash_sha256_update(
                &state,
                reinterpret_cast<const unsigned char*>(buffer.data()),
                static_cast<unsigned long long>(count)) != 0) {
            error = "failed while copying asset: " + path_text(source);
            return false;
        }
    }
    if (input.bad()) {
        error = "failed while reading source asset: " + path_text(source);
        return false;
    }
    output.flush();
    if (!output) {
        error = "failed while flushing copied asset: " + path_text(target);
        return false;
    }

    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    if (crypto_hash_sha256_final(&state, digest.data()) != 0 || copied != entry.size ||
        hash_to_hex(digest) != entry.sha256) {
        error = "source asset does not match the required retail version: " + path_text(source);
        return false;
    }
    return true;
}

#if defined(_WIN32)
[[nodiscard]] std::wstring quote_windows_argument(const std::filesystem::path& path) {
    std::wstring result{L"\""};
    std::size_t backslashes{};
    for (const wchar_t character : path.wstring()) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(character);
            backslashes = 0U;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0U;
        result.push_back(character);
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'\"');
    return result;
}
#endif

} // namespace

AssetManifestLoadResult
load_asset_manifest(const std::filesystem::path& manifest_path) noexcept {
    try {
        std::ifstream stream(manifest_path, std::ios::binary);
        if (!stream) {
            return {std::nullopt, "cannot open asset manifest: " + path_text(manifest_path)};
        }
        nlohmann::json document;
        stream >> document;
        if (!document.is_object() || document.value("schema", 0) != 1 ||
            !document.contains("files") || !document["files"].is_array() ||
            !document.contains("file_count") || !document["file_count"].is_number_unsigned() ||
            !document.contains("total_bytes") || !document["total_bytes"].is_number_unsigned()) {
            return {std::nullopt, "asset manifest has an unsupported or malformed schema"};
        }

        const auto& files = document["files"];
        const auto declared_count = document["file_count"].get<std::uint64_t>();
        if (declared_count != files.size() || files.size() > maximum_manifest_files) {
            return {std::nullopt, "asset manifest file count is inconsistent"};
        }

        AssetManifest manifest;
        manifest.files.reserve(files.size());
        std::set<std::string> casefolded_paths;
        std::uint64_t total_bytes{};
        for (const auto& item : files) {
            if (!item.is_object() || !item.contains("path") || !item["path"].is_string() ||
                !item.contains("size") || !item["size"].is_number_unsigned() ||
                !item.contains("sha256") || !item["sha256"].is_string()) {
                return {std::nullopt, "asset manifest contains a malformed file entry"};
            }
            const auto relative_text = item["path"].get<std::string>();
            const auto relative_path = std::filesystem::path{relative_text};
            if (relative_text.find('\\') != std::string::npos ||
                !safe_relative_path(relative_path)) {
                return {std::nullopt, "asset manifest contains an unsafe path: " + relative_text};
            }
            if (!casefolded_paths.insert(lowercase_ascii(relative_text)).second) {
                return {std::nullopt,
                        "asset manifest contains a case-insensitive duplicate: " + relative_text};
            }
            const auto size = item["size"].get<std::uint64_t>();
            auto sha256 = lowercase_ascii(item["sha256"].get<std::string>());
            if (!valid_hash(sha256) || total_bytes >
                                           std::numeric_limits<std::uint64_t>::max() - size) {
                return {std::nullopt, "asset manifest contains invalid size or hash data"};
            }
            total_bytes += size;
            manifest.files.push_back(AssetManifestEntry{relative_path, size, std::move(sha256)});
        }
        if (total_bytes != document["total_bytes"].get<std::uint64_t>()) {
            return {std::nullopt, "asset manifest total byte count is inconsistent"};
        }
        manifest.total_bytes = total_bytes;
        return {std::move(manifest), {}};
    } catch (const std::exception& exception) {
        return {std::nullopt,
                std::string{"asset manifest loading failed: "} + exception.what()};
    } catch (...) {
        return {std::nullopt, "asset manifest loading failed with an unknown exception"};
    }
}

AssetTreeCheck verify_asset_tree(const std::filesystem::path& root,
                                 const AssetManifest& manifest,
                                 AssetVerificationDepth depth) noexcept {
    try {
        std::error_code code;
        const auto normalized_root = std::filesystem::weakly_canonical(root, code);
        if (code || !std::filesystem::is_directory(normalized_root, code) || code) {
            return {false, 0U, 0U, "asset root is missing or inaccessible: " + path_text(root)};
        }

        AssetTreeCheck result{true, 0U, 0U, {}};
        for (const auto& entry : manifest.files) {
            const auto target = normalized_root / entry.relative_path;
            const auto normalized_target = std::filesystem::weakly_canonical(target, code);
            if (code || !path_below(normalized_root, normalized_target)) {
                return {false,
                        result.files_checked,
                        result.bytes_checked,
                        "asset path is redirected outside the asset root: " + path_text(target)};
            }
            const auto status = std::filesystem::symlink_status(target, code);
            if (code || std::filesystem::is_symlink(status) ||
                !std::filesystem::is_regular_file(status)) {
                return {false,
                        result.files_checked,
                        result.bytes_checked,
                        "required asset is missing or redirected: " + path_text(target)};
            }
            const auto size = std::filesystem::file_size(target, code);
            if (code || size != entry.size) {
                return {false,
                        result.files_checked,
                        result.bytes_checked,
                        "required asset has the wrong size: " + path_text(target)};
            }
            if (depth == AssetVerificationDepth::full_hash) {
                std::string hash_error;
                const auto hash = sha256_file(target, hash_error);
                if (!hash.has_value() || *hash != entry.sha256) {
                    return {false,
                            result.files_checked,
                            result.bytes_checked,
                            hash.has_value()
                                ? "required asset has the wrong SHA-256: " + path_text(target)
                                : std::move(hash_error)};
                }
            }
            ++result.files_checked;
            result.bytes_checked += entry.size;
        }
        return result;
    } catch (const std::exception& exception) {
        return {false,
                0U,
                0U,
                std::string{"asset verification failed: "} + exception.what()};
    } catch (...) {
        return {false, 0U, 0U, "asset verification failed with an unknown exception"};
    }
}

std::optional<std::filesystem::path> find_asset_source(
    const std::filesystem::path& selected_directory,
    const AssetManifest& manifest,
    std::string& error) noexcept {
    try {
        if (contains_legacy_macos_bundle(selected_directory)) {
            error = "legacy macOS .app assets are intentionally unsupported; select a "
                    "Windows Ace of Spades Battle Builder installation copied to this Mac";
            return std::nullopt;
        }
        std::vector<std::filesystem::path> candidates;
        candidates.reserve(64U);
        append_standard_layouts(candidates, selected_directory);
        append_steam_layouts(candidates, selected_directory);

        // A Windows installation is commonly copied to macOS under a custom
        // folder such as AceOfSpades_no_steam_new. Probe only likely install
        // names at one bounded level; never inspect legacy .app bundles.
        std::error_code iteration_code;
        std::size_t visited{};
        std::filesystem::directory_iterator iterator{
            selected_directory,
            std::filesystem::directory_options::skip_permission_denied,
            iteration_code};
        const std::filesystem::directory_iterator end;
        while (!iteration_code && iterator != end &&
               visited < maximum_discovery_children) {
            const auto entry = *iterator;
            iterator.increment(iteration_code);
            ++visited;
            std::error_code status_code;
            const auto status = entry.symlink_status(status_code);
            if (status_code || std::filesystem::is_symlink(status) ||
                !std::filesystem::is_directory(status)) {
                continue;
            }
            if (!contains_legacy_macos_bundle(entry.path()) &&
                resembles_windows_install_name(path_text(entry.path().filename()))) {
                append_standard_layouts(candidates, entry.path());
            }
        }

        std::set<std::filesystem::path> attempted;
        std::ostringstream details;
        std::size_t failures{};
        for (const auto& candidate : candidates) {
            std::error_code code;
            const auto normalized = std::filesystem::weakly_canonical(candidate, code);
            if (code || !attempted.insert(normalized).second) {
                continue;
            }
            const auto check =
                verify_asset_tree(normalized, manifest, AssetVerificationDepth::metadata);
            if (check) {
                error.clear();
                return normalized;
            }
            if (failures < maximum_reported_candidates) {
                if (failures != 0U) {
                    details << "; ";
                }
                details << path_text(candidate) << ": " << check.error;
            }
            ++failures;
        }
        if (failures > maximum_reported_candidates) {
            details << "; " << (failures - maximum_reported_candidates)
                    << " additional layouts checked";
        }
        error = "the selected folder is not a matching Ace of Spades Battle Builder "
                "installation (" +
                details.str() + ")";
        return std::nullopt;
    } catch (const std::exception& exception) {
        error = std::string{"asset source discovery failed: "} + exception.what();
        return std::nullopt;
    } catch (...) {
        error = "asset source discovery failed with an unknown exception";
        return std::nullopt;
    }
}

std::optional<std::filesystem::path>
default_asset_source_directory() noexcept {
    try {
        std::vector<std::filesystem::path> candidates;
#if defined(_WIN32)
        for (const auto variable : {"ProgramFiles(x86)", "ProgramFiles"}) {
            if (const auto root = environment_path(variable); root.has_value()) {
                candidates.push_back(*root / "Steam" / "steamapps" / "common" /
                                     "aceofspades");
                candidates.push_back(*root / "Steam" / "steamapps" / "common" /
                                     "Ace of Spades");
            }
        }
#else
        if (const auto home = environment_path("HOME"); home.has_value()) {
#if defined(__APPLE__)
            candidates.push_back(*home / "Library" / "Application Support" /
                                 "Steam" / "steamapps" / "common" /
                                 "Ace of Spades");
            candidates.push_back(*home / "Library" / "Application Support" /
                                 "Steam" / "steamapps" / "common" /
                                 "aceofspades");
#else
            candidates.push_back(*home / ".local" / "share" / "Steam" /
                                 "steamapps" / "common" / "Ace of Spades");
            candidates.push_back(*home / ".steam" / "steam" / "steamapps" /
                                 "common" / "Ace of Spades");
#endif
        }
#endif
        for (const auto& candidate : candidates) {
            std::error_code code;
            if (std::filesystem::is_directory(candidate, code) && !code) {
                return candidate;
            }
        }
    } catch (...) {
    }
    return std::nullopt;
}

AssetInstallResult install_asset_tree_atomic(const std::filesystem::path& source_root,
                                             const std::filesystem::path& destination_root,
                                             const AssetManifest& manifest,
                                             AssetProgressCallback progress) noexcept {
    std::filesystem::path staging;
    std::filesystem::path backup;
    try {
        std::error_code code;
        const auto source = std::filesystem::weakly_canonical(source_root, code);
        if (code || !std::filesystem::is_directory(source, code) || code) {
            return {false, "asset source is missing or inaccessible: " + path_text(source_root)};
        }
        std::filesystem::create_directories(destination_root.parent_path(), code);
        if (code) {
            return {false, "cannot create asset destination parent: " + code.message()};
        }
        const auto destination_parent =
            std::filesystem::weakly_canonical(destination_root.parent_path(), code);
        if (code || destination_root.filename().empty()) {
            return {false, "asset destination is invalid or inaccessible"};
        }
        const auto destination = destination_parent / destination_root.filename();
        if (source == destination) {
            const auto existing =
                verify_asset_tree(destination, manifest, AssetVerificationDepth::full_hash);
            return existing ? AssetInstallResult{true, {}}
                            : AssetInstallResult{false, existing.error};
        }
        if (path_below(source, destination)) {
            return {false, "asset destination cannot be inside the selected source tree"};
        }

        staging = unique_sibling(destination, "installing");
        backup = unique_sibling(destination, "previous");
        std::filesystem::create_directory(staging, code);
        if (code) {
            return {false, "cannot create asset staging directory: " + code.message()};
        }

        AssetInstallProgress state{0U, manifest.files.size(), 0U, manifest.total_bytes};
        if (progress) {
            progress(state);
        }
        for (const auto& entry : manifest.files) {
            const auto source_file = source / entry.relative_path;
            const auto target_file = staging / entry.relative_path;
            const auto normalized_source = std::filesystem::weakly_canonical(source_file, code);
            if (code || !path_below(source, normalized_source)) {
                std::filesystem::remove_all(staging, code);
                return {false,
                        "source asset escapes through a redirected path: " +
                            path_text(source_file)};
            }
            std::string copy_error;
            if (!copy_and_hash(normalized_source, target_file, entry, copy_error)) {
                std::filesystem::remove_all(staging, code);
                return {false, std::move(copy_error)};
            }
            ++state.files_completed;
            state.bytes_completed += entry.size;
            if (progress) {
                progress(state);
            }
        }

        for (const auto& entry : optional_language_fonts) {
            const auto source_file = source / entry.relative_path;
            const auto status = std::filesystem::symlink_status(source_file, code);
            if (code || !std::filesystem::is_regular_file(status) ||
                std::filesystem::is_symlink(status)) {
                code.clear();
                continue;
            }
            const auto normalized_source = std::filesystem::weakly_canonical(source_file, code);
            if (code || !path_below(source, normalized_source)) {
                code.clear();
                continue;
            }
            std::string ignored_error;
            const auto target_file = staging / entry.relative_path;
            if (!copy_and_hash(normalized_source, target_file, entry, ignored_error)) {
                // A different depot/font revision must not poison a valid
                // base install or become executable input to FreeType.
                std::filesystem::remove(target_file, code);
                code.clear();
            }
        }

        const auto staged_check =
            verify_asset_tree(staging, manifest, AssetVerificationDepth::metadata);
        if (!staged_check) {
            std::filesystem::remove_all(staging, code);
            return {false, "staged asset verification failed: " + staged_check.error};
        }

        const bool had_destination = std::filesystem::exists(destination, code) && !code;
        if (had_destination) {
            std::filesystem::rename(destination, backup, code);
            if (code) {
                std::filesystem::remove_all(staging, code);
                return {false, "cannot preserve the previous asset tree: " + code.message()};
            }
        }
        std::filesystem::rename(staging, destination, code);
        if (code) {
            const auto rename_error = code.message();
            if (had_destination) {
                std::error_code restore_code;
                std::filesystem::rename(backup, destination, restore_code);
            }
            std::filesystem::remove_all(staging, code);
            return {false, "cannot activate the verified asset tree: " + rename_error};
        }
        if (had_destination) {
            std::filesystem::remove_all(backup, code);
        }
        return {true, {}};
    } catch (const std::exception& exception) {
        std::error_code ignored;
        if (!staging.empty()) {
            std::filesystem::remove_all(staging, ignored);
        }
        return {false, std::string{"asset installation failed: "} + exception.what()};
    } catch (...) {
        std::error_code ignored;
        if (!staging.empty()) {
            std::filesystem::remove_all(staging, ignored);
        }
        return {false, "asset installation failed with an unknown exception"};
    }
}

NativeSteamImportResult import_native_steam_runtime(
    const std::filesystem::path& selected_directory,
    const std::filesystem::path& executable_directory) noexcept {
#if !defined(_WIN32)
    static_cast<void>(selected_directory);
    static_cast<void>(executable_directory);
    return {};
#else
    try {
        std::vector<std::filesystem::path> roots;
        append_standard_layouts(roots, selected_directory);
        roots.push_back(selected_directory.parent_path());
        std::filesystem::path source;
        std::error_code code;
        for (const auto& root : roots) {
            const auto candidate = root / "steam_api.dll";
            const auto status = std::filesystem::symlink_status(candidate, code);
            if (!code && std::filesystem::is_regular_file(status) &&
                !std::filesystem::is_symlink(status)) {
                source = std::filesystem::weakly_canonical(candidate, code);
                if (!code) break;
            }
            code.clear();
        }
        if (source.empty()) return {};

        const auto size = std::filesystem::file_size(source, code);
        if (code || size < 256U || size > 16U * 1'024U * 1'024U) {
            return {false, "the selected steam_api.dll has an invalid size"};
        }
        std::ifstream stream(source, std::ios::binary);
        std::array<unsigned char, 64U> dos{};
        stream.read(reinterpret_cast<char*>(dos.data()),
                    static_cast<std::streamsize>(dos.size()));
        if (!stream || dos[0U] != 'M' || dos[1U] != 'Z') {
            return {false, "the selected steam_api.dll is not a PE image"};
        }
        const auto pe_offset = static_cast<std::uint32_t>(dos[0x3CU]) |
                               (static_cast<std::uint32_t>(dos[0x3DU]) << 8U) |
                               (static_cast<std::uint32_t>(dos[0x3EU]) << 16U) |
                               (static_cast<std::uint32_t>(dos[0x3FU]) << 24U);
        if (pe_offset > size - 6U) {
            return {false, "the selected steam_api.dll has an invalid PE header"};
        }
        stream.seekg(static_cast<std::streamoff>(pe_offset));
        std::array<unsigned char, 6U> pe{};
        stream.read(reinterpret_cast<char*>(pe.data()),
                    static_cast<std::streamsize>(pe.size()));
        const auto machine = static_cast<std::uint16_t>(pe[4U]) |
                             static_cast<std::uint16_t>(pe[5U] << 8U);
        if (!stream || pe[0U] != 'P' || pe[1U] != 'E' || pe[2U] != 0U ||
            pe[3U] != 0U || machine != 0x014CU) {
            return {false, "steam_api.dll must be the original 32-bit retail runtime"};
        }

        const auto destination = executable_directory / "steam" / "win32";
        std::filesystem::create_directories(destination, code);
        if (code) return {false, "cannot create the Steam runtime directory: " + code.message()};
        const auto staging = destination / "steam_api.dll.installing";
        std::filesystem::copy_file(source, staging,
                                   std::filesystem::copy_options::overwrite_existing, code);
        if (code) return {false, "cannot copy steam_api.dll: " + code.message()};
        std::filesystem::rename(staging, destination / "steam_api.dll", code);
        if (code) {
            std::filesystem::remove(destination / "steam_api.dll", code);
            code.clear();
            std::filesystem::rename(staging, destination / "steam_api.dll", code);
        }
        if (code) return {false, "cannot activate steam_api.dll: " + code.message()};

        // The helper passes AppID explicitly, but preserving this tiny retail
        // marker keeps Steam launches and developer diagnostics conventional.
        const auto app_id_source = source.parent_path() / "steam_appid.txt";
        if (std::filesystem::is_regular_file(app_id_source, code) && !code) {
            std::filesystem::copy_file(app_id_source, destination / "steam_appid.txt",
                                       std::filesystem::copy_options::overwrite_existing, code);
            if (code) return {false, "cannot copy steam_appid.txt: " + code.message()};
        }
        return {true, {}};
    } catch (const std::exception& exception) {
        return {false, std::string{"Steam runtime import failed: "} + exception.what()};
    } catch (...) {
        return {false, "Steam runtime import failed"};
    }
#endif
}

AssetInstallerExit run_asset_installer(const std::filesystem::path& installer_executable,
                                       const std::filesystem::path& manifest_path,
                                       const std::filesystem::path& destination_root,
                                       std::string& error) noexcept {
    try {
        std::error_code code;
        if (!std::filesystem::is_regular_file(installer_executable, code) || code) {
            error = "packaged asset installer is missing: " + path_text(installer_executable);
            return AssetInstallerExit::failed;
        }
#if defined(_WIN32)
        std::wstring command = quote_windows_argument(installer_executable) + L" --manifest " +
                               quote_windows_argument(manifest_path) + L" --destination " +
                               quote_windows_argument(destination_root);
        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        const auto working_directory = installer_executable.parent_path().wstring();
        if (CreateProcessW(installer_executable.c_str(),
                           mutable_command.data(),
                           nullptr,
                           nullptr,
                           FALSE,
                           0U,
                           nullptr,
                           working_directory.c_str(),
                           &startup,
                           &process) == FALSE) {
            error = "could not launch the asset installer (Windows error " +
                    std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
            return AssetInstallerExit::failed;
        }
        CloseHandle(process.hThread);
        const auto wait = WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exit_code{1U};
        const bool read_exit = GetExitCodeProcess(process.hProcess, &exit_code) != FALSE;
        CloseHandle(process.hProcess);
        if (wait != WAIT_OBJECT_0 || !read_exit) {
            error = "asset installer process did not exit cleanly";
            return AssetInstallerExit::failed;
        }
        if (exit_code == 0U) {
            error.clear();
            return AssetInstallerExit::installed;
        }
        if (exit_code == static_cast<DWORD>(installer_cancel_exit_code)) {
            error = "asset import was cancelled";
            return AssetInstallerExit::cancelled;
        }
        error = "asset installer failed with exit code " + std::to_string(exit_code);
        return AssetInstallerExit::failed;
#else
        const auto executable = installer_executable.string();
        const auto manifest = manifest_path.string();
        const auto destination = destination_root.string();
        std::array arguments{
            const_cast<char*>(executable.c_str()),
            const_cast<char*>("--manifest"),
            const_cast<char*>(manifest.c_str()),
            const_cast<char*>("--destination"),
            const_cast<char*>(destination.c_str()),
            static_cast<char*>(nullptr),
        };
        pid_t process{};
        const int spawn_result =
            posix_spawn(&process, executable.c_str(), nullptr, nullptr, arguments.data(), environ);
        if (spawn_result != 0) {
            error = "could not launch the asset installer: " +
                    std::string{std::strerror(spawn_result)};
            return AssetInstallerExit::failed;
        }
        int status{};
        while (waitpid(process, &status, 0) < 0) {
            if (errno != EINTR) {
                error = "could not wait for the asset installer";
                return AssetInstallerExit::failed;
            }
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            error.clear();
            return AssetInstallerExit::installed;
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == installer_cancel_exit_code) {
            error = "asset import was cancelled";
            return AssetInstallerExit::cancelled;
        }
        error = "asset installer terminated unsuccessfully";
        return AssetInstallerExit::failed;
#endif
    } catch (const std::exception& exception) {
        error = std::string{"asset installer launch failed: "} + exception.what();
        return AssetInstallerExit::failed;
    } catch (...) {
        error = "asset installer launch failed with an unknown exception";
        return AssetInstallerExit::failed;
    }
}

} // namespace battlespades::assets
