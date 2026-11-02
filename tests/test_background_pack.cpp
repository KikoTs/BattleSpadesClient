#include "battlespades/assets/background_pack.hpp"
#include "battlespades/render/image_memory.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace battlespades::assets;
namespace fs = std::filesystem;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error{message};
}

std::vector<std::uint8_t> file_bytes(const fs::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, {}};
}

void put(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output{path, std::ios::binary};
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    check(output.good(), "cannot write test fixture");
}

int main(int argc, char** argv) {
    const auto root = fs::temp_directory_path() / ("battlespades-background-pack-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); } } cleanup{root};
    try {
        fs::create_directories(root);
        const auto path = root / "test.bspbg";
        const std::string payload = "a recognizable background payload that must not be stored as plaintext";
        const BackgroundAsset first{"png/ui/menu_backgrounds/example.png", {payload.begin(), payload.end()}};
        const BackgroundAsset second{"png/ui/menu_backgrounds/layers/example/cutout.png", {1,2,3,4,5,6}};
        const std::vector images{first, second};
        std::string error;
        check(write_background_pack(path, images, error), error);
        const auto original = file_bytes(path);
        check(std::search(original.begin(), original.end(), first.bytes.begin(), first.bytes.end()) == original.end(),
              "pack leaked the plaintext payload");
        const auto pack = BackgroundPack::open(path, error);
        check(pack.has_value(), error);
        check(pack->names().size() == images.size(), "pack lost entries");
        for (const auto& image : images) {
            check(pack->contains(image.name), "pack lost asset name");
            check(pack->read(image.name, error) == image.bytes, "pack round trip changed bytes");
        }
        check(!pack->read("png/ui/menu_backgrounds/missing.png", error), "missing entry was accepted");
        check(write_background_pack(root / "second.bspbg", images, error), error);
        check(original != file_bytes(root / "second.bspbg"), "encryption reused deterministic nonces");

        for (const std::string bad : {"../example.png", "/example.png", "png/ui/menu_backgrounds/../bad.png",
                "png/ui/menu_backgrounds/Example.png", "png/ui/menu_backgrounds/a\\b.png",
                "png/ui/menu_backgrounds/a.txt", "png/ui/menu_backgrounds/a//b.png"}) {
            auto invalid = first;
            invalid.name = bad;
            check(!write_background_pack(path, std::vector{invalid}, error), "unsafe path was accepted");
            check(file_bytes(path) == original, "invalid write replaced a valid pack");
        }
        check(!write_background_pack(path, std::vector{first, first}, error), "duplicate entry was accepted");
        check(!write_background_pack(path, std::vector<BackgroundAsset>{}, error), "empty pack was accepted");
        auto empty = first;
        empty.bytes.clear();
        check(!write_background_pack(path, std::vector{empty}, error), "empty image was accepted");
        auto oversized = first;
        oversized.bytes.resize(8U * 1024U * 1024U + 1U);
        check(!write_background_pack(path, std::vector{oversized}, error), "oversized image was accepted");

        const auto broken = root / "broken.bspbg";
        for (std::size_t length{}; length < original.size(); ++length) {
            put(broken, {original.begin(), original.begin() + static_cast<std::ptrdiff_t>(length)});
            check(!BackgroundPack::open(broken, error), "truncated pack was accepted");
        }
        // Name, nonce, ciphertext, tag and framing damage all fail closed.
        for (const auto position : {std::size_t{0}, std::size_t{8}, std::size_t{12}, std::size_t{14},
                std::size_t{18 + first.name.size() + 3}, original.size() - 1}) {
            auto corrupted = original;
            corrupted[position] ^= 1;
            put(broken, corrupted);
            check(!BackgroundPack::open(broken, error), "tampered pack was accepted");
        }
        auto trailing = original;
        trailing.push_back(0);
        put(broken, trailing);
        check(!BackgroundPack::open(broken, error), "trailing pack bytes were accepted");

        const auto shared = cached_background_pack(path, error);
        check(shared && shared == cached_background_pack(path, error), "preload cache did not share immutable bytes");
        std::vector<std::future<bool>> reads;
        for (int worker{}; worker < 8; ++worker) {
            reads.push_back(std::async(std::launch::async, [shared, first] {
                std::string worker_error;
                for (int i{}; i < 25; ++i)
                    if (shared->read(first.name, worker_error) != first.bytes) return false;
                return true;
            }));
        }
        for (auto& read : reads) check(read.get(), "concurrent background decode changed bytes");
        if (argc == 2) {
            const auto actual = BackgroundPack::open(fs::path{argv[1]}, error);
            check(actual.has_value(), error);
            for (const auto& name : actual->names()) {
                const auto bytes = actual->read(name, error);
                check(bytes.has_value(), error);
                const auto image = battlespades::render::decode_png_rgba8(std::span<const std::uint8_t>{*bytes});
                check(static_cast<bool>(image), image.error);
            }
            std::cout << "Decoded all " << actual->names().size() << " packed PNGs in memory\n";
        }
        check(!battlespades::render::decode_png_rgba8(std::span<const std::uint8_t>{first.bytes}),
              "memory decoder accepted a non-PNG payload");
        std::cout << "Encrypted background round trips, corruption bounds, atomic writes and concurrent reads passed\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
