#include "battlespades/platform/workshop_sync.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

namespace battlespades::platform {
namespace {

constexpr std::string_view index_header{"battlespades-workshop-index 1"};
/** Retail sidecars are a few KiB of JSON; the client's scanner refuses over 1 MiB. */
constexpr std::size_t maximum_sidecar_bytes{1U << 20U};
constexpr std::string_view subscribed_prefix{"Subscribed_"};

[[nodiscard]] std::uint32_t read_le32(const unsigned char* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

void append_chunk(std::vector<unsigned char>& output, std::string_view tag,
                  std::span<const unsigned char> payload) {
    const auto size = static_cast<std::uint32_t>(payload.size());
    output.insert(output.end(), tag.begin(), tag.end());
    output.push_back(0U);
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        output.push_back(static_cast<unsigned char>((size >> shift) & 0xFFU));
    }
    output.insert(output.end(), payload.begin(), payload.end());
}

/** First non-blank byte is '{': a JSON object, as every retail sidecar is. */
[[nodiscard]] bool looks_like_json_object(std::span<const unsigned char> bytes) noexcept {
    std::size_t offset{};
    // UTF-8 byte order mark.
    if (bytes.size() >= 3U && bytes[0] == 0xEFU && bytes[1] == 0xBBU && bytes[2] == 0xBFU) {
        offset = 3U;
    }
    for (; offset < bytes.size(); ++offset) {
        const auto value = bytes[offset];
        if (value == ' ' || value == '\t' || value == '\r' || value == '\n') continue;
        return value == '{';
    }
    return false;
}

[[nodiscard]] std::string lower(std::string_view value) {
    std::string output{value};
    std::ranges::transform(output, output.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return output;
}

} // namespace

std::string workshop_subscribed_stem(std::uint64_t published_file_id) {
    return std::string{subscribed_prefix} + std::to_string(published_file_id);
}

std::optional<std::uint64_t> parse_workshop_subscribed_stem(std::string_view stem) {
    if (!stem.starts_with(subscribed_prefix)) return std::nullopt;
    const auto digits = stem.substr(subscribed_prefix.size());
    if (digits.empty() || digits.size() > 20U || (digits.size() > 1U && digits.front() == '0')) {
        return std::nullopt;
    }
    std::uint64_t value{};
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error != std::errc{} || end != digits.data() + digits.size() || value == 0U) {
        return std::nullopt;
    }
    return value;
}

std::optional<AosContainer> parse_aos_container(std::span<const unsigned char> bytes,
                                                std::string& error) {
    error.clear();
    AosContainer output;
    bool have_vxl{};
    bool have_ugc{};
    std::size_t offset{};
    while (!(have_vxl && have_ugc)) {
        const auto left = bytes.size() - offset;
        if (left < 8U) {
            error = left == 0U ? (have_vxl ? "the item has no UGC sidecar"
                                           : "the item has no VXL map data")
                               : "the item ends inside a chunk header";
            return std::nullopt;
        }
        const auto* const header = bytes.data() + offset;
        const auto length = static_cast<std::size_t>(read_le32(header + 4U));
        // Retail compares the tag with strcmp, so the tag must be NUL-terminated.
        const bool vxl = std::memcmp(header, "VXL\0", 4U) == 0;
        const bool ugc = std::memcmp(header, "UGC\0", 4U) == 0;
        if (!vxl && !ugc) {
            error = "the item holds an unknown chunk at byte " + std::to_string(offset);
            return std::nullopt;
        }
        if (length > left - 8U) {
            error = std::string{"the item's "} + (vxl ? "VXL" : "UGC") + " chunk is truncated";
            return std::nullopt;
        }
        const auto payload = bytes.subspan(offset + 8U, length);
        // A repeated chunk replaces the earlier one, as retail's rewrite did.
        auto& target = vxl ? output.vxl : output.ugc;
        target.assign(payload.begin(), payload.end());
        (vxl ? have_vxl : have_ugc) = true;
        offset += 8U + length;
    }
    if (output.vxl.empty()) {
        error = "the item's VXL map data is empty";
        return std::nullopt;
    }
    if (output.ugc.empty() || output.ugc.size() > maximum_sidecar_bytes ||
        !looks_like_json_object(output.ugc)) {
        error = "the item's UGC sidecar is not a JSON object under 1 MiB";
        return std::nullopt;
    }
    return output;
}

std::vector<unsigned char> build_aos_container(std::span<const unsigned char> vxl,
                                               std::span<const unsigned char> ugc) {
    std::vector<unsigned char> output;
    output.reserve(vxl.size() + ugc.size() + 16U);
    append_chunk(output, "VXL", vxl);
    append_chunk(output, "UGC", ugc);
    return output;
}

bool workshop_safe_file_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 200U || name == "." || name == "..") return false;
    for (const char character : name) {
        const auto code = static_cast<unsigned char>(character);
        if (code < 0x20U || code == 0x7FU) return false;
        if (std::strchr("/\\:*?\"<>|", character) != nullptr) return false;
    }
    // Windows strips trailing dots and spaces, which would alias another name.
    if (name.back() == '.' || name.back() == ' ' || name.front() == ' ') return false;
    if (name.find("..") != std::string_view::npos) return false;
    try {
        const auto device = lower(name.substr(0U, name.find('.')));
        static constexpr std::array reserved{"con", "prn", "aux", "nul", "com1", "com2", "com3",
                                             "com4", "com5", "com6", "com7", "com8", "com9",
                                             "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6",
                                             "lpt7", "lpt8", "lpt9"};
        for (const auto* const entry : reserved) {
            if (device == entry) return false;
        }
        const std::filesystem::path path{std::string{name}};
        return !path.is_absolute() && !path.has_root_name() && !path.has_parent_path() &&
               path.filename() == path;
    } catch (...) {
        return false;
    }
}

std::string serialize_workshop_index(const WorkshopIndex& index) {
    std::ostringstream output;
    output << index_header << '\n';
    for (const auto& [id, entry] : index.items) {
        output << id << ' ' << entry.time_updated;
        for (const auto& file : entry.files) output << ' ' << file;
        output << '\n';
    }
    return output.str();
}

WorkshopIndex parse_workshop_index(std::string_view text) {
    WorkshopIndex index;
    std::istringstream input{std::string{text}};
    std::string line;
    if (!std::getline(input, line)) return index;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != index_header) return index;
    while (std::getline(input, line) && index.items.size() < 4'096U) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream fields{line};
        std::string id_text;
        std::uint64_t time{};
        if (!(fields >> id_text >> time) || time > 0xFFFF'FFFFULL) continue;
        std::uint64_t id{};
        const auto [end, error] =
            std::from_chars(id_text.data(), id_text.data() + id_text.size(), id);
        if (error != std::errc{} || end != id_text.data() + id_text.size() || id == 0U) continue;
        WorkshopIndexEntry entry;
        entry.time_updated = static_cast<std::uint32_t>(time);
        std::string file;
        while (fields >> file && entry.files.size() < 8U) {
            if (workshop_safe_file_name(file)) entry.files.push_back(file);
        }
        index.items[id] = std::move(entry);
    }
    return index;
}

WorkshopIndex load_workshop_index(const std::filesystem::path& file) {
    std::string error;
    const auto bytes = read_workshop_file(file, 1U << 20U, error);
    if (!bytes.has_value()) return {};
    return parse_workshop_index(
        std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
}

bool save_workshop_index(const std::filesystem::path& file, const WorkshopIndex& index,
                         std::string& error) {
    const auto text = serialize_workshop_index(index);
    return write_file_atomically(
        file,
        std::span<const unsigned char>{reinterpret_cast<const unsigned char*>(text.data()),
                                       text.size()},
        error);
}

bool workshop_entry_files_present(const std::filesystem::path& maps_directory,
                                  const WorkshopIndexEntry& entry) {
    if (entry.files.empty()) return false;
    for (const auto& file : entry.files) {
        std::error_code code;
        if (!std::filesystem::is_regular_file(maps_directory / file, code) || code) return false;
    }
    return true;
}

bool write_file_atomically(const std::filesystem::path& target,
                           std::span<const unsigned char> bytes, std::string& error) {
    // The gallery and thumbnail workers can publish the same cached image.
    // Protect the shared temporary filename through its final rename; network
    // fetches, validation and image decoding all happen before this short disk
    // transaction. The other callers only publish Workshop maps and indexes.
    static std::mutex publication_mutex;
    const std::lock_guard publication_lock{publication_mutex};
    error.clear();
    // A leading dot keeps the half-written file out of the client's map
    // scanner (it reads *.ugc) and out of its reserved-stem list.
    auto temporary = target;
    temporary.replace_filename("." + target.filename().string() + ".partial");
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        if (!output) {
            error = "cannot write " + temporary.string();
            return false;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            error = "writing " + temporary.string() + " failed";
            output.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
    std::error_code code;
    // MSVC's rename replaces an existing target (MoveFileEx REPLACE_EXISTING).
    std::filesystem::rename(temporary, target, code);
    if (code) {
        error = "cannot replace " + target.string() + ": " + code.message();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

std::optional<WorkshopIndexEntry> install_workshop_map(
    const std::filesystem::path& maps_directory, std::uint64_t published_file_id,
    std::uint32_t time_updated, std::span<const unsigned char> aos_bytes,
    std::span<const unsigned char> preview_png, std::string& error) {
    error.clear();
    if (published_file_id == 0U) {
        error = "an item without a published file id";
        return std::nullopt;
    }
    auto container = parse_aos_container(aos_bytes, error);
    if (!container.has_value()) return std::nullopt;
    std::error_code code;
    std::filesystem::create_directories(maps_directory, code);
    if (code) {
        error = "cannot create " + maps_directory.string() + ": " + code.message();
        return std::nullopt;
    }
    const auto stem = workshop_subscribed_stem(published_file_id);
    WorkshopIndexEntry entry;
    entry.time_updated = time_updated;
    const auto write = [&](std::string_view extension, std::span<const unsigned char> bytes) {
        const auto name = stem + std::string{extension};
        if (!write_file_atomically(maps_directory / name, bytes, error)) return false;
        entry.files.push_back(name);
        return true;
    };
    // The scanner lists a map by its .ugc, so that goes last: the map
    // appears only once its voxels (and preview) are in place.
    if (!write(".vxl", container->vxl)) return std::nullopt;
    static constexpr std::array<unsigned char, 8> png_signature{0x89U, 'P', 'N', 'G',
                                                                0x0DU, 0x0AU, 0x1AU, 0x0AU};
    if (preview_png.size() > png_signature.size() &&
        std::equal(png_signature.begin(), png_signature.end(), preview_png.begin())) {
        if (!write(".png", preview_png)) return std::nullopt;
    }
    // The map's server-side metadata: the same JSON as the sidecar (as in
    // subscribed maps of existing retail installs), which a hosted match needs
    // (validate_custom_map_files insists on .vxl, .txt and .ugc).
    if (!write(".txt", container->ugc)) return std::nullopt;
    if (!write(".ugc", container->ugc)) return std::nullopt;
    return entry;
}

std::size_t remove_workshop_map(const std::filesystem::path& maps_directory,
                                std::uint64_t published_file_id, const WorkshopIndexEntry& entry) {
    const auto stem = workshop_subscribed_stem(published_file_id) + ".";
    std::size_t removed{};
    // The .ugc first, so a half-removed map never lists without its voxels.
    auto files = entry.files;
    std::ranges::stable_partition(files, [](const std::string& file) {
        return file.ends_with(".ugc");
    });
    for (const auto& file : files) {
        if (!workshop_safe_file_name(file) || !file.starts_with(stem)) continue;
        const auto extension = file.substr(stem.size());
        if (extension != "vxl" && extension != "ugc" && extension != "png" && extension != "txt") {
            continue;
        }
        std::error_code code;
        const auto path = maps_directory / file;
        if (std::filesystem::is_symlink(path, code)) continue;
        if (std::filesystem::remove(path, code) && !code) ++removed;
    }
    return removed;
}

std::optional<std::vector<unsigned char>> read_workshop_file(const std::filesystem::path& file,
                                                             std::uintmax_t maximum_bytes,
                                                             std::string& error) {
    error.clear();
    std::error_code code;
    if (!std::filesystem::is_regular_file(file, code) || code) {
        error = file.string() + " is not a file";
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(file, code);
    if (code || size > maximum_bytes) {
        error = file.string() + " is unreadable or too large";
        return std::nullopt;
    }
    std::ifstream input{file, std::ios::binary};
    if (!input) {
        error = "cannot open " + file.string();
        return std::nullopt;
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        error = "short read of " + file.string();
        return std::nullopt;
    }
    return bytes;
}

} // namespace battlespades::platform
