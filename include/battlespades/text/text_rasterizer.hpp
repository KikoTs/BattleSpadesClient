#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::text {

/** Case mapping supported without introducing a locale service dependency. */
enum class TextCase : std::uint8_t {
    preserve,
    ascii_uppercase,
    /** Unicode simple-uppercase mapping used by localized UI labels. */
    unicode_uppercase,
};

enum class TextErrorCode : std::uint8_t {
    none,
    not_ready,
    invalid_configuration,
    invalid_request,
    invalid_utf8,
    resource_limit,
    font_error,
    shaping_error,
    raster_error,
};

/** Hard limits applied before allocation or native font work. */
struct TextRasterizerLimits final {
    std::size_t maximum_utf8_bytes{4U * 1'024U};
    std::size_t maximum_glyphs{2U * 1'024U};
    std::uint32_t maximum_pixel_height{256U};
    std::uint32_t maximum_bitmap_dimension{4U * 1'024U};
    std::size_t maximum_bitmap_bytes{16U * 1'024U * 1'024U};
    std::size_t maximum_cache_entries{256U};
    std::size_t maximum_cache_bytes{32U * 1'024U * 1'024U};
};

/**
 * Immutable font and cache configuration for one rasterizer.
 *
 * `font_asset` may be relative or absolute, but its resolved target must stay
 * below `asset_root`. Use one instance per font face; requests may override the
 * default pixel height, so Spades 24/36 can safely share an instance.
 */
struct TextRasterizerConfig final {
    std::filesystem::path asset_root;
    std::filesystem::path font_asset;
    std::uint32_t default_pixel_height{16U};
    TextRasterizerLimits limits{};
    /** Render the font's .notdef glyph for unsupported scalars in live UI text. */
    bool allow_missing_glyphs{false};
};

/** Already-localized UTF-8 input. Localization lookup remains upstream. */
struct TextRasterRequest final {
    std::string utf8;
    TextCase text_case{TextCase::preserve};
    std::optional<std::uint32_t> pixel_height;
    /** Use retail FTTextureGlyph's 140/64px outside stroke bitmap. */
    bool retail_outline_stroke{false};
};

/** Stable key material for diskless, per-process raster cache lookup. */
struct TextCacheKey final {
    std::string canonical;
    std::uint64_t stable_hash{};

    [[nodiscard]] friend bool operator==(const TextCacheKey&, const TextCacheKey&) = default;
};

struct TextCacheKeyResult final {
    std::optional<TextCacheKey> key;
    TextErrorCode error_code{TextErrorCode::none};
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return key.has_value();
    }
};

/** Metrics use a top-left-native Y axis; ink top may be negative. */
struct TextMetrics final {
    std::int32_t advance_width_pixels{};
    std::int32_t content_left_pixels{};
    std::uint32_t content_width_pixels{};
    std::int32_t ascender_pixels{};
    std::int32_t descender_pixels{};
    std::int32_t line_height_pixels{};
    std::int32_t ink_left_pixels{};
    std::int32_t ink_top_pixels{};
    std::uint32_t ink_width_pixels{};
    std::uint32_t ink_height_pixels{};
    std::int32_t baseline_x_in_bitmap{};
    std::int32_t baseline_y_in_bitmap{};
    std::size_t glyph_count{};
};

/** Straight-alpha white glyph coverage in tightly packed RGBA8 rows. */
struct Rgba8Bitmap final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t row_stride_bytes{};
    std::vector<std::uint8_t> pixels;
};

/** Immutable output suitable for a transient bgfx texture or atlas upload. */
struct RasterizedText final {
    std::string transformed_utf8;
    std::uint32_t pixel_height{};
    TextMetrics metrics{};
    Rgba8Bitmap bitmap{};
};

struct TextRasterResult final {
    std::shared_ptr<const RasterizedText> output;
    TextCacheKey cache_key{};
    TextErrorCode error_code{TextErrorCode::none};
    std::string error;
    bool from_cache{false};

    [[nodiscard]] explicit operator bool() const noexcept {
        return output != nullptr;
    }
};

struct TextCacheStats final {
    std::uint64_t hits{};
    std::uint64_t misses{};
    std::uint64_t evictions{};
    std::size_t entries{};
    std::size_t bytes{};
};

/**
 * Bounded FreeType/HarfBuzz CPU shaping and rasterization service.
 *
 * The object owns all native handles and mutable cache state. Public methods
 * serialize access because an FT_Face is not concurrently mutable. Returned
 * images are immutable and may outlive cache eviction through shared ownership.
 * Configuration/font failures set `ready() == false`; rendering then fails
 * closed with `initialization_error()` and never emits partial glyph data.
 */
class TextRasterizer final {
public:
    explicit TextRasterizer(TextRasterizerConfig config);
    ~TextRasterizer();

    TextRasterizer(const TextRasterizer&) = delete;
    TextRasterizer& operator=(const TextRasterizer&) = delete;
    TextRasterizer(TextRasterizer&&) noexcept;
    TextRasterizer& operator=(TextRasterizer&&) noexcept;

    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] TextErrorCode initialization_error_code() const noexcept;
    [[nodiscard]] std::string_view initialization_error() const noexcept;
    [[nodiscard]] std::string_view font_asset_id() const noexcept;
    [[nodiscard]] std::uint64_t font_fingerprint() const noexcept;

    /**
     * Returns true only when every visible Unicode scalar has a real glyph.
     *
     * This is intentionally separate from rasterization so the frontend can
     * select a locale fallback face before HarfBuzz turns a missing character
     * into glyph zero (the visible square/question-mark failure mode).
     */
    [[nodiscard]] bool supports_text(std::string_view utf8,
                                     TextCase text_case = TextCase::preserve) const;

    /** Validates and normalizes input without touching FreeType mutable state. */
    [[nodiscard]] TextCacheKeyResult cache_key(const TextRasterRequest& request) const;

    /** Shapes, measures and rasterizes one line of already-localized UTF-8. */
    [[nodiscard]] TextRasterResult rasterize(const TextRasterRequest& request);

    void clear_cache() noexcept;
    [[nodiscard]] TextCacheStats cache_stats() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/** Prefer complete coverage; otherwise retain the first ready face for .notdef fallback. */
[[nodiscard]] TextRasterizer* select_text_font(
    std::span<TextRasterizer* const> candidates, std::string_view utf8,
    TextCase text_case = TextCase::preserve);

} // namespace battlespades::text
