#include "battlespades/frontend/ugc_preview_writer.hpp"

#include "battlespades/frontend/ugc_project_repository.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
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

} // namespace battlespades::frontend
