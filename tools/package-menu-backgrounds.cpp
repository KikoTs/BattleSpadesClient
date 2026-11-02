#include "battlespades/assets/background_pack.hpp"
#include <nlohmann/json.hpp>
#include <sodium.h>
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace battlespades::assets;

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    try {
        if (argc != 5) throw std::runtime_error{
            "usage: aos_pack_menu_backgrounds <original-root> <catalog.json> <scenes.json> <output.bspbg>"};
        if (sodium_init() < 0) throw std::runtime_error{"Cannot initialize encryption"};
        const fs::path root = fs::absolute(argv[1]);
        std::map<std::string, std::string> expected;
        const auto add = [&expected](const nlohmann::json& entry) {
            const auto name = entry.at("asset").get<std::string>();
            const auto hash = entry.at("sha256").get<std::string>();
            if (!expected.emplace(name, hash).second) throw std::runtime_error{"Duplicate catalog asset"};
        };
        std::ifstream catalog_file{fs::path{argv[2]}}, scenes_file{fs::path{argv[3]}};
        const auto catalog = nlohmann::json::parse(catalog_file);
        const auto scenes = nlohmann::json::parse(scenes_file);
        for (const auto& entry : catalog) add(entry);
        for (const auto& scene : scenes.at("scenes"))
            for (const auto& layer : scene.at("layers")) add(layer);
        std::vector<BackgroundAsset> images;
        for (const auto& [name, expected_hash] : expected) {
            const fs::path relative{name};
            if (!name.starts_with("png/ui/menu_backgrounds/") || relative.is_absolute() ||
                name.find("..") != std::string::npos || name.find('\\') != std::string::npos)
                throw std::runtime_error{"Unsafe catalog asset path"};
            std::ifstream input{root / relative, std::ios::binary | std::ios::ate};
            const auto length = input.tellg();
            if (!input || length <= 0 || length > 8 * 1024 * 1024)
                throw std::runtime_error{"Missing or oversized background: " + name};
            BackgroundAsset image{name, std::vector<std::uint8_t>(static_cast<std::size_t>(length))};
            input.seekg(0);
            input.read(reinterpret_cast<char*>(image.bytes.data()), static_cast<std::streamsize>(length));
            if (!input) throw std::runtime_error{"Cannot read background: " + name};
            std::array<unsigned char, crypto_hash_sha256_BYTES> hash{};
            crypto_hash_sha256(hash.data(), image.bytes.data(), image.bytes.size());
            std::array<char, crypto_hash_sha256_BYTES * 2 + 1> hex{};
            sodium_bin2hex(hex.data(), hex.size(), hash.data(), hash.size());
            if (std::string_view{hex.data()} != expected_hash)
                throw std::runtime_error{"Background checksum mismatch: " + name};
            images.push_back(std::move(image));
        }
        std::string error;
        if (!write_background_pack(fs::path{argv[4]}, images, error)) throw std::runtime_error{error};
        const auto verified = BackgroundPack::open(fs::path{argv[4]}, error);
        if (!verified) throw std::runtime_error{error};
        for (const auto& image : images) {
            const auto decoded = verified->read(image.name, error);
            if (!decoded || *decoded != image.bytes) throw std::runtime_error{"Background round trip failed"};
        }
        std::cout << "Encrypted and verified " << images.size() << " backgrounds/layers; "
                  << fs::file_size(fs::path{argv[4]}) << " bytes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
