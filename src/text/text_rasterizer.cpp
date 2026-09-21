#include "battlespades/text/text_rasterizer.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H
#include <harfbuzz/hb-ft.h>
#include <harfbuzz/hb.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <list>
#include <mutex>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace battlespades::text {
namespace {

constexpr std::uint64_t fnv_offset_basis{14'695'981'039'346'656'037ULL};
constexpr std::uint64_t fnv_prime{1'099'511'628'211ULL};
constexpr FT_Int32 retail_ft_load_flags{FT_LOAD_DEFAULT | FT_LOAD_FORCE_AUTOHINT};
constexpr FT_Int32 retail_bbox_load_flags{FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP};
// Shipping FTTextureGlyph calls FT_Stroker_Set(..., 140, ROUND, ROUND, 0).
constexpr FT_Fixed retail_ftgl_stroke_radius_26_6{140};
constexpr std::uintmax_t maximum_font_file_bytes{64U * 1'024U * 1'024U};

[[nodiscard]] std::uint64_t fnv1a(std::string_view bytes,
                                  std::uint64_t hash = fnv_offset_basis) noexcept {
    for (const auto byte : bytes) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= fnv_prime;
    }
    return hash;
}

[[nodiscard]] std::string hash_hex(std::uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << value;
    return stream.str();
}

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
    const auto encoded = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(encoded.data()),
                       encoded.size()};
}

[[nodiscard]] bool is_descendant(const std::filesystem::path& candidate,
                                 const std::filesystem::path& root) {
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty() && candidate != root) {
        return false;
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return !relative.is_absolute();
}

[[nodiscard]] bool load_font_file(const std::filesystem::path& path,
                                  std::vector<FT_Byte>& bytes,
                                  std::uint64_t& fingerprint,
                                  std::string& error) {
    std::error_code filesystem_error;
    const auto size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error || size == 0U || size > maximum_font_file_bytes ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<FT_Long>::max())) {
        error = "font file has an invalid size: " + path_utf8(path);
        return false;
    }

    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        error = "cannot open font bytes: " + path_utf8(path);
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!stream ||
        stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        bytes.clear();
        error = "failed while reading font bytes: " + path_utf8(path);
        return false;
    }
    fingerprint = fnv1a(std::string_view{
        reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    error.clear();
    return true;
}

[[nodiscard]] bool valid_continuation(std::uint8_t byte) noexcept {
    return (byte & 0xC0U) == 0x80U;
}

struct Utf8NormalizationResult final {
    std::string text;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error.empty();
    }
};

[[nodiscard]] constexpr std::uint32_t simple_uppercase(std::uint32_t code_point) noexcept {
    if (code_point >= 'a' && code_point <= 'z') return code_point - ('a' - 'A');

    // Latin-1 letters used by the recovered Western European packs.
    if ((code_point >= 0x00E0U && code_point <= 0x00F6U && code_point != 0x00F7U) ||
        (code_point >= 0x00F8U && code_point <= 0x00FEU)) {
        return code_point - 0x20U;
    }
    if (code_point == 0x00FFU) return 0x0178U; // ÿ -> Ÿ

    // Locale letters outside Latin-1 (Polish, Czech and Turkish).  Keeping
    // this table explicit avoids process-locale dependent std::towupper and
    // gives identical output on Windows, Linux and macOS.
    switch (code_point) {
    case 0x0105U: return 0x0104U; // ą
    case 0x0107U: return 0x0106U; // ć
    case 0x010DU: return 0x010CU; // č
    case 0x010FU: return 0x010EU; // ď
    case 0x0119U: return 0x0118U; // ę
    case 0x011BU: return 0x011AU; // ě
    case 0x011FU: return 0x011EU; // ğ
    case 0x0131U: return 0x0049U; // dotless i
    case 0x0142U: return 0x0141U; // ł
    case 0x0144U: return 0x0143U; // ń
    case 0x0148U: return 0x0147U; // ň
    case 0x0159U: return 0x0158U; // ř
    case 0x015BU: return 0x015AU; // ś
    case 0x015FU: return 0x015EU; // ş
    case 0x0161U: return 0x0160U; // š
    case 0x0165U: return 0x0164U; // ť
    case 0x016FU: return 0x016EU; // ů
    case 0x017AU: return 0x0179U; // ź
    case 0x017CU: return 0x017BU; // ż
    case 0x017EU: return 0x017DU; // ž
    default: break;
    }

    // Russian/Bulgarian plus the Ukrainian letters used by community packs.
    if (code_point >= 0x0430U && code_point <= 0x044FU) return code_point - 0x20U;
    if (code_point >= 0x0450U && code_point <= 0x045FU) return code_point - 0x50U;
    switch (code_point) {
    case 0x0491U: return 0x0490U; // ґ
    case 0x04CFU: return 0x04C0U;
    default: return code_point;
    }
}

void append_utf8(std::string& output, std::uint32_t code_point) {
    if (code_point <= 0x7FU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

[[nodiscard]] Utf8NormalizationResult normalize_utf8(std::string_view input, TextCase text_case) {
    Utf8NormalizationResult result{{}, {}};
    result.text.reserve(input.size());
    std::size_t index{};
    while (index < input.size()) {
        const auto first = static_cast<std::uint8_t>(input[index]);
        std::size_t length{};
        std::uint32_t code_point{};
        if (first <= 0x7FU) {
            length = 1U;
            code_point = first;
        } else if (first >= 0xC2U && first <= 0xDFU) {
            length = 2U;
            code_point = first & 0x1FU;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            length = 3U;
            code_point = first & 0x0FU;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            length = 4U;
            code_point = first & 0x07U;
        } else {
            result.error = "invalid UTF-8 leading byte at byte " + std::to_string(index);
            return result;
        }

        if (index + length > input.size()) {
            result.error = "truncated UTF-8 sequence at byte " + std::to_string(index);
            return result;
        }
        for (std::size_t offset = 1U; offset < length; ++offset) {
            const auto continuation = static_cast<std::uint8_t>(input[index + offset]);
            if (!valid_continuation(continuation)) {
                result.error =
                    "invalid UTF-8 continuation byte at byte " + std::to_string(index + offset);
                return result;
            }
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }

        const auto overlong = (length == 2U && code_point < 0x80U) ||
                              (length == 3U && code_point < 0x800U) ||
                              (length == 4U && code_point < 0x10000U);
        const auto surrogate = code_point >= 0xD800U && code_point <= 0xDFFFU;
        if (overlong || surrogate || code_point > 0x10FFFFU) {
            result.error = "invalid UTF-8 scalar value at byte " + std::to_string(index);
            return result;
        }

        // A few recovered European strings use no-break spacing characters
        // whose retail fonts expose no cmap entry. They are layout spaces, not
        // visible missing glyphs; normalize them before HarfBuzz so they can
        // never turn into a question-mark/tofu box on another platform.
        if (code_point == 0x00A0U || code_point == 0x2007U || code_point == 0x202FU) {
            code_point = 0x20U;
        }

        // Retail English labels only require locale-invariant ASCII casing.
        if (text_case == TextCase::ascii_uppercase && code_point >= 'a' &&
            code_point <= 'z') {
            code_point -= ('a' - 'A');
        } else if (text_case == TextCase::unicode_uppercase) {
            code_point = simple_uppercase(code_point);
        }
        append_utf8(result.text, code_point);
        index += length;
    }
    return result;
}

[[nodiscard]] std::int64_t floor_26_6(std::int64_t value) noexcept {
    if (value >= 0) {
        return value / 64;
    }
    return -((-value + 63) / 64);
}

[[nodiscard]] std::int64_t ceil_26_6(std::int64_t value) noexcept {
    if (value >= 0) {
        return (value + 63) / 64;
    }
    return -((-value) / 64);
}

[[nodiscard]] std::string ft_error(std::string_view operation, FT_Error error) {
    return std::string{operation} + " failed with FreeType error " + std::to_string(error);
}

struct HbBufferDeleter final {
    void operator()(hb_buffer_t* buffer) const noexcept {
        if (buffer != nullptr) {
            hb_buffer_destroy(buffer);
        }
    }
};

using UniqueHbBuffer = std::unique_ptr<hb_buffer_t, HbBufferDeleter>;

struct PlacedGlyph final {
    FT_UInt glyph_index{};
    std::int64_t left{};
    std::int64_t top{};
};

[[nodiscard]] std::uint8_t
gray_coverage(const FT_Bitmap& bitmap, const std::uint8_t* row, std::uint32_t x) noexcept {
    if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
        const auto byte = row[x / 8U];
        const auto mask = static_cast<std::uint8_t>(0x80U >> (x % 8U));
        return (byte & mask) != 0U ? 255U : 0U;
    }
    const auto sample = row[x];
    if (bitmap.num_grays <= 1U || bitmap.num_grays == 256U) {
        return sample;
    }
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(sample) * 255U) /
                                     (bitmap.num_grays - 1U));
}

} // namespace

class TextRasterizer::Impl final {
public:
    explicit Impl(TextRasterizerConfig configuration) : config{std::move(configuration)} {
        initialize();
    }

    ~Impl() {
        if (hb_font != nullptr) {
            hb_font_destroy(hb_font);
        }
        if (face != nullptr) {
            FT_Done_Face(face);
        }
        if (library != nullptr) {
            FT_Done_FreeType(library);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    struct CacheEntry final {
        std::shared_ptr<const RasterizedText> output;
        std::size_t bytes{};
        std::list<std::string>::iterator lru_position;
    };

    TextRasterizerConfig config;
    FT_Library library{};
    FT_Face face{};
    hb_font_t* hb_font{};
    std::filesystem::path resolved_font_path;
    // FT_New_Memory_Face borrows this allocation until FT_Done_Face.
    std::vector<FT_Byte> font_bytes;
    std::string font_id;
    std::uint64_t fingerprint{};
    TextErrorCode init_error_code{TextErrorCode::none};
    std::string init_error;
    mutable std::mutex mutex;
    std::unordered_map<std::string, CacheEntry> cache;
    mutable std::unordered_map<std::string, bool> coverage_cache;
    std::list<std::string> lru;
    TextCacheStats stats;

    [[nodiscard]] bool ready() const noexcept {
        return init_error_code == TextErrorCode::none && library != nullptr && face != nullptr &&
               hb_font != nullptr;
    }

    [[nodiscard]] bool supports_text(std::string_view utf8, TextCase text_case) const {
        if (!ready() || utf8.size() > config.limits.maximum_utf8_bytes) return false;
        const auto normalized = normalize_utf8(utf8, text_case);
        if (!normalized) return false;

        std::scoped_lock lock{mutex};
        if (const auto cached = coverage_cache.find(normalized.text);
            cached != coverage_cache.end()) {
            return cached->second;
        }
        bool covered{true};
        std::size_t offset{};
        while (offset < normalized.text.size()) {
            const auto first = static_cast<std::uint8_t>(normalized.text[offset]);
            std::size_t length{1U};
            std::uint32_t code_point{first};
            if (first >= 0xC2U && first <= 0xDFU) {
                length = 2U;
                code_point = first & 0x1FU;
            } else if (first >= 0xE0U && first <= 0xEFU) {
                length = 3U;
                code_point = first & 0x0FU;
            } else if (first >= 0xF0U) {
                length = 4U;
                code_point = first & 0x07U;
            }
            for (std::size_t index = 1U; index < length; ++index) {
                code_point =
                    (code_point << 6U) |
                    (static_cast<std::uint8_t>(normalized.text[offset + index]) & 0x3FU);
            }

            const auto non_rendering = code_point < 0x20U || code_point == 0x7FU ||
                                       code_point == 0x200CU || code_point == 0x200DU ||
                                       (code_point >= 0xFE00U && code_point <= 0xFE0FU) ||
                                       (code_point >= 0xE0100U && code_point <= 0xE01EFU);
            if (!non_rendering && FT_Get_Char_Index(face, code_point) == 0U) {
                covered = false;
                break;
            }
            offset += length;
        }
        if (coverage_cache.size() >= 1'024U) coverage_cache.clear();
        coverage_cache.emplace(normalized.text, covered);
        return covered;
    }

    void initialize() {
        if (config.asset_root.empty() || config.font_asset.empty()) {
            fail_initialization(TextErrorCode::invalid_configuration,
                                "asset root and font asset must both be configured");
            return;
        }
        if (!valid_limits()) {
            fail_initialization(TextErrorCode::invalid_configuration,
                                "text rasterizer limits are invalid or internally inconsistent");
            return;
        }
        if (config.default_pixel_height == 0U ||
            config.default_pixel_height > config.limits.maximum_pixel_height) {
            fail_initialization(TextErrorCode::invalid_configuration,
                                "default font pixel height is outside configured limits");
            return;
        }

        std::error_code error;
        const auto root = std::filesystem::weakly_canonical(config.asset_root, error);
        if (error || !std::filesystem::is_directory(root, error) || error) {
            fail_initialization(TextErrorCode::invalid_configuration,
                                "asset root does not resolve to a directory: " +
                                    path_utf8(config.asset_root));
            return;
        }
        const auto candidate =
            config.font_asset.is_absolute() ? config.font_asset : root / config.font_asset;
        resolved_font_path = std::filesystem::weakly_canonical(candidate, error);
        if (error || !is_descendant(resolved_font_path, root)) {
            fail_initialization(TextErrorCode::invalid_configuration,
                                "font asset resolves outside the configured asset root");
            return;
        }
        if (!std::filesystem::is_regular_file(resolved_font_path, error) || error) {
            fail_initialization(TextErrorCode::invalid_configuration,
                                "font asset is not a regular file: " +
                                    path_utf8(resolved_font_path));
            return;
        }

        const auto relative = resolved_font_path.lexically_relative(root);
        font_id = path_utf8(relative);
        if (!load_font_file(resolved_font_path,
                            font_bytes,
                            fingerprint,
                            init_error)) {
            init_error_code = TextErrorCode::font_error;
            return;
        }

        auto ft_result = FT_Init_FreeType(&library);
        if (ft_result != 0) {
            fail_initialization(TextErrorCode::font_error, ft_error("FT_Init_FreeType", ft_result));
            return;
        }
        // FreeType's filename API is narrow on Windows and therefore cannot
        // open an executable-adjacent font below an arbitrary Unicode path.
        // std::ifstream already opened it through the native wide path; keep
        // those bounded bytes alive and let FreeType consume memory directly.
        ft_result = FT_New_Memory_Face(
            library,
            font_bytes.data(),
            static_cast<FT_Long>(font_bytes.size()),
            0,
            &face);
        if (ft_result != 0) {
            fail_initialization(TextErrorCode::font_error,
                                ft_error("FT_New_Memory_Face", ft_result));
            return;
        }
        ft_result = FT_Select_Charmap(face, FT_ENCODING_UNICODE);
        if (ft_result != 0) {
            fail_initialization(TextErrorCode::font_error,
                                ft_error("FT_Select_Charmap", ft_result));
            return;
        }
        ft_result = FT_Set_Pixel_Sizes(face, 0U, config.default_pixel_height);
        if (ft_result != 0) {
            fail_initialization(TextErrorCode::font_error,
                                ft_error("FT_Set_Pixel_Sizes", ft_result));
            return;
        }
        hb_font = hb_ft_font_create_referenced(face);
        if (hb_font == nullptr) {
            fail_initialization(TextErrorCode::font_error,
                                "hb_ft_font_create_referenced returned null");
        } else {
            hb_ft_font_set_load_flags(hb_font, retail_ft_load_flags);
        }
    }

    [[nodiscard]] bool valid_limits() const noexcept {
        const auto& limits = config.limits;
        return limits.maximum_utf8_bytes > 0U && limits.maximum_glyphs > 0U &&
               limits.maximum_pixel_height > 0U && limits.maximum_bitmap_dimension > 0U &&
               limits.maximum_bitmap_bytes >= 4U &&
               (limits.maximum_cache_entries == 0U || limits.maximum_cache_bytes > 0U);
    }

    void fail_initialization(TextErrorCode code, std::string message) {
        init_error_code = code;
        init_error = std::move(message);
    }

    [[nodiscard]] TextCacheKeyResult key_for(const TextRasterRequest& request) const {
        if (!ready()) {
            return {std::nullopt, TextErrorCode::not_ready, init_error};
        }
        if (request.utf8.size() > config.limits.maximum_utf8_bytes) {
            return {std::nullopt,
                    TextErrorCode::resource_limit,
                    "UTF-8 input exceeds the configured byte limit"};
        }
        const auto pixel_height = request.pixel_height.value_or(config.default_pixel_height);
        if (pixel_height == 0U || pixel_height > config.limits.maximum_pixel_height) {
            return {std::nullopt,
                    TextErrorCode::resource_limit,
                    "requested font pixel height is outside configured limits"};
        }

        auto normalized = normalize_utf8(request.utf8, request.text_case);
        if (!normalized) {
            return {std::nullopt, TextErrorCode::invalid_utf8, std::move(normalized.error)};
        }
        auto& transformed = normalized.text;
        if (transformed.find_first_of("\r\n") != std::string::npos) {
            return {std::nullopt,
                    TextErrorCode::invalid_request,
                    "single-line text rasterization does not accept line breaks"};
        }

        std::string canonical;
        canonical.reserve(transformed.size() + font_id.size() + 96U);
        canonical += "battlespades-text-v3-retail-ftgl\nfont=";
        canonical += font_id;
        canonical += "\nfingerprint=";
        canonical += hash_hex(fingerprint);
        canonical += "\npixel-height=";
        canonical += std::to_string(pixel_height);
        canonical += "\ncase=";
        switch (request.text_case) {
        case TextCase::ascii_uppercase: canonical += "ascii-upper"; break;
        case TextCase::unicode_uppercase: canonical += "unicode-upper"; break;
        case TextCase::preserve: canonical += "preserve"; break;
        }
        canonical += "\nrender=";
        canonical += request.retail_outline_stroke ? "ftgl-outside-stroke-140" : "normal";
        canonical += config.allow_missing_glyphs ? "\nmissing-glyphs=notdef"
                                                 : "\nmissing-glyphs=reject";
        canonical += "\nutf8-bytes=";
        canonical += std::to_string(transformed.size());
        canonical += "\n";
        canonical += transformed;
        return {TextCacheKey{canonical, fnv1a(canonical)}, TextErrorCode::none, {}};
    }

    [[nodiscard]] TextRasterResult rasterize(const TextRasterRequest& request) {
        const auto key_result = key_for(request);
        if (!key_result) {
            return {nullptr, {}, key_result.error_code, key_result.error, false};
        }
        auto key = *key_result.key;

        std::scoped_lock lock{mutex};
        if (const auto found = cache.find(key.canonical); found != cache.end()) {
            lru.splice(lru.begin(), lru, found->second.lru_position);
            ++stats.hits;
            return {found->second.output, std::move(key), TextErrorCode::none, {}, true};
        }
        ++stats.misses;

        const auto pixel_height = request.pixel_height.value_or(config.default_pixel_height);
        const auto transformed = transformed_text_from_key(key.canonical);
        auto result = rasterize_uncached(
            transformed, pixel_height, request.retail_outline_stroke, key);
        if (!result) {
            return result;
        }

        const auto entry_bytes = result.output->bitmap.pixels.size() +
                                 result.output->transformed_utf8.size() + key.canonical.size();
        if (config.limits.maximum_cache_entries > 0U &&
            entry_bytes <= config.limits.maximum_cache_bytes) {
            evict_for(entry_bytes);
            lru.push_front(key.canonical);
            cache.emplace(key.canonical, CacheEntry{result.output, entry_bytes, lru.begin()});
            stats.entries = cache.size();
            stats.bytes += entry_bytes;
        }
        return result;
    }

    [[nodiscard]] static std::string transformed_text_from_key(std::string_view canonical) {
        constexpr std::string_view marker{"\nutf8-bytes="};
        const auto marker_position = canonical.find(marker);
        if (marker_position == std::string_view::npos) {
            return {};
        }
        const auto text_separator = canonical.find('\n', marker_position + marker.size());
        return text_separator == std::string_view::npos
                   ? std::string{}
                   : std::string{canonical.substr(text_separator + 1U)};
    }

    [[nodiscard]] TextRasterResult rasterize_uncached(const std::string& text,
                                                      std::uint32_t pixel_height,
                                                      bool retail_outline_stroke,
                                                      const TextCacheKey& key) {
        auto error = FT_Set_Pixel_Sizes(face, 0U, pixel_height);
        if (error != 0) {
            return failure(TextErrorCode::font_error, ft_error("FT_Set_Pixel_Sizes", error), key);
        }
        hb_ft_font_changed(hb_font);

        UniqueHbBuffer buffer{hb_buffer_create()};
        if (buffer == nullptr || !hb_buffer_allocation_successful(buffer.get())) {
            return failure(
                TextErrorCode::shaping_error, "HarfBuzz could not allocate a shaping buffer", key);
        }
        hb_buffer_set_cluster_level(buffer.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
        hb_buffer_add_utf8(buffer.get(),
                           text.data(),
                           static_cast<int>(text.size()),
                           0U,
                           static_cast<int>(text.size()));
        hb_buffer_guess_segment_properties(buffer.get());
        // Retail's FTGL path advances one glyph at a time and does not apply
        // OpenType kerning or ligatures. Disabling those features preserves
        // its measured widths (notably the Welcome/name plate) while still
        // retaining HarfBuzz's UTF-8 decoding and script-to-glyph mapping.
        constexpr std::array retail_features{
            hb_feature_t{HB_TAG('k', 'e', 'r', 'n'),
                         0U,
                         HB_FEATURE_GLOBAL_START,
                         HB_FEATURE_GLOBAL_END},
            hb_feature_t{HB_TAG('l', 'i', 'g', 'a'),
                         0U,
                         HB_FEATURE_GLOBAL_START,
                         HB_FEATURE_GLOBAL_END},
            hb_feature_t{HB_TAG('c', 'l', 'i', 'g'),
                         0U,
                         HB_FEATURE_GLOBAL_START,
                         HB_FEATURE_GLOBAL_END},
        };
        hb_shape(hb_font,
                 buffer.get(),
                 retail_features.data(),
                 static_cast<unsigned int>(retail_features.size()));

        unsigned int glyph_count{};
        const auto* glyph_info = hb_buffer_get_glyph_infos(buffer.get(), &glyph_count);
        const auto* glyph_positions = hb_buffer_get_glyph_positions(buffer.get(), &glyph_count);
        if ((glyph_count > 0U && (glyph_info == nullptr || glyph_positions == nullptr)) ||
            glyph_count > config.limits.maximum_glyphs) {
            return failure(
                TextErrorCode::resource_limit, "shaped glyph count exceeds configured limits", key);
        }
        if (!config.allow_missing_glyphs && glyph_count > 0U &&
            std::any_of(glyph_info, glyph_info + glyph_count,
                        [](const hb_glyph_info_t& glyph) { return glyph.codepoint == 0U; })) {
            return failure(TextErrorCode::font_error,
                           "font shaping produced missing glyph zero",
                           key);
        }

        std::vector<PlacedGlyph> glyphs;
        glyphs.reserve(glyph_count);
        std::int64_t pen_x{};
        std::int64_t pen_y{};
        std::int64_t minimum_x{};
        std::int64_t minimum_y{};
        std::int64_t maximum_x{};
        std::int64_t maximum_y{};
        std::int64_t minimum_content_x{};
        std::int64_t maximum_content_x{};
        bool has_ink{};
        bool has_content{};

        // Retail does not fake its kill-feed outline with translated copies.
        // get_font(..., True) sets FTTextureFont's stroke flag; each glyph then
        // takes this exact FreeType path before conversion to an alpha bitmap.
        const auto with_rendered_glyph =
            [this, retail_outline_stroke](FT_UInt glyph_index,
                                          auto&& visitor) -> FT_Error {
            auto glyph_error = FT_Load_Glyph(face, glyph_index,
                                             retail_ft_load_flags);
            if (glyph_error != 0) return glyph_error;

            if (!retail_outline_stroke) {
                glyph_error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
                if (glyph_error == 0) {
                    visitor(face->glyph->bitmap, face->glyph->bitmap_left,
                            face->glyph->bitmap_top);
                }
                return glyph_error;
            }

            FT_Glyph glyph{};
            glyph_error = FT_Get_Glyph(face->glyph, &glyph);
            if (glyph_error != 0) return glyph_error;

            FT_Stroker stroker{};
            glyph_error = FT_Stroker_New(library, &stroker);
            if (glyph_error == 0) {
                FT_Stroker_Set(stroker, retail_ftgl_stroke_radius_26_6,
                               FT_STROKER_LINECAP_ROUND,
                               FT_STROKER_LINEJOIN_ROUND, 0);
                glyph_error = FT_Glyph_StrokeBorder(&glyph, stroker, false, true);
                FT_Stroker_Done(stroker);
            }
            if (glyph_error == 0) {
                glyph_error = FT_Glyph_To_Bitmap(
                    &glyph, FT_RENDER_MODE_NORMAL, nullptr, true);
            }
            if (glyph_error == 0) {
                const auto bitmap_glyph =
                    reinterpret_cast<FT_BitmapGlyph>(glyph);
                visitor(bitmap_glyph->bitmap, bitmap_glyph->left,
                        bitmap_glyph->top);
            }
            if (glyph != nullptr) FT_Done_Glyph(glyph);
            return glyph_error;
        };

        for (unsigned int index = 0U; index < glyph_count; ++index) {
            const auto glyph_index = static_cast<FT_UInt>(glyph_info[index].codepoint);
            error = FT_Load_Glyph(face, glyph_index, retail_bbox_load_flags);
            if (error != 0) {
                return failure(TextErrorCode::font_error, ft_error("FT_Load_Glyph", error), key);
            }
            const auto content_left = pen_x + glyph_positions[index].x_offset +
                                      face->glyph->metrics.horiBearingX;
            const auto content_right = content_left + face->glyph->metrics.width;
            if (face->glyph->metrics.width > 0) {
                if (!has_content) {
                    minimum_content_x = content_left;
                    maximum_content_x = content_right;
                    has_content = true;
                } else {
                    minimum_content_x = std::min(minimum_content_x, content_left);
                    maximum_content_x = std::max(maximum_content_x, content_right);
                }
            }

            const auto origin_x = floor_26_6(pen_x + glyph_positions[index].x_offset);
            const auto origin_y = floor_26_6(pen_y + glyph_positions[index].y_offset);
            std::int64_t left{};
            std::int64_t top{};
            std::int64_t width{};
            std::int64_t rows{};
            error = with_rendered_glyph(
                glyph_index,
                [&](const FT_Bitmap& bitmap, FT_Int bitmap_left,
                    FT_Int bitmap_top) {
                    left = origin_x + bitmap_left;
                    top = -origin_y - bitmap_top;
                    width = static_cast<std::int64_t>(bitmap.width);
                    rows = static_cast<std::int64_t>(bitmap.rows);
                });
            if (error != 0) {
                return failure(TextErrorCode::raster_error,
                               ft_error("glyph bitmap render", error), key);
            }
            glyphs.push_back(PlacedGlyph{glyph_index, left, top});

            if (width > 0 && rows > 0) {
                if (!has_ink) {
                    minimum_x = left;
                    minimum_y = top;
                    maximum_x = left + width;
                    maximum_y = top + rows;
                    has_ink = true;
                } else {
                    minimum_x = std::min(minimum_x, left);
                    minimum_y = std::min(minimum_y, top);
                    maximum_x = std::max(maximum_x, left + width);
                    maximum_y = std::max(maximum_y, top + rows);
                }
            }
            pen_x += glyph_positions[index].x_advance;
            pen_y += glyph_positions[index].y_advance;
        }

        const auto bitmap_width = has_ink ? maximum_x - minimum_x : 0;
        const auto bitmap_height = has_ink ? maximum_y - minimum_y : 0;
        if (bitmap_width < 0 || bitmap_height < 0 ||
            bitmap_width > config.limits.maximum_bitmap_dimension ||
            bitmap_height > config.limits.maximum_bitmap_dimension) {
            return failure(TextErrorCode::resource_limit,
                           "rasterized ink bounds exceed configured dimensions",
                           key);
        }
        const auto pixel_count =
            static_cast<std::uint64_t>(bitmap_width) * static_cast<std::uint64_t>(bitmap_height);
        const auto byte_count = pixel_count * 4U;
        if (byte_count > config.limits.maximum_bitmap_bytes ||
            byte_count > std::numeric_limits<std::size_t>::max()) {
            return failure(TextErrorCode::resource_limit,
                           "rasterized bitmap exceeds configured byte limit",
                           key);
        }

        auto output = std::make_shared<RasterizedText>();
        output->transformed_utf8 = text;
        output->pixel_height = pixel_height;
        output->bitmap.width = static_cast<std::uint32_t>(bitmap_width);
        output->bitmap.height = static_cast<std::uint32_t>(bitmap_height);
        output->bitmap.row_stride_bytes = output->bitmap.width * 4U;
        output->bitmap.pixels.assign(static_cast<std::size_t>(byte_count), 0U);

        const auto advance = pen_x < 0 ? -pen_x : pen_x;
        const auto advance_pixels = ceil_26_6(advance);
        const auto content_left_pixels = has_content ? floor_26_6(minimum_content_x) : 0;
        const auto content_width_26_6 =
            has_content ? maximum_content_x - minimum_content_x : 0;
        // FTGL truncated Spades' fractional outline box. Current FreeType's
        // A750 autohinter is half a pixel narrower than the 2015 build, so its
        // nearest-pixel box restores the measured retail name-plate width.
        const auto content_width_pixels =
            resolved_font_path.filename() == "Spades.ttf"
                ? content_width_26_6 / 64
                : (content_width_26_6 + 32) / 64;
        const auto ascender = ceil_26_6(face->size->metrics.ascender);
        const auto descender = -floor_26_6(face->size->metrics.descender);
        const auto line_height = ceil_26_6(face->size->metrics.height);
        if (advance_pixels > std::numeric_limits<std::int32_t>::max() ||
            content_left_pixels < std::numeric_limits<std::int32_t>::min() ||
            content_left_pixels > std::numeric_limits<std::int32_t>::max() ||
            content_width_pixels < 0 ||
            content_width_pixels > std::numeric_limits<std::uint32_t>::max() ||
            ascender > std::numeric_limits<std::int32_t>::max() ||
            descender > std::numeric_limits<std::int32_t>::max() ||
            line_height > std::numeric_limits<std::int32_t>::max()) {
            return failure(
                TextErrorCode::resource_limit, "text metrics exceed supported integer range", key);
        }

        output->metrics.advance_width_pixels = static_cast<std::int32_t>(advance_pixels);
        output->metrics.content_left_pixels =
            static_cast<std::int32_t>(content_left_pixels);
        output->metrics.content_width_pixels =
            static_cast<std::uint32_t>(content_width_pixels);
        output->metrics.ascender_pixels = static_cast<std::int32_t>(ascender);
        output->metrics.descender_pixels = static_cast<std::int32_t>(descender);
        output->metrics.line_height_pixels = static_cast<std::int32_t>(line_height);
        output->metrics.ink_left_pixels = static_cast<std::int32_t>(minimum_x);
        output->metrics.ink_top_pixels = static_cast<std::int32_t>(minimum_y);
        output->metrics.ink_width_pixels = output->bitmap.width;
        output->metrics.ink_height_pixels = output->bitmap.height;
        output->metrics.baseline_x_in_bitmap = has_ink ? static_cast<std::int32_t>(-minimum_x) : 0;
        output->metrics.baseline_y_in_bitmap = has_ink ? static_cast<std::int32_t>(-minimum_y) : 0;
        output->metrics.glyph_count = glyph_count;

        for (const auto& glyph : glyphs) {
            bool unsupported_pixel_mode{};
            error = with_rendered_glyph(
                glyph.glyph_index,
                [&](const FT_Bitmap& bitmap, FT_Int, FT_Int) {
                    if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY &&
                        bitmap.pixel_mode != FT_PIXEL_MODE_MONO) {
                        unsupported_pixel_mode = true;
                        return;
                    }
                    const auto destination_x = glyph.left - minimum_x;
                    const auto destination_y = glyph.top - minimum_y;
                    const auto pitch = static_cast<std::ptrdiff_t>(bitmap.pitch);
                    const auto absolute_pitch = pitch >= 0 ? pitch : -pitch;
                    for (std::uint32_t row = 0U; row < bitmap.rows; ++row) {
                        const auto source_row =
                            pitch >= 0
                                ? row
                                : static_cast<std::uint32_t>(bitmap.rows - 1U - row);
                        const auto* source =
                            bitmap.buffer +
                            static_cast<std::ptrdiff_t>(source_row) * absolute_pitch;
                        for (std::uint32_t column = 0U; column < bitmap.width;
                             ++column) {
                            const auto coverage =
                                gray_coverage(bitmap, source, column);
                            if (coverage == 0U) continue;
                            const auto x = destination_x + column;
                            const auto y = destination_y + row;
                            const auto destination =
                                (static_cast<std::size_t>(y) *
                                     output->bitmap.width +
                                 static_cast<std::size_t>(x)) *
                                4U;
                            output->bitmap.pixels[destination] = 255U;
                            output->bitmap.pixels[destination + 1U] = 255U;
                            output->bitmap.pixels[destination + 2U] = 255U;
                            output->bitmap.pixels[destination + 3U] =
                                std::max(output->bitmap.pixels[destination + 3U],
                                         coverage);
                        }
                    }
                });
            if (error != 0) {
                return failure(
                    TextErrorCode::raster_error, ft_error("glyph bitmap replay", error), key);
            }
            if (unsupported_pixel_mode) {
                return failure(TextErrorCode::raster_error,
                               "FreeType produced an unsupported glyph pixel mode",
                               key);
            }
        }

        return {std::move(output), key, TextErrorCode::none, {}, false};
    }

    [[nodiscard]] static TextRasterResult
    failure(TextErrorCode code, std::string message, const TextCacheKey& key) {
        return {nullptr, key, code, std::move(message), false};
    }

    void evict_for(std::size_t incoming_bytes) {
        while (!lru.empty() && (cache.size() >= config.limits.maximum_cache_entries ||
                                stats.bytes + incoming_bytes > config.limits.maximum_cache_bytes)) {
            const auto& key = lru.back();
            const auto found = cache.find(key);
            if (found != cache.end()) {
                stats.bytes -= found->second.bytes;
                cache.erase(found);
                ++stats.evictions;
            }
            lru.pop_back();
        }
    }

    void clear_cache() noexcept {
        std::scoped_lock lock{mutex};
        cache.clear();
        coverage_cache.clear();
        lru.clear();
        stats.entries = 0U;
        stats.bytes = 0U;
    }

    [[nodiscard]] TextCacheStats cache_stats() const noexcept {
        std::scoped_lock lock{mutex};
        auto snapshot = stats;
        snapshot.entries = cache.size();
        return snapshot;
    }
};

TextRasterizer::TextRasterizer(TextRasterizerConfig config)
    : impl_{std::make_unique<Impl>(std::move(config))} {}

TextRasterizer::~TextRasterizer() = default;

TextRasterizer::TextRasterizer(TextRasterizer&&) noexcept = default;

TextRasterizer& TextRasterizer::operator=(TextRasterizer&&) noexcept = default;

bool TextRasterizer::ready() const noexcept {
    return impl_ != nullptr && impl_->ready();
}

TextErrorCode TextRasterizer::initialization_error_code() const noexcept {
    return impl_ == nullptr ? TextErrorCode::not_ready : impl_->init_error_code;
}

std::string_view TextRasterizer::initialization_error() const noexcept {
    static constexpr std::string_view moved_from_error{"text rasterizer was moved from"};
    return impl_ == nullptr ? moved_from_error : std::string_view{impl_->init_error};
}

std::string_view TextRasterizer::font_asset_id() const noexcept {
    return impl_ == nullptr ? std::string_view{} : std::string_view{impl_->font_id};
}

std::uint64_t TextRasterizer::font_fingerprint() const noexcept {
    return impl_ == nullptr ? 0U : impl_->fingerprint;
}

bool TextRasterizer::supports_text(std::string_view utf8, TextCase text_case) const {
    return impl_ != nullptr && impl_->supports_text(utf8, text_case);
}

TextCacheKeyResult TextRasterizer::cache_key(const TextRasterRequest& request) const {
    if (impl_ == nullptr) {
        return {std::nullopt, TextErrorCode::not_ready, "text rasterizer was moved from"};
    }
    return impl_->key_for(request);
}

TextRasterResult TextRasterizer::rasterize(const TextRasterRequest& request) {
    if (impl_ == nullptr) {
        return {nullptr, {}, TextErrorCode::not_ready, "text rasterizer was moved from", false};
    }
    return impl_->rasterize(request);
}

void TextRasterizer::clear_cache() noexcept {
    if (impl_ != nullptr) {
        impl_->clear_cache();
    }
}

TextCacheStats TextRasterizer::cache_stats() const noexcept {
    return impl_ == nullptr ? TextCacheStats{} : impl_->cache_stats();
}

TextRasterizer* select_text_font(std::span<TextRasterizer* const> candidates,
                                 std::string_view utf8, TextCase text_case) {
    TextRasterizer* fallback{};
    for (std::size_t index{}; index < candidates.size(); ++index) {
        auto* const candidate = candidates[index];
        if (candidate == nullptr || !candidate->ready()) continue;
        if (fallback == nullptr) fallback = candidate;
        const auto current = candidates.begin() + static_cast<std::ptrdiff_t>(index);
        if (std::find(candidates.begin(), current, candidate) != current) continue;
        if (candidate->supports_text(utf8, text_case)) return candidate;
    }
    return fallback;
}

} // namespace battlespades::text
