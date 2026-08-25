#include "battlespades/world/map_atmosphere.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

namespace battlespades::world {
namespace {

using Rgb = std::array<float, 3U>;

[[nodiscard]] float luminance(const Rgb& color) noexcept {
    return 0.2126F * color[0U] + 0.7152F * color[1U] + 0.0722F * color[2U];
}

/**
 * Splits a colour into unit-luminance chroma, pulled toward white by `floor`.
 *
 * Without the floor a channel that is exactly zero in the source stays zero
 * after normalisation and starves that channel everywhere it lights. Alcatraz's
 * sunset sun is literally (1.0, 0.447, 0.0); used raw it turns the whole map
 * monochrome orange.
 */
[[nodiscard]] Rgb chroma_of(const Rgb& color, float floor) noexcept {
    const float level = luminance(color);
    if (level < 1.0e-4F) {
        return {1.0F, 1.0F, 1.0F};
    }
    Rgb unit{color[0U] / level, color[1U] / level, color[2U] / level};
    for (float& channel : unit) {
        channel = floor + (1.0F - floor) * channel;
    }
    return unit;
}

[[nodiscard]] std::array<std::uint8_t, 3U> to_srgb_bytes(const Rgb& color) noexcept {
    std::array<std::uint8_t, 3U> bytes{};
    for (std::size_t index{}; index < 3U; ++index) {
        bytes[index] = static_cast<std::uint8_t>(
            std::clamp(std::lround(color[index] * 255.0F), 0L, 255L));
    }
    return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::error_code code;
    const auto size = std::filesystem::file_size(path, code);
    if (code || size == 0U || size > 64U * 1024U * 1024U) {
        return {};
    }
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        return {};
    }
    return bytes;
}

[[nodiscard]] std::string lowercase(std::string_view text) {
    std::string result{text};
    std::ranges::transform(result, result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

/**
 * Resolves a texture basename case-insensitively.
 *
 * Mandatory, not defensive: `mesh/ArcticBase/ARCTIC_SkySphere.aos` binds
 * `t_ARCTIC_SkyGrad.tga` while the shipped file is `t_Arctic_SkyGrad.tga`. It
 * is the only such mismatch in the whole asset tree, and it silently breaks
 * exactly one map on a case-sensitive filesystem.
 */
[[nodiscard]] std::filesystem::path
resolve_case_insensitive(const std::filesystem::path& directory,
                         std::string_view name) {
    const auto exact = directory / name;
    std::error_code code;
    if (std::filesystem::is_regular_file(exact, code) && !code) {
        return exact;
    }
    const auto wanted = lowercase(name);
    for (std::filesystem::directory_iterator iterator{directory, code}, end;
         !code && iterator != end; iterator.increment(code)) {
        if (lowercase(iterator->path().filename().string()) == wanted) {
            return iterator->path();
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Minimal readers. aos_world links no JSON or image library by design, so the
// derivation stays headless and unit-testable; these parse exactly the shapes
// the retail assets use and reject everything else.
// ---------------------------------------------------------------------------

class Cursor final {
public:
    explicit Cursor(std::span<const std::uint8_t> bytes) : bytes_{bytes} {}

    [[nodiscard]] std::optional<std::uint32_t> u32() noexcept {
        if (remaining() < 4U) return std::nullopt;
        const auto value = static_cast<std::uint32_t>(bytes_[offset_]) |
                           (static_cast<std::uint32_t>(bytes_[offset_ + 1U]) << 8U) |
                           (static_cast<std::uint32_t>(bytes_[offset_ + 2U]) << 16U) |
                           (static_cast<std::uint32_t>(bytes_[offset_ + 3U]) << 24U);
        offset_ += 4U;
        return value;
    }

    [[nodiscard]] std::optional<float> f32() noexcept {
        const auto bits = u32();
        if (!bits.has_value()) return std::nullopt;
        float value{};
        const auto raw = *bits;
        std::memcpy(&value, &raw, sizeof(value));
        return std::isfinite(value) ? std::optional{value} : std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> text(std::size_t count) {
        if (count == 0U || count > 127U || count > remaining()) {
            return std::nullopt;
        }
        std::string result{reinterpret_cast<const char*>(bytes_.data() + offset_), count};
        offset_ += count;
        return result;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

/** One `.aos` layer: the geometry plus the texture it binds. */
struct AosMesh final {
    std::string name;
    std::vector<std::array<float, 9U>> vertices; // x y z r g b a u v
    std::string texture_name;
};

[[nodiscard]] std::optional<std::vector<AosMesh>>
parse_aos(const std::filesystem::path& path) {
    const auto bytes = read_file(path);
    if (bytes.empty()) {
        return std::nullopt;
    }
    Cursor reader{bytes};
    static_cast<void>(reader.u32()); // advisory size; one shipped mesh lies
    const auto mesh_count = reader.u32();
    if (!mesh_count.has_value() || *mesh_count == 0U || *mesh_count > 64U) {
        return std::nullopt;
    }
    std::vector<AosMesh> result;
    result.reserve(*mesh_count);
    for (std::uint32_t index{}; index < *mesh_count; ++index) {
        const auto name_length = reader.u32();
        if (!name_length.has_value()) return std::nullopt;
        auto name = reader.text(*name_length);
        const auto vertex_count = reader.u32();
        if (!name.has_value() || !vertex_count.has_value() || *vertex_count == 0U ||
            *vertex_count > 1'000'000U ||
            static_cast<std::uint64_t>(*vertex_count) * 36U > reader.remaining()) {
            return std::nullopt;
        }
        AosMesh mesh;
        mesh.name = std::move(*name);
        mesh.vertices.reserve(*vertex_count);
        for (std::uint32_t vertex{}; vertex < *vertex_count; ++vertex) {
            std::array<float, 9U> values{};
            for (float& value : values) {
                const auto parsed = reader.f32();
                if (!parsed.has_value()) return std::nullopt;
                value = *parsed;
            }
            mesh.vertices.push_back(values);
        }
        const auto texture_length = reader.u32();
        if (!texture_length.has_value()) return std::nullopt;
        auto texture = reader.text(*texture_length);
        if (!texture.has_value()) return std::nullopt;
        mesh.texture_name = std::move(*texture);
        result.push_back(std::move(mesh));
    }
    return result;
}

/** Decoded uncompressed TGA, top row first regardless of the origin bit. */
struct Image final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<Rgb> pixels;
};

[[nodiscard]] std::optional<Image> decode_tga(const std::filesystem::path& path) {
    const auto bytes = read_file(path);
    if (bytes.size() < 18U) {
        return std::nullopt;
    }
    const std::uint8_t id_length = bytes[0U];
    const std::uint8_t image_type = bytes[2U];
    const auto width = static_cast<std::uint32_t>(bytes[12U] | (bytes[13U] << 8U));
    const auto height = static_cast<std::uint32_t>(bytes[14U] | (bytes[15U] << 8U));
    const std::uint8_t bits = bytes[16U];
    const std::uint8_t descriptor = bytes[17U];
    // Only the uncompressed true-colour form retail ships is accepted.
    if (image_type != 2U || (bits != 24U && bits != 32U) || width == 0U ||
        height == 0U || width > 8192U || height > 8192U) {
        return std::nullopt;
    }
    const std::size_t channels = bits / 8U;
    const std::size_t offset = 18U + id_length;
    const std::size_t needed = offset + static_cast<std::size_t>(width) * height * channels;
    if (bytes.size() < needed) {
        return std::nullopt;
    }
    // Bit 5 set means the first stored row is the top one; every shipped sky
    // gradient is bottom-left origin, so the rows normally need flipping.
    const bool top_origin = (descriptor & 0x20U) != 0U;

    Image image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * height);
    for (std::uint32_t row{}; row < height; ++row) {
        const std::uint32_t source_row = top_origin ? row : (height - 1U - row);
        for (std::uint32_t column{}; column < width; ++column) {
            const std::size_t at =
                offset + ((static_cast<std::size_t>(source_row) * width) + column) * channels;
            // TGA stores BGR(A).
            image.pixels[(static_cast<std::size_t>(row) * width) + column] = {
                static_cast<float>(bytes[at + 2U]) / 255.0F,
                static_cast<float>(bytes[at + 1U]) / 255.0F,
                static_cast<float>(bytes[at]) / 255.0F,
            };
        }
    }
    return image;
}

// ---------------------------------------------------------------------------
// Skydome definition reader. The shipped schema is exactly five keys:
// render_list, rotation, scale, translation, uv_speeds.
// ---------------------------------------------------------------------------

struct DomeDefinition final {
    std::vector<std::string> render_list;
    std::vector<std::pair<std::string, std::array<float, 3U>>> rotation;
    std::vector<std::pair<std::string, float>> scale;
    std::vector<std::pair<std::string, std::array<float, 3U>>> translation;
};

[[nodiscard]] std::size_t skip_space(std::string_view text, std::size_t at) noexcept {
    while (at < text.size() && (std::isspace(static_cast<unsigned char>(text[at])) != 0)) {
        ++at;
    }
    return at;
}

[[nodiscard]] std::optional<std::string> read_json_string(std::string_view text,
                                                          std::size_t& at) {
    at = skip_space(text, at);
    if (at >= text.size() || text[at] != '"') {
        return std::nullopt;
    }
    const auto end = text.find('"', at + 1U);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    std::string value{text.substr(at + 1U, end - at - 1U)};
    at = end + 1U;
    return value;
}

[[nodiscard]] std::optional<float> read_json_number(std::string_view text,
                                                    std::size_t& at) {
    at = skip_space(text, at);
    const std::size_t begin = at;
    while (at < text.size() && (std::isdigit(static_cast<unsigned char>(text[at])) != 0 ||
                                text[at] == '-' || text[at] == '+' || text[at] == '.' ||
                                text[at] == 'e' || text[at] == 'E')) {
        ++at;
    }
    if (at == begin) {
        return std::nullopt;
    }
    try {
        return std::stof(std::string{text.substr(begin, at - begin)});
    } catch (...) {
        return std::nullopt;
    }
}

/** Finds a top-level key and returns the offset just past its colon. */
[[nodiscard]] std::optional<std::size_t> find_key(std::string_view text,
                                                  std::string_view key) {
    const std::string needle = "\"" + std::string{key} + "\"";
    const auto at = text.find(needle);
    if (at == std::string_view::npos) {
        return std::nullopt;
    }
    const auto colon = text.find(':', at + needle.size());
    return colon == std::string_view::npos ? std::nullopt : std::optional{colon + 1U};
}

[[nodiscard]] std::optional<DomeDefinition>
parse_dome_definition(const std::filesystem::path& path) {
    const auto bytes = read_file(path);
    if (bytes.empty()) {
        return std::nullopt;
    }
    const std::string_view text{reinterpret_cast<const char*>(bytes.data()), bytes.size()};

    DomeDefinition definition;
    if (const auto start = find_key(text, "render_list"); start.has_value()) {
        auto at = skip_space(text, *start);
        if (at < text.size() && text[at] == '[') {
            ++at;
            while (at < text.size()) {
                at = skip_space(text, at);
                if (at < text.size() && text[at] == ']') break;
                auto entry = read_json_string(text, at);
                if (!entry.has_value()) break;
                definition.render_list.push_back(std::move(*entry));
                at = skip_space(text, at);
                if (at < text.size() && text[at] == ',') ++at;
            }
        }
    }
    if (definition.render_list.empty() || definition.render_list.size() > 64U) {
        return std::nullopt;
    }

    // rotation/translation are name -> [x, y, z]; scale is name -> number.
    const auto read_vector_map =
        [&](std::string_view key,
            std::vector<std::pair<std::string, std::array<float, 3U>>>& into) {
            const auto start = find_key(text, key);
            if (!start.has_value()) return;
            auto at = skip_space(text, *start);
            if (at >= text.size() || text[at] != '{') return;
            ++at;
            while (at < text.size()) {
                at = skip_space(text, at);
                if (at < text.size() && text[at] == '}') break;
                auto name = read_json_string(text, at);
                if (!name.has_value()) break;
                at = skip_space(text, at);
                if (at >= text.size() || text[at] != ':') break;
                ++at;
                at = skip_space(text, at);
                if (at >= text.size() || text[at] != '[') break;
                ++at;
                std::array<float, 3U> value{};
                bool ok = true;
                for (std::size_t axis{}; axis < 3U; ++axis) {
                    const auto number = read_json_number(text, at);
                    if (!number.has_value()) {
                        ok = false;
                        break;
                    }
                    value[axis] = *number;
                    at = skip_space(text, at);
                    if (at < text.size() && text[at] == ',') ++at;
                }
                if (!ok) break;
                at = skip_space(text, at);
                if (at < text.size() && text[at] == ']') ++at;
                at = skip_space(text, at);
                if (at < text.size() && text[at] == ',') ++at;
                into.emplace_back(std::move(*name), value);
            }
        };
    read_vector_map("rotation", definition.rotation);
    read_vector_map("translation", definition.translation);

    if (const auto start = find_key(text, "scale"); start.has_value()) {
        auto at = skip_space(text, *start);
        if (at < text.size() && text[at] == '{') {
            ++at;
            while (at < text.size()) {
                at = skip_space(text, at);
                if (at < text.size() && text[at] == '}') break;
                auto name = read_json_string(text, at);
                if (!name.has_value()) break;
                at = skip_space(text, at);
                if (at >= text.size() || text[at] != ':') break;
                ++at;
                const auto number = read_json_number(text, at);
                if (!number.has_value()) break;
                definition.scale.emplace_back(std::move(*name), *number);
                at = skip_space(text, at);
                if (at < text.size() && text[at] == ',') ++at;
            }
        }
    }
    return definition;
}

template <typename Value>
[[nodiscard]] Value lookup(
    const std::vector<std::pair<std::string, Value>>& entries,
    std::string_view name, Value fallback) {
    const auto found = std::ranges::find(entries, name, &std::pair<std::string, Value>::first);
    return found == entries.end() ? fallback : found->second;
}

/** The renderer's exact authored-transform bake, so directions agree with what is drawn. */
[[nodiscard]] std::array<float, 3U> rotate_xyz(std::array<float, 3U> value,
                                               const std::array<float, 3U>& degrees) noexcept {
    constexpr float radians_per_degree{0.01745329251994329577F};
    const auto rx = degrees[0U] * radians_per_degree;
    const auto ry = degrees[1U] * radians_per_degree;
    const auto rz = degrees[2U] * radians_per_degree;
    const auto sx = std::sin(rx);
    const auto cx = std::cos(rx);
    const auto sy = std::sin(ry);
    const auto cy = std::cos(ry);
    const auto sz = std::sin(rz);
    const auto cz = std::cos(rz);
    value = {value[0U], value[1U] * cx - value[2U] * sx, value[1U] * sx + value[2U] * cx};
    value = {value[0U] * cy + value[2U] * sy, value[1U], -value[0U] * sy + value[2U] * cy};
    return {value[0U] * cz - value[1U] * sz, value[0U] * sz + value[1U] * cz, value[2U]};
}

/**
 * Measured elevation to gradient V.
 *
 * The skysphere UV layout is identical across every shipped dome, and the
 * horizon sits at V = 0.891, not at the midpoint: the sky occupies 89% of the
 * V range and the below-horizon clamp only the last 10%. Sampling the middle
 * row would read well above the horizon on every map.
 */
constexpr std::array<std::pair<float, float>, 10U> elevation_to_v{{
    {90.0F, 0.005F}, {75.0F, 0.065F}, {60.0F, 0.280F}, {45.0F, 0.442F},
    {30.0F, 0.603F}, {20.0F, 0.764F}, {10.0F, 0.857F}, {5.0F, 0.886F},
    {0.0F, 0.891F}, {-90.0F, 0.996F},
}};

[[nodiscard]] float v_for_elevation(float elevation) noexcept {
    if (elevation >= elevation_to_v.front().first) return elevation_to_v.front().second;
    if (elevation <= elevation_to_v.back().first) return elevation_to_v.back().second;
    for (std::size_t index{1U}; index < elevation_to_v.size(); ++index) {
        const auto [high_e, high_v] = elevation_to_v[index - 1U];
        const auto [low_e, low_v] = elevation_to_v[index];
        if (elevation <= high_e && elevation >= low_e) {
            const float span = high_e - low_e;
            const float t = span > 1.0e-6F ? (high_e - elevation) / span : 0.0F;
            return high_v + (low_v - high_v) * t;
        }
    }
    return 0.5F;
}

/**
 * Averages one gradient row across its full width.
 *
 * Never sample a single column: the gradients bake an azimuthal sun lobe into
 * the middle of the image, so a mid-column read is far brighter than the row
 * actually contributes to ambient.
 */
[[nodiscard]] Rgb row_average(const Image& image, float v) {
    const auto row = static_cast<std::uint32_t>(
        std::clamp(std::lround(v * static_cast<float>(image.height - 1U)), 0L,
                   static_cast<long>(image.height - 1U)));
    Rgb total{};
    for (std::uint32_t column{}; column < image.width; ++column) {
        const auto& pixel = image.pixels[(static_cast<std::size_t>(row) * image.width) + column];
        for (std::size_t channel{}; channel < 3U; ++channel) {
            total[channel] += pixel[channel];
        }
    }
    const float inverse = 1.0F / static_cast<float>(image.width);
    return {total[0U] * inverse, total[1U] * inverse, total[2U] * inverse};
}

/**
 * V of the lowest row that is still painted sky rather than the bottom clamp.
 *
 * Most gradients pad their bottom rows with a single flat colour standing in for
 * "below the horizon", and on several domes that padding is pure black. A fixed
 * horizon probe walks straight into it: elevation 2 degrees is V=0.889, which on
 * a 128-row gradient is row 113 -- exactly where Chicago's clamp begins. The
 * result was a fog colour of #010307, so the authored orange fire-glow over a
 * burning city was replaced by near-black, and thickening its fog only pulled
 * the mid-distance further toward black. MayanJungle fails the same way.
 *
 * Rather than hand-tune a probe elevation per dome, find the clamp: scan up from
 * the last row while rows stay within one 8-bit step of it, and report the first
 * row that genuinely differs. Domes with no clamp block return their bottom row,
 * so this cannot move a probe that was already correct.
 */
[[nodiscard]] float lowest_sky_v(const Image& image) noexcept {
    if (image.height <= 1U) {
        return 1.0F;
    }
    const auto last = image.height - 1U;

    std::vector<float> levels;
    levels.reserve(image.height);
    for (std::uint32_t row{}; row < image.height; ++row) {
        levels.push_back(
            luminance(row_average(image, static_cast<float>(row) / static_cast<float>(last))));
    }

    // Threshold against the gradient's OWN median row, not against an absolute
    // level: a night dome's real horizon is darker than a desert dome's zenith,
    // so any fixed cutoff either keeps Chicago's black or discards Tokyo's sky.
    // The median is taken over every row, which is safe because the clamp block
    // is always a minority of the image.
    //
    // Identity-with-the-bottom-row was the obvious test and it is WRONG: these
    // gradients are anti-aliased, so the clamp is preceded by a transition row
    // that differs from pure black by a few 8-bit steps while carrying none of
    // the sky. On Chicago that row is #020308 at luminance 0.0127 against 0.1527
    // one row above -- a twelvefold cliff, and exactly the row an
    // identity test would have accepted as sky.
    std::vector<float> sorted = levels;
    std::ranges::nth_element(sorted, sorted.begin() + (sorted.size() / 2U));
    const float threshold = sorted[sorted.size() / 2U] * 0.2F;

    auto row = last;
    while (row > 0U && levels[row] < threshold) {
        --row;
    }
    return static_cast<float>(row) / static_cast<float>(last);
}

/**
 * Cosine-weighted irradiance over the upper hemisphere.
 *
 * For a diffuse surface facing straight up the irradiance is the integral of
 * radiance times cos(theta) over the hemisphere; substituting elevation gives a
 * sin(e)*cos(e) weight that peaks at 45 degrees, which is exactly where these
 * gradients put their bright band.
 */
[[nodiscard]] Rgb integrate_sky(const Image& image) {
    Rgb total{};
    float weight_total{};
    for (int degrees{}; degrees <= 90; ++degrees) {
        const auto elevation = static_cast<float>(degrees);
        const float radians = elevation * std::numbers::pi_v<float> / 180.0F;
        const float weight = std::sin(radians) * std::cos(radians);
        if (weight <= 0.0F) continue;
        const Rgb row = row_average(image, v_for_elevation(elevation));
        for (std::size_t channel{}; channel < 3U; ++channel) {
            total[channel] += row[channel] * weight;
        }
        weight_total += weight;
    }
    if (weight_total <= 0.0F) {
        return {0.5F, 0.5F, 0.5F};
    }
    return {total[0U] / weight_total, total[1U] / weight_total, total[2U] / weight_total};
}

[[nodiscard]] bool is_sun_texture(std::string_view texture) {
    const auto lower = lowercase(texture);
    // Classify on the texture, not the layer name: SecretBaseB_SUN.aos binds a
    // moon texture and is a night dome.
    return lower.find("skysun") != std::string::npos ||
           lower.find("sun") != std::string::npos ||
           lower.find("moon") != std::string::npos;
}

} // namespace

void clamp_atmosphere_for_play(MapAtmosphere& atmosphere) noexcept {
    atmosphere.ambient_intensity = std::clamp(atmosphere.ambient_intensity, 0.17F, 1.2F);
    atmosphere.key_intensity = std::clamp(atmosphere.key_intensity, 0.0F, 1.2F);
    atmosphere.exposure = std::clamp(atmosphere.exposure, 1.0F, 3.0F);
    atmosphere.specular_strength = std::clamp(atmosphere.specular_strength, 0.0F, 0.6F);
    atmosphere.fog_density = std::clamp(atmosphere.fog_density, 0.2F, 3.0F);
    const float length = std::sqrt(atmosphere.sun_direction[0U] * atmosphere.sun_direction[0U] +
                                   atmosphere.sun_direction[1U] * atmosphere.sun_direction[1U] +
                                   atmosphere.sun_direction[2U] * atmosphere.sun_direction[2U]);
    if (length > 1.0e-4F) {
        for (float& axis : atmosphere.sun_direction) {
            axis /= length;
        }
    } else {
        atmosphere.sun_direction = {0.36F, 0.26F, -0.90F};
    }
}

bool derive_map_atmosphere(const std::filesystem::path& asset_root,
                           std::string_view definition_name,
                           MapAtmosphere& atmosphere, std::string& error) {
    if (definition_name.empty() || definition_name.size() > 64U ||
        definition_name.find('/') != std::string_view::npos ||
        definition_name.find('\\') != std::string_view::npos ||
        !definition_name.ends_with(".txt")) {
        error = "invalid skydome definition name";
        return false;
    }
    const std::string stem{definition_name.substr(0U, definition_name.size() - 4U)};
    const auto mesh_root = asset_root / "mesh" / stem;
    const auto definition = parse_dome_definition(mesh_root / definition_name);
    if (!definition.has_value()) {
        error = "unreadable skydome definition: " + stem;
        return false;
    }

    // render_list[0] is the skysphere on every shipped dome.
    const auto& sphere_layer = definition->render_list.front();
    const auto sphere = parse_aos(mesh_root / (sphere_layer + ".aos"));
    if (!sphere.has_value() || sphere->empty()) {
        error = "unreadable skysphere mesh for " + stem;
        return false;
    }
    const auto gradient_path =
        resolve_case_insensitive(asset_root / "tga", sphere->front().texture_name);
    const auto gradient = gradient_path.empty()
                              ? std::nullopt
                              : decode_tga(gradient_path);
    if (!gradient.has_value()) {
        error = "unreadable sky gradient for " + stem;
        return false;
    }

    // The retail skydome shader is texture * vertex colour, so the sphere's
    // own tint is part of the sky and must scale every sampled row.
    Rgb tint{};
    for (const auto& vertex : sphere->front().vertices) {
        tint[0U] += vertex[3U];
        tint[1U] += vertex[4U];
        tint[2U] += vertex[5U];
    }
    if (!sphere->front().vertices.empty()) {
        const float inverse = 1.0F / static_cast<float>(sphere->front().vertices.size());
        for (float& channel : tint) {
            channel = std::clamp(channel * inverse, 0.0F, 1.0F);
        }
    } else {
        tint = {1.0F, 1.0F, 1.0F};
    }
    const auto tinted = [&tint](Rgb color) {
        return Rgb{color[0U] * tint[0U], color[1U] * tint[1U], color[2U] * tint[2U]};
    };

    MapAtmosphere derived;
    derived.source = stem;

    const Rgb sky_raw = tinted(integrate_sky(*gradient));
    // Two degrees up, not exactly zero: on a 64px gradient the horizon row sits
    // one texel from the below-horizon clamp block. Taller gradients move that
    // clamp, so never sample below the last genuinely painted sky row -- see
    // lowest_sky_v for the two domes this was silently destroying.
    const Rgb horizon_raw = tinted(
        row_average(*gradient, std::min(v_for_elevation(2.0F), lowest_sky_v(*gradient))));
    const Rgb zenith_raw = tinted(row_average(*gradient, v_for_elevation(88.0F)));
    Rgb ground_raw = tinted(row_average(*gradient, v_for_elevation(-45.0F)));
    // A real ground bounce is duller and dimmer than the painted band.
    const float ground_level = luminance(ground_raw);
    for (float& channel : ground_raw) {
        channel = (channel * 0.6F + ground_level * 0.4F) * 0.75F;
    }

    // Ambient chroma is pulled hard toward white. An earlier 0.55 floor let a
    // saturated sky dominate: Classic_B's cyan gradient normalised to unit
    // luminance is (0.58, 1.11, 1.15), which cut every block's red by a quarter
    // and turned neutral grey stone visibly teal. The map's atmosphere should
    // tint the world, not repaint it, so the sky now contributes about a tenth
    // of the hue and the blocks keep their own identity.
    derived.sky_ambient = chroma_of(sky_raw, 0.88F);
    // The ground term must be genuinely DIMMER, not merely differently hued.
    // Both vectors are unit-luminance by construction, so mixing them by
    // surface orientation produced identical brightness for a floor and a
    // ceiling and the hemispheric term contributed no shape at all. Carrying
    // the measured sky-to-ground level ratio here is what makes upward faces
    // read as sky-lit and downward faces as shadowed.
    const float ground_ratio =
        std::clamp(luminance(ground_raw) / std::max(luminance(sky_raw), 1.0e-4F),
                   0.45F, 0.85F);
    derived.ground_ambient = chroma_of(ground_raw, 0.88F);
    for (float& channel : derived.ground_ambient) {
        channel *= ground_ratio;
    }
    derived.horizon_color = to_srgb_bytes(horizon_raw);
    derived.zenith_color = to_srgb_bytes(zenith_raw);
    derived.fog_color = derived.horizon_color;

    // Never scale the world by raw sky luminance: it spans a 24x range across
    // the shipped domes, and six maps become unplayable if taken literally.
    const float sky_level = std::clamp(luminance(sky_raw), 0.0F, 1.0F);
    // Raised from 0.17 after playtesting: night maps were technically legible
    // but felt dead, which is the opposite of what a neon city should feel like.
    // Chroma is still untouched, so a brighter night stays unmistakably night.
    constexpr float floor_intensity{0.26F};
    derived.ambient_intensity =
        floor_intensity + (1.0F - floor_intensity) * std::pow(sky_level, 0.62F);
    derived.exposure = 1.0F / std::max(0.35F, std::pow(std::max(sky_level, 1.0e-3F), 0.35F));
    derived.specular_strength = 0.08F + 0.16F * sky_level;
    derived.fog_density = 1.0F;

    // Locate the sun or moon and turn its baked position into a direction.
    bool has_key{};
    for (const auto& layer : definition->render_list) {
        const auto mesh = parse_aos(mesh_root / (layer + ".aos"));
        if (!mesh.has_value() || mesh->empty()) {
            continue;
        }
        const auto& candidate = mesh->front();
        if (candidate.vertices.size() != 6U || !is_sun_texture(candidate.texture_name)) {
            continue;
        }
        std::array<float, 3U> centroid{};
        Rgb color{};
        for (const auto& vertex : candidate.vertices) {
            centroid[0U] += vertex[0U];
            centroid[1U] += vertex[1U];
            centroid[2U] += vertex[2U];
            color[0U] += vertex[3U];
            color[1U] += vertex[4U];
            color[2U] += vertex[5U];
        }
        for (std::size_t axis{}; axis < 3U; ++axis) {
            centroid[axis] /= 6.0F;
            color[axis] /= 6.0F;
        }
        const float scale = lookup(definition->scale, layer, 1.0F);
        const auto rotation = lookup(definition->rotation, layer, std::array<float, 3U>{});
        const auto translation =
            lookup(definition->translation, layer, std::array<float, 3U>{});
        auto position = rotate_xyz(
            {centroid[0U] * scale, centroid[1U] * scale, centroid[2U] * scale}, rotation);
        position[0U] += translation[0U];
        position[1U] += translation[1U];
        position[2U] += translation[2U];
        // The renderer's y-up to canonical z-down swizzle.
        const std::array<float, 3U> canonical{position[0U], position[2U], -position[1U]};
        const float length = std::sqrt(canonical[0U] * canonical[0U] +
                                       canonical[1U] * canonical[1U] +
                                       canonical[2U] * canonical[2U]);
        if (length < 1.0e-3F) {
            continue;
        }
        const std::array<float, 3U> unit{canonical[0U] / length, canonical[1U] / length,
                                          canonical[2U] / length};
        // Reject anything at or below the horizon: it is distant scenery, not
        // a light. A river panorama sits just under five degrees.
        const float elevation_degrees =
            std::asin(std::clamp(-unit[2U], -1.0F, 1.0F)) * 180.0F / std::numbers::pi_v<float>;
        if (elevation_degrees <= 5.0F) {
            continue;
        }
        derived.sun_direction = unit;
        // The key light keeps more of its character than ambient does, because a
        // warm low sun is the point on a map like Alcatraz. Still floored: that
        // sun's authored colour has blue at exactly 0.0, and used raw it drains
        // the blue channel out of every lit surface.
        derived.sun_color = chroma_of(color, 0.68F);
        has_key = true;
        break;
    }
    // A dome with no sun or moon is genuinely overcast, and a full-strength key
    // would cast a direction the painted sky visibly contradicts. But dropping
    // to zero is worse: hemispheric ambient alone differentiates only up from
    // down, so on a uniform sky such as Classic_B all four walls of a corridor
    // light identically and the voxel geometry stops reading. Overcast skies
    // are never perfectly uniform anyway, so a weak off-axis bias is both
    // defensible and necessary. NONRETAIL playability choice, same class as the
    // brightness floor below.
    // An overcast sky is not uniform: it is brightest where the sun sits behind
    // it, which is why an overcast day still casts soft shadows. Weak enough to
    // stay believable under a flat sky, strong enough that shadows actually
    // read on domes with no sun layer at all -- Classic_B, the Training map, is
    // one of them, so a key too weak here means the player never sees a shadow.
    constexpr float overcast_key{0.34F};
    derived.key_intensity =
        has_key ? (0.30F + 0.45F * std::sqrt(sky_level)) : overcast_key;

    clamp_atmosphere_for_play(derived);
    atmosphere = std::move(derived);
    error.clear();
    return true;
}

const AtmosphereOverride*
authored_atmosphere_override(std::string_view definition_name) noexcept {
    // Art direction for every shipped dome, from a measured survey of each
    // gradient TGA, each render_list, and each map's voxel histogram.
    //
    // Read the derivation's two systematic blind spots first, because most of
    // these entries exist to correct one of them:
    //
    //  1. `key = 0.30 + 0.45 * sqrt(sky_level)` infers the sun from how bright
    //     the SKY is. That inverts wherever the sun is separate from its sky --
    //     hardest on LunarBase, where a bare white disc hangs in a black vacuum
    //     and the formula reads the vacuum. It also cannot tell a sun from a
    //     moon, which is why the night domes all pull the key down.
    //  2. Nothing in the derivation looks at what the map is MADE OF. A sky says
    //     nothing about how much light the ground returns, so the darkest-albedo
    //     maps (Colosseum at mean luminance 0.161, GreatWall 0.225, Classic
    //     0.227) need ambient raised or they render as silhouettes, while the
    //     snow maps need it cut or they flatten.
    //
    // Values only where measurement justified them: a dome whose derivation is
    // already right is deliberately absent rather than pinned. Order matches the
    // survey shards.

    // -- Desert and warm ------------------------------------------------------
    // The +45 halo is 1.6x the zenith, so this sky is sun-dominated, yet the
    // derivation made key (0.68) LESS than ambient (0.86) -- shaded and sunlit
    // sandstone came out only 1.79x apart. Two Sandstorm layers scroll faster
    // here than on any other dome, and the horizon sits 0.317 in luminance below
    // the 45-degree band, which is near-ground dust extinction.
    static constexpr AtmosphereOverride egypt{.key_intensity = 0.85F,
                                              .fog_density = 1.5F,
                                              .specular_strength = 0.10F,
                                              .ambient_intensity = 0.78F,
                                              .source = "Egypt"};
    // The arena stone averages luminance 0.161, 3.6x darker than Egypt's
    // sandstone under a near-identical sky. At the derived 0.83 ambient, shaded
    // stone rendered at 0.134 -- effectively black. Ambient is the lever here,
    // not exposure: dropping exposure to its floor would lift it only ~5%.
    static constexpr AtmosphereOverride colosseum{.key_intensity = 0.82F,
                                                  .fog_density = 1.3F,
                                                  .specular_strength = 0.10F,
                                                  .ambient_intensity = 1.00F,
                                                  .source = "Colosseum"};
    // The sun's authored #FF7200 normalises to a red multiplier of 1.281, so the
    // derived key was casting a RED component brighter than Egypt's near-white
    // sun under a sky 2.25x dimmer. The only dome shipping a dedicated fog-bank
    // mesh, with a city panorama behind it to occlude. Specular is raised, not
    // lowered: a 10-degree sun over bay water is peak grazing-Fresnel geometry
    // and it is the only drama this desaturated palette has.
    static constexpr AtmosphereOverride alcatraz{.key_intensity = 0.46F,
                                                 .fog_density = 1.9F,
                                                 .specular_strength = 0.28F,
                                                 .source = "Alcatraz"};
    // The gradient is INVERTED -- horizon brighter than zenith -- which is what a
    // sky looks like with the sun behind a smoke ceiling. Six SkySmoke plumes,
    // the most of any dome. Specular near zero: this is mud and sandbags.
    static constexpr AtmosphereOverride ww1{.key_intensity = 0.44F,
                                            .fog_density = 1.7F,
                                            .specular_strength = 0.06F,
                                            .source = "WW1"};

    // -- Night ----------------------------------------------------------------
    // A moon must not key like a sun, however bright its sky samples. Specular is
    // RAISED against the formula: 0.08 + 0.16 * sky_level collapses on a dark sky
    // and kills the wet-asphalt neon reflection that is this map's whole look.
    // Ambient stays derived -- ~9,400 emissive voxels supply the local light, and
    // raising ambient is exactly what flattens neon contrast.
    static constexpr AtmosphereOverride tokyo{.key_intensity = 0.20F,
                                              .fog_density = 1.35F,
                                              .specular_strength = 0.22F,
                                              .exposure = 1.85F,
                                              .source = "Tokyo"};
    // Same moon case. Chicago's 1.6 fog trial erased the already-dark building
    // silhouettes at mid distance (72% fog at half the draw range) and made
    // interiors read as black even after the horizon-clamp repair. Keep the
    // smoky identity at 1.25, lift only the hemispheric fill, and lower the
    // Reinhard white point so lamp/window spill remains visible without turning
    // the night dome into daylight.
    static constexpr AtmosphereOverride chicago{.key_intensity = 0.22F,
                                                .fog_density = 1.25F,
                                                .specular_strength = 0.20F,
                                                .ambient_intensity = 0.48F,
                                                .exposure = 1.90F,
                                                .source = "Chicago"};
    // The layer is named SUN but binds t_SecretBaseB_Moon.tga: the same
    // moon-keying-like-a-sun case, which slipped through only because this sky is
    // 2.1x brighter than Tokyo's. Above the two city moons because it genuinely
    // is brighter and unobscured, still unmistakably not daylight.
    static constexpr AtmosphereOverride secret_base_night{
        .key_intensity = 0.30F, .fog_density = 1.25F, .source = "SecretBaseNight"};
    // The one real sun among the night-adjacent domes; key and ambient are both
    // correct as derived. Specular is raised because 0.08 + 0.16 * sky_level tops
    // out at 0.24 even for a perfect sky, so this asks to be treated as the full
    // daylight case it measurably is. Fog is CUT: this fog colour is 44x brighter
    // than Chicago's, so equal density washes out instead of deepening.
    static constexpr AtmosphereOverride secret_base{
        .fog_density = 0.85F, .specular_strength = 0.24F, .source = "SecretBase"};

    // -- Cold and wet ---------------------------------------------------------
    // No sun and no moon, so the key is the overcast fallback -- but this map
    // carries 36,448 voxels above luminance 0.75, and at 0.34 the snow saturates.
    // Ambient stays derived because WinterValley shares this dome with the
    // opposite problem: its majority surface is #0C0C0C.
    static constexpr AtmosphereOverride arctic_base{.key_intensity = 0.26F,
                                                    .fog_density = 1.5F,
                                                    .specular_strength = 0.16F,
                                                    .source = "ArcticBase"};
    // Key and ambient are both correct: a real, high, unobstructed sun over the
    // brightest measured sky. The thickest fog bank in its shard, and a strong
    // blue fog colour, so density reads as sea haze rather than washout.
    static constexpr AtmosphereOverride atlantis{
        .fog_density = 1.35F, .specular_strength = 0.30F, .source = "Atlantis"};
    // Two nearly opaque cloud decks whose vertex tints multiply the sky down to
    // about a third of white, plus two rain sheets. Specular is deliberately the
    // OPPOSITE call from BranCastle's: rain means every horizontal surface is
    // wet, and wet-versus-dry is the distinction the sky-brightness formula
    // cannot see. Ambient stays derived -- 23% of this map is one brick at
    // luminance 0.181 and any cut makes it unplayable.
    static constexpr AtmosphereOverride london{.key_intensity = 0.24F,
                                               .fog_density = 2.2F,
                                               .specular_strength = 0.26F,
                                               .source = "London"};
    // The one dome where the overcast fallback does exactly what it claims: no
    // sun layer, yet a real diffuse bright patch at 1.24x the row mean. Only fog
    // moves, and modestly -- the render_list authors no fog layer at all, but it
    // fakes aerial perspective with three stacked headland bands.
    static constexpr AtmosphereOverride ww2_docklands{.fog_density = 1.3F,
                                                      .source = "WW2Docklands"};

    // -- Overcast and daylight ------------------------------------------------
    // The lowest-albedo map surveyed, by 2x: albedo caps at 0.495 with a median
    // of 0.227, so the derived ambient rendered the median surface at 0.18.
    // Specular is cut because the azimuthal peak is 1.05x -- there is no bright
    // region for a highlight to sample, and the derived value invents one.
    static constexpr AtmosphereOverride classic{
        .specular_strength = 0.10F, .ambient_intensity = 0.95F, .source = "Classic"};
    // Classic_B and User_Grassland bind the SAME gradient asset, so they share
    // one entry: divergence would be an authoring inconsistency, not a choice.
    // Measured azimuthal variance is exactly zero -- min == max == mean -- so a
    // specular lobe has literally nothing to reflect. Ambient is bounded by
    // BlockNess, where 45% of the surface is above V 0.85: keeping snow inside
    // the tonemap alongside a 0.34 key requires ambient <= 1.0/0.919 - 0.34.
    // The key is deliberately left derived; its 0.34 constant was tuned on this
    // exact dome, and on a zero-variance sky it is the only thing separating the
    // four walls of a corridor.
    static constexpr AtmosphereOverride classic_b{
        .specular_strength = 0.12F, .ambient_intensity = 0.74F, .source = "ClassicB"};
    // The strongest directional signal of its shard -- a 1.28x azimuthal peak
    // with a 0.9725 peak pixel -- plus two SkySunBeams layers drawn specifically
    // to sell god-rays. The derived key is depressed because the murky brown
    // zenith drags the hemispheric mean down while the sun itself is unattenuated.
    static constexpr AtmosphereOverride frontier{.key_intensity = 0.85F,
                                                 .fog_density = 1.6F,
                                                 .specular_strength = 0.12F,
                                                 .source = "Frontier"};
    // Two dedicated mist layers plus a SunBeams layer: authored as a hazy dawn,
    // and shafts only read against thick air. Fog here SEPARATES rather than
    // muddies, because the fog colour is 2.7x brighter than the median albedo.
    // Ambient is raised for the second-darkest map measured.
    static constexpr AtmosphereOverride great_wall{
        .fog_density = 1.8F, .ambient_intensity = 0.90F, .source = "GreatWall"};
    // The dome needing the most correction. Its sky_level is measured AFTER a
    // 0.72 sphere tint that retail applied to the sky as DRAWN, not to the light
    // it casts, and both maps on it sit at median albedo 0.24 -- CastleWars has
    // no emissive fixture anywhere, so ambient is the only fill it will ever get.
    // The key rises off the overcast constant because this gradient's azimuthal
    // variance is 1.66x, the highest surveyed, where that constant was tuned on a
    // sky whose variance is exactly zero. Fog is capped low deliberately: the fog
    // colour separates from the terrain by only 0.04 luminance, so past ~1.3 it
    // erases the depth cues it is meant to add.
    static constexpr AtmosphereOverride invasion{
        .key_intensity = 0.45F,
        .fog_density = 1.3F,
        .ambient_intensity = 0.62F,
        // Every warm layer in this dome clusters within four degrees of azimuth
        // 172 at 10-34 degrees elevation, while the sunless fallback lights from
        // ~64 degrees at azimuth 54 -- the world was lit from a direction its own
        // painted sky contradicts.
        .sun_direction = std::array<float, 3U>{0.129F, 0.918F, -0.375F},
        .source = "Invasion"};

    // -- Storm and jungle -----------------------------------------------------
    // Fog cut from the 2.4 first authored by eye: at 2.4 a target is 51% fogged
    // at 32 blocks and 94% at 64, which hides the candles rather than giving them
    // something to glow through. 1.8 is still unmistakably a storm at 33%/80%.
    static constexpr AtmosphereOverride bran_castle{.key_intensity = 0.24F,
                                                    .fog_density = 1.8F,
                                                    .specular_strength = 0.06F,
                                                    .source = "BranCastle"};
    // An unobscured low sun at alpha 1.0 with a 1.32x lobe, but derived key 0.58
    // against ambient 0.671 is only a 1.50x facing contrast, which reads overcast
    // under a full sunset. Lowering ambient buys the key its headroom.
    static constexpr AtmosphereOverride mayan_jungle{
        .key_intensity = 0.70F, .ambient_intensity = 0.60F, .source = "MayanJungle"};
    // The clearest case of the derivation measuring the wrong light. A bare white
    // sun at alpha 1.0 with no cloud and no fog layer anywhere in the render_list
    // -- vacuum sunlight is the definition of a hard key -- yet the formula reads
    // the black sky and returns 0.38. Ambient is cut because 82% of its derived
    // value was the playability floor rather than a measurement, and specular is
    // raised because the shader's specular term is multiplied by the SUN colour
    // and gated by the shadow map: it is the sun's highlight, not the sky's.
    // Fog stays at 1.0 despite a vacuum having none -- the shader hard-snaps fog
    // to 1.0 at the cull distance, so a lower density shows the cull edge as a
    // visible pop.
    static constexpr AtmosphereOverride lunar_base{.key_intensity = 0.95F,
                                                   .specular_strength = 0.22F,
                                                   .ambient_intensity = 0.20F,
                                                   .exposure = 1.7F,
                                                   .source = "LunarBase"};
    // Not a uniform overcast: the 20-degree band peaks at 1.40x its row mean,
    // which is a hazy sun. Capped deliberately low because the sun direction is
    // still the unmeasured fallback at ~64 degrees while the lobe is at ~19, so a
    // harder key would cast shadows the painted sky contradicts. Fog also fixes a
    // real artefact, cutting the chunk-cull pop from 0.141 to about 0.05.
    static constexpr AtmosphereOverride ww2{.key_intensity = 0.46F,
                                            .fog_density = 1.25F,
                                            .ambient_intensity = 0.60F,
                                            .source = "WW2"};

    struct Entry final {
        std::string_view dome;
        const AtmosphereOverride* art;
    };
    static constexpr std::array<Entry, 22U> table{{
        {"Egypt.txt", &egypt},
        {"Colosseum.txt", &colosseum},
        {"Alcatraz.txt", &alcatraz},
        {"WW1.txt", &ww1},
        {"Tokyo.txt", &tokyo},
        {"Chicago.txt", &chicago},
        {"SecretBase_Night.txt", &secret_base_night},
        {"SecretBase.txt", &secret_base},
        {"ArcticBase.txt", &arctic_base},
        {"Atlantis.txt", &atlantis},
        {"London.txt", &london},
        {"WW2_DockLands.txt", &ww2_docklands},
        {"Classic.txt", &classic},
        {"Classic_B.txt", &classic_b},
        {"User_Grassland.txt", &classic_b},
        {"Frontier.txt", &frontier},
        {"GreatWall.txt", &great_wall},
        {"Invasion.txt", &invasion},
        {"BranCastle.txt", &bran_castle},
        {"MayanJungle.txt", &mayan_jungle},
        {"LunarBase.txt", &lunar_base},
        {"WW2.txt", &ww2},
    }};
    const auto found = std::ranges::find(table, definition_name, &Entry::dome);
    return found == table.end() ? nullptr : found->art;
}

MapAtmosphere resolve_map_atmosphere(const std::filesystem::path& asset_root,
                                     std::string_view definition_name) {
    MapAtmosphere atmosphere;
    std::string error;
    if (!derive_map_atmosphere(asset_root, definition_name, atmosphere, error)) {
        // A malformed or missing dome keeps the safe default rather than
        // blanking the world's lighting.
        return atmosphere;
    }
    if (const auto* art = authored_atmosphere_override(definition_name);
        art != nullptr) {
        // Only what the override actually names; everything else keeps its
        // derived value.
        if (art->key_intensity.has_value()) atmosphere.key_intensity = *art->key_intensity;
        if (art->fog_density.has_value()) atmosphere.fog_density = *art->fog_density;
        if (art->specular_strength.has_value()) {
            atmosphere.specular_strength = *art->specular_strength;
        }
        if (art->ambient_intensity.has_value()) {
            atmosphere.ambient_intensity = *art->ambient_intensity;
        }
        if (art->exposure.has_value()) {
            atmosphere.exposure = *art->exposure;
        }
        if (art->sun_direction.has_value()) {
            // Normalised on apply so an authored direction can be written as the
            // readable approximation it is, rather than as six decimal places
            // that only look precise.
            const auto& wanted = *art->sun_direction;
            const float length = std::sqrt((wanted[0U] * wanted[0U]) +
                                           (wanted[1U] * wanted[1U]) +
                                           (wanted[2U] * wanted[2U]));
            if (length > 1.0e-6F) {
                atmosphere.sun_direction = {wanted[0U] / length, wanted[1U] / length,
                                            wanted[2U] / length};
            }
        }
        atmosphere.source += "+";
        atmosphere.source += art->source;
    }
    clamp_atmosphere_for_play(atmosphere);
    return atmosphere;
}

} // namespace battlespades::world
