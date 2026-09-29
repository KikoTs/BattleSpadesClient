#include "battlespades/frontend/ugc_preview_writer.hpp"

#include "battlespades/frontend/ugc_project_repository.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <ranges>
#include <system_error>

namespace battlespades::frontend {
namespace {

struct PngSink final {
    std::vector<std::uint8_t> bytes;
    bool failed{};
};

void append_png_bytes(void* context, void* data, int size) noexcept {
    auto* sink = static_cast<PngSink*>(context);
    if (sink == nullptr || data == nullptr || size <= 0 || sink->failed) return;
    try {
        const auto* first = static_cast<const std::uint8_t*>(data);
        sink->bytes.insert(sink->bytes.end(), first, first + size);
    } catch (...) {
        sink->failed = true;
    }
}

[[nodiscard]] bool regular_non_symlink(const std::filesystem::path& path) noexcept {
    std::error_code code;
    const bool symlink = std::filesystem::is_symlink(path, code);
    if (code || symlink) return false;
    code.clear();
    return std::filesystem::is_regular_file(path, code) && !code;
}

} // namespace

bool write_ugc_overhead_preview(const std::filesystem::path& maps_root,
                                std::string_view project_title,
                                std::span<const std::uint8_t> rgba,
                                std::uint32_t width,
                                std::uint32_t height,
                                std::vector<std::byte>& png_bytes,
                                std::string& error) noexcept {
    png_bytes.clear();
    error.clear();
    try {
        constexpr std::uint32_t maximum_preview_extent{4'096U};
        if (project_title.empty() || width == 0U || height == 0U ||
            width > maximum_preview_extent || height > maximum_preview_extent ||
            width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
            height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
            error = "invalid UGC preview dimensions or title";
            return false;
        }
        const auto required = static_cast<std::uint64_t>(width) * height * 4U;
        if (required != rgba.size() || required > std::numeric_limits<std::size_t>::max()) {
            error = "UGC preview pixels do not match the declared RGBA extent";
            return false;
        }

        const auto scan = scan_ugc_projects(maps_root);
        const auto project = std::ranges::find_if(scan.maps, [&](const auto& candidate) {
            return candidate.title == project_title;
        });
        if (project == scan.maps.end()) {
            error = "the active UGC project is not present in the publish catalog";
            return false;
        }

        std::error_code code;
        const auto root = std::filesystem::weakly_canonical(maps_root, code);
        if (code || !std::filesystem::is_directory(root)) {
            error = "cannot resolve the UGC preview catalog";
            return false;
        }
        const auto sidecar = root / std::filesystem::path{project->uid};
        if (!regular_non_symlink(sidecar)) {
            error = "the selected UGC project sidecar is unsafe";
            return false;
        }
        auto output = sidecar;
        output.replace_extension(".png");
        if (std::filesystem::is_symlink(output, code) && !code) {
            error = "the UGC preview path is a symlink";
            return false;
        }
        code.clear();

        PngSink sink;
        sink.bytes.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(required, 1U << 20U)));
        const auto encoded = stbi_write_png_to_func(append_png_bytes,
                                                    &sink,
                                                    static_cast<int>(width),
                                                    static_cast<int>(height),
                                                    4,
                                                    rgba.data(),
                                                    static_cast<int>(width * 4U));
        if (encoded == 0 || sink.failed || sink.bytes.empty()) {
            error = "PNG encoding failed for the UGC overhead preview";
            return false;
        }

        auto temporary = output;
        temporary += ".tmp";
        if (std::filesystem::is_symlink(temporary, code) && !code) {
            error = "the UGC preview temporary path is a symlink";
            return false;
        }
        code.clear();
        {
            std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
            if (!stream) {
                error = "cannot create the UGC preview temporary file";
                return false;
            }
            stream.write(reinterpret_cast<const char*>(sink.bytes.data()),
                         static_cast<std::streamsize>(sink.bytes.size()));
            if (!stream) {
                error = "cannot write the complete UGC preview PNG";
                return false;
            }
        }
        std::filesystem::remove(output, code);
        code.clear();
        std::filesystem::rename(temporary, output, code);
        if (code) {
            std::filesystem::remove(temporary, code);
            error = "cannot commit the UGC preview PNG";
            return false;
        }

        png_bytes.reserve(sink.bytes.size());
        std::ranges::transform(sink.bytes, std::back_inserter(png_bytes), [](std::uint8_t value) {
            return static_cast<std::byte>(value);
        });
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool encode_png_rgba8(std::span<const std::uint8_t> rgba,
                      std::uint32_t width,
                      std::uint32_t height,
                      std::vector<std::byte>& png_bytes,
                      std::string& error) noexcept {
    png_bytes.clear();
    error.clear();
    try {
        constexpr std::uint32_t maximum_extent{4'096U};
        if (width == 0U || height == 0U || width > maximum_extent || height > maximum_extent ||
            static_cast<std::uint64_t>(width) * height * 4U != rgba.size()) {
            error = "invalid PNG pixel extent";
            return false;
        }
        PngSink sink;
        const auto encoded = stbi_write_png_to_func(append_png_bytes,
                                                    &sink,
                                                    static_cast<int>(width),
                                                    static_cast<int>(height),
                                                    4,
                                                    rgba.data(),
                                                    static_cast<int>(width * 4U));
        if (encoded == 0 || sink.failed || sink.bytes.empty()) {
            error = "PNG encoding failed";
            return false;
        }
        png_bytes.reserve(sink.bytes.size());
        std::ranges::transform(sink.bytes, std::back_inserter(png_bytes), [](std::uint8_t value) {
            return static_cast<std::byte>(value);
        });
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool write_ugc_preview_png(const std::filesystem::path& maps_root,
                           std::string_view project_title,
                           std::span<const std::byte> png_bytes,
                           std::string& error) noexcept {
    error.clear();
    try {
        if (project_title.empty() || png_bytes.size() < 8U) {
            error = "invalid UGC preview PNG or title";
            return false;
        }
        const auto scan = scan_ugc_projects(maps_root);
        const auto project = std::ranges::find_if(scan.maps, [&](const auto& candidate) {
            return candidate.title == project_title;
        });
        if (project == scan.maps.end()) {
            error = "the active UGC project is not present in the publish catalog";
            return false;
        }
        std::error_code code;
        const auto root = std::filesystem::weakly_canonical(maps_root, code);
        if (code || !std::filesystem::is_directory(root)) {
            error = "cannot resolve the UGC preview catalog";
            return false;
        }
        const auto sidecar = root / std::filesystem::path{project->uid};
        if (!regular_non_symlink(sidecar)) {
            error = "the selected UGC project sidecar is unsafe";
            return false;
        }
        auto output = sidecar;
        output.replace_extension(".png");
        auto temporary = output;
        temporary += ".tmp";
        if ((std::filesystem::is_symlink(output, code) && !code) ||
            (std::filesystem::is_symlink(temporary, code) && !code)) {
            error = "the UGC preview path is a symlink";
            return false;
        }
        code.clear();
        {
            std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
            if (!stream) {
                error = "cannot create the UGC preview temporary file";
                return false;
            }
            stream.write(reinterpret_cast<const char*>(png_bytes.data()),
                         static_cast<std::streamsize>(png_bytes.size()));
            if (!stream) {
                error = "cannot write the complete UGC preview PNG";
                return false;
            }
        }
        std::filesystem::remove(output, code);
        code.clear();
        std::filesystem::rename(temporary, output, code);
        if (code) {
            std::filesystem::remove(temporary, code);
            error = "cannot commit the UGC preview PNG";
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

std::vector<std::uint8_t> ugc_screenshot_preview_rgba(std::span<const std::uint8_t> rgba,
                                                      std::uint32_t width,
                                                      std::uint32_t height,
                                                      std::uint32_t& out_extent) {
    out_extent = 0U;
    if (width == 0U || height == 0U ||
        static_cast<std::uint64_t>(width) * height * 4U != rgba.size()) {
        return {};
    }
    const auto smallest = std::min(width, height);
    const auto origin_x = static_cast<std::uint64_t>((width - smallest) / 2U);
    const auto origin_y = static_cast<std::uint64_t>((height - smallest) / 2U);
    constexpr std::uint32_t retail_extent{512U};
    const auto extent = smallest > retail_extent ? retail_extent : smallest;
    std::vector<std::uint8_t> output(static_cast<std::size_t>(extent) * extent * 4U, 0U);
    for (std::uint32_t y{}; y < extent; ++y) {
        const auto source_y0 = static_cast<std::uint64_t>(y) * smallest / extent;
        const auto source_y1 = std::max<std::uint64_t>(
            source_y0 + 1U, static_cast<std::uint64_t>(y + 1U) * smallest / extent);
        for (std::uint32_t x{}; x < extent; ++x) {
            const auto source_x0 = static_cast<std::uint64_t>(x) * smallest / extent;
            const auto source_x1 = std::max<std::uint64_t>(
                source_x0 + 1U, static_cast<std::uint64_t>(x + 1U) * smallest / extent);
            std::array<std::uint64_t, 3U> sum{};
            std::uint64_t count{};
            for (auto sy = source_y0; sy < source_y1; ++sy) {
                for (auto sx = source_x0; sx < source_x1; ++sx) {
                    const auto index = ((origin_y + sy) * width + origin_x + sx) * 4U;
                    sum[0U] += rgba[static_cast<std::size_t>(index)];
                    sum[1U] += rgba[static_cast<std::size_t>(index + 1U)];
                    sum[2U] += rgba[static_cast<std::size_t>(index + 2U)];
                    ++count;
                }
            }
            const auto target = (static_cast<std::size_t>(y) * extent + x) * 4U;
            for (std::size_t channel{}; channel < 3U; ++channel) {
                output[target + channel] = static_cast<std::uint8_t>(sum[channel] / count);
            }
            // The retail scaler works in RGB; the preview is fully opaque.
            output[target + 3U] = 255U;
        }
    }
    out_extent = extent;
    return output;
}

bool write_screenshot_png(const std::filesystem::path& path,
                          std::span<const std::uint8_t> rgba,
                          std::uint32_t width,
                          std::uint32_t height,
                          std::string& error) noexcept {
    error.clear();
    try {
        constexpr std::uint32_t maximum_extent{16'384U};
        if (width == 0U || height == 0U || width > maximum_extent || height > maximum_extent) {
            error = "invalid screenshot dimensions";
            return false;
        }
        if (static_cast<std::uint64_t>(width) * height * 4U != rgba.size()) {
            error = "screenshot pixels do not match the declared RGBA extent";
            return false;
        }
        std::error_code code;
        std::filesystem::create_directories(path.parent_path(), code);
        if (code) {
            error = "cannot create the screenshot directory " + path.parent_path().string();
            return false;
        }
        PngSink sink;
        const auto encoded = stbi_write_png_to_func(append_png_bytes,
                                                    &sink,
                                                    static_cast<int>(width),
                                                    static_cast<int>(height),
                                                    4,
                                                    rgba.data(),
                                                    static_cast<int>(width * 4U));
        if (encoded == 0 || sink.failed || sink.bytes.empty()) {
            error = "PNG encoding failed for the screenshot";
            return false;
        }
        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        stream.write(reinterpret_cast<const char*>(sink.bytes.data()),
                     static_cast<std::streamsize>(sink.bytes.size()));
        if (!stream) {
            error = "cannot write the screenshot " + path.string();
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace battlespades::frontend
