#include "battlespades/assets/background_pack.hpp"

#include <sodium.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <set>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace battlespades::assets {
namespace {
constexpr std::array<std::uint8_t, 8> magic{'B','S','P','B','G','P','K','1'};
constexpr std::size_t maximum_pack = 128U * 1024U * 1024U;
constexpr std::size_t maximum_image = 8U * 1024U * 1024U;
constexpr std::size_t maximum_entries = 128U;
constexpr auto nonce_size = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
constexpr auto tag_size = crypto_aead_xchacha20poly1305_ietf_ABYTES;
// A shipped client must be able to read its artwork. This embedded format key
// obscures storage and detects corruption; it cannot enforce access or rights.
constexpr std::array<std::uint8_t, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> key{
    0x6b,0x39,0xe1,0x47,0xa8,0x05,0xf2,0xcc,0x91,0x7d,0x63,0x20,0xb4,0xea,0x0f,0x58,
    0xd3,0x82,0x1a,0x76,0x4e,0x9b,0xc5,0x33,0x07,0xaf,0x65,0xdd,0x12,0x8c,0xf0,0x49};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

void initialize() {
    static const int result = sodium_init();
    require(result >= 0, "Cannot initialize background encryption");
}

bool safe_name(std::string_view name) {
    if (!name.starts_with("png/ui/menu_backgrounds/") || !name.ends_with(".png") ||
        name.size() > 256U || name.find("..") != std::string_view::npos ||
        name.find("//") != std::string_view::npos) return false;
    return std::ranges::all_of(name, [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '/' || c == '-' || c == '_' || c == '.';
    });
}

void append_integer(std::vector<std::uint8_t>& bytes, std::uint32_t value, unsigned count) {
    for (unsigned i{}; i < count; ++i)
        bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8U)));
}

std::uint32_t take_integer(std::span<const std::uint8_t> bytes, std::size_t& cursor, unsigned count) {
    require(cursor <= bytes.size() && count <= bytes.size() - cursor, "Truncated background index");
    std::uint32_t value{};
    for (unsigned i{}; i < count; ++i) value |= std::uint32_t{bytes[cursor++]} << (i * 8U);
    return value;
}
} // namespace

std::optional<BackgroundPack> BackgroundPack::open(const std::filesystem::path& path, std::string& error) {
    error.clear();
    try {
        initialize();
        std::ifstream input{path, std::ios::binary | std::ios::ate};
        require(input.good(), "Cannot open background pack");
        const auto length = input.tellg();
        require(length >= 12 && length <= static_cast<std::streamoff>(maximum_pack), "Invalid background pack size");
        BackgroundPack pack;
        pack.encrypted_.resize(static_cast<std::size_t>(length));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(pack.encrypted_.data()), static_cast<std::streamsize>(length));
        require(input.good(), "Cannot read background pack");
        const std::span<const std::uint8_t> bytes{pack.encrypted_};
        require(std::equal(magic.begin(), magic.end(), bytes.begin()), "Unsupported background pack version");
        std::size_t cursor = magic.size();
        const auto count = take_integer(bytes, cursor, 4);
        require(count > 0 && count <= maximum_entries, "Invalid background entry count");
        for (std::uint32_t i{}; i < count; ++i) {
            const auto name_size = take_integer(bytes, cursor, 2);
            const auto size = take_integer(bytes, cursor, 4);
            require(name_size > 0 && name_size <= 256 && name_size <= bytes.size() - cursor,
                    "Invalid background asset name length");
            std::string name{reinterpret_cast<const char*>(bytes.data() + cursor), name_size};
            cursor += name_size;
            require(safe_name(name), "Unsafe background asset name");
            require(size > 0 && size <= maximum_image, "Invalid background image size");
            const auto encrypted_size = std::size_t{size} + tag_size;
            require(nonce_size + encrypted_size <= bytes.size() - cursor, "Truncated background image");
            require(pack.entries_.emplace(std::move(name), Entry{cursor, cursor + nonce_size, size}).second,
                    "Duplicate background asset");
            cursor += nonce_size + encrypted_size;
        }
        require(cursor == bytes.size(), "Unexpected bytes after background pack");
        // Authenticate everything before advertising any optional scene as
        // available. A corrupt pack then falls back as one unit.
        for (const auto& [name, entry] : pack.entries_) {
            static_cast<void>(entry);
            require(pack.read(name, error).has_value(), "Background pack authentication failed");
        }
        return pack;
    } catch (const std::exception& failure) {
        error = failure.what();
        return std::nullopt;
    }
}

bool BackgroundPack::contains(std::string_view name) const noexcept { return entries_.contains(name); }

std::vector<std::string> BackgroundPack::names() const {
    std::vector<std::string> result;
    result.reserve(entries_.size());
    for (const auto& [name, entry] : entries_) {
        static_cast<void>(entry);
        result.push_back(name);
    }
    return result;
}

std::optional<std::vector<std::uint8_t>> BackgroundPack::read(std::string_view name, std::string& error) const {
    error.clear();
    const auto found = entries_.find(name);
    if (found == entries_.end()) { error = "Background asset is not in this pack"; return std::nullopt; }
    const auto& entry = found->second;
    std::vector<std::uint8_t> plain(entry.size);
    unsigned long long size{};
    const int result = crypto_aead_xchacha20poly1305_ietf_decrypt(plain.data(), &size, nullptr,
        encrypted_.data() + entry.ciphertext, entry.size + tag_size,
        reinterpret_cast<const unsigned char*>(name.data()), name.size(),
        encrypted_.data() + entry.nonce, key.data());
    if (result != 0 || size != entry.size) {
        sodium_memzero(plain.data(), plain.size());
        error = "Background image authentication failed";
        return std::nullopt;
    }
    return plain;
}

bool write_background_pack(const std::filesystem::path& path,
                          std::span<const BackgroundAsset> images, std::string& error) {
    error.clear();
    std::filesystem::path temporary;
    try {
        initialize();
        require(!images.empty() && images.size() <= maximum_entries, "Invalid background entry count");
        std::vector<std::uint8_t> bytes{magic.begin(), magic.end()};
        append_integer(bytes, static_cast<std::uint32_t>(images.size()), 4);
        std::set<std::string, std::less<>> names;
        for (const auto& image : images) {
            require(safe_name(image.name), "Unsafe background asset name");
            require(names.insert(image.name).second, "Duplicate background asset");
            require(!image.bytes.empty() && image.bytes.size() <= maximum_image, "Invalid background image size");
            const auto growth = 6U + image.name.size() + nonce_size + image.bytes.size() + tag_size;
            require(growth <= maximum_pack - bytes.size(), "Background pack is too large");
            append_integer(bytes, static_cast<std::uint32_t>(image.name.size()), 2);
            append_integer(bytes, static_cast<std::uint32_t>(image.bytes.size()), 4);
            bytes.insert(bytes.end(), image.name.begin(), image.name.end());
            std::array<std::uint8_t, nonce_size> nonce{};
            randombytes_buf(nonce.data(), nonce.size());
            bytes.insert(bytes.end(), nonce.begin(), nonce.end());
            const auto offset = bytes.size();
            bytes.resize(offset + image.bytes.size() + tag_size);
            unsigned long long written{};
            require(crypto_aead_xchacha20poly1305_ietf_encrypt(bytes.data() + offset, &written,
                image.bytes.data(), image.bytes.size(),
                reinterpret_cast<const unsigned char*>(image.name.data()), image.name.size(),
                nullptr, nonce.data(), key.data()) == 0 && written == image.bytes.size() + tag_size,
                "Background encryption failed");
        }
        static std::atomic<std::uint64_t> sequence{};
        temporary = path;
        const auto process_id =
#if defined(_WIN32)
            GetCurrentProcessId();
#else
            getpid();
#endif
        temporary += ".tmp." + std::to_string(process_id) + '.' +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + '.' +
            std::to_string(sequence.fetch_add(1));
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        {
            std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            output.close();
            require(output.good(), "Cannot write background pack");
        }
#if defined(_WIN32)
        require(MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
                "Cannot replace background pack");
#else
        std::filesystem::rename(temporary, path);
#endif
        return true;
    } catch (const std::exception& failure) {
        if (!temporary.empty()) { std::error_code ignored; std::filesystem::remove(temporary, ignored); }
        error = failure.what();
        return false;
    }
}

std::shared_ptr<const BackgroundPack> cached_background_pack(const std::filesystem::path& path, std::string& error) {
    static std::mutex mutex;
    static std::map<std::filesystem::path, std::weak_ptr<const BackgroundPack>> cache;
    std::scoped_lock lock{mutex};
    const auto normalized = std::filesystem::absolute(path).lexically_normal();
    if (const auto found = cache.find(normalized); found != cache.end())
        if (auto existing = found->second.lock()) { error.clear(); return existing; }
    auto loaded = BackgroundPack::open(normalized, error);
    if (!loaded) return {};
    auto result = std::make_shared<const BackgroundPack>(std::move(*loaded));
    std::erase_if(cache, [](const auto& entry) { return entry.second.expired(); });
    cache[normalized] = result;
    return result;
}
} // namespace battlespades::assets
