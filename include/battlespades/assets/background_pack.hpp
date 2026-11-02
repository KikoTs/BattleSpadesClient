#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::assets {

inline constexpr std::string_view background_pack_path{"backgrounds/menu-backgrounds.bspbg"};

struct BackgroundAsset final {
    std::string name;
    std::vector<std::uint8_t> bytes;
};

/** Encrypted, authenticated image storage. The client key is not a DRM boundary. */
class BackgroundPack final {
public:
    [[nodiscard]] static std::optional<BackgroundPack> open(
        const std::filesystem::path& path, std::string& error);
    [[nodiscard]] bool contains(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<std::string> names() const;
    /** Immutable pack data supports concurrent preload workers. */
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> read(
        std::string_view name, std::string& error) const;

private:
    struct Entry { std::size_t nonce{}, ciphertext{}, size{}; };
    std::vector<std::uint8_t> encrypted_;
    std::map<std::string, Entry, std::less<>> entries_;
};

[[nodiscard]] bool write_background_pack(const std::filesystem::path& path,
    std::span<const BackgroundAsset> images, std::string& error);
/** Shares encrypted bytes between the menu and asynchronous preload workers. */
[[nodiscard]] std::shared_ptr<const BackgroundPack> cached_background_pack(
    const std::filesystem::path& path, std::string& error);

} // namespace battlespades::assets
