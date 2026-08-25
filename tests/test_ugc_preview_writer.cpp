#include "battlespades/frontend/ugc_preview_writer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

[[nodiscard]] std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    const std::vector<char> chars{std::istreambuf_iterator<char>{stream}, {}};
    std::vector<std::byte> bytes;
    bytes.reserve(chars.size());
    for (const char value : chars) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
    return bytes;
}

} // namespace

int main() {
    const auto suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
                      ("aos-ugc-preview-test-" + suffix) / "maps";
    std::filesystem::create_directories(root);
    const auto sidecar = root / "NeonArena.ugc";
    {
        std::ofstream stream{sidecar};
        stream << R"({"title":"Neon Arena","tags":["tdm"]})";
    }
    {
        std::ofstream stream{root / "NeonArena.vxl", std::ios::binary};
        stream.put('\0');
    }
    {
        std::ofstream stream{root / "NeonArena.txt"};
        stream << "Classic_B.txt\n";
    }

    constexpr std::uint32_t width{8U};
    constexpr std::uint32_t height{8U};
    std::vector<std::uint8_t> rgba(width * height * 4U, 255U);
    for (std::size_t index{}; index < rgba.size(); index += 4U) {
        rgba[index + 0U] = static_cast<std::uint8_t>(index & 0xffU);
        rgba[index + 1U] = 85U;
        rgba[index + 2U] = 156U;
    }
    std::vector<std::byte> png;
    std::string error;
    expect(battlespades::frontend::write_ugc_overhead_preview(
               root, "Neon Arena", rgba, width, height, png, error),
           error.c_str());
    constexpr std::array signature{
        std::byte{0x89}, std::byte{0x50}, std::byte{0x4e}, std::byte{0x47},
        std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a},
    };
    expect(png.size() > signature.size() &&
               std::equal(signature.begin(), signature.end(), png.begin()),
           "preview encoder must produce a PNG signature");
    expect(read_bytes(root / "NeonArena.png") == png,
           "persisted preview and returned loading-screen payload must match exactly");
    expect(!battlespades::frontend::write_ugc_overhead_preview(
               root, "../Neon Arena", rgba, width, height, png, error) && !error.empty(),
           "an authored title must never act as a path selector");

    std::filesystem::remove_all(root.parent_path());
    std::cout << "UGC overhead preview: PNG persisted and traversal-safe\n";
    return 0;
}
