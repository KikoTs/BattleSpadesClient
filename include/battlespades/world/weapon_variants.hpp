#pragma once

#include <filesystem>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::world {
using SkinVariantSelection = std::map<std::string,std::string>;
struct SkinVariantChoice final {
    std::string id, label;
    std::map<std::string,std::string> config;
    float magnification{};
    bool scope{};
};
struct SkinVariantOption final {
    std::string id, label, default_choice;
    std::vector<SkinVariantChoice> choices;
    [[nodiscard]] const SkinVariantChoice& selected(const SkinVariantSelection&) const;
};
struct SkinVariantDefinition final {
    std::vector<SkinVariantOption> options;
    float magnification{};
    bool scope{};
};
struct ResolvedSkinVariant final {
    std::map<std::string,std::string> config;
    float magnification{};
    bool scope{};
};
[[nodiscard]] SkinVariantDefinition load_skin_variants(const std::filesystem::path& manifest);
[[nodiscard]] ResolvedSkinVariant resolve_skin_variant(const SkinVariantDefinition&,const SkinVariantSelection&);
[[nodiscard]] double skin_variant_zoom_target(float magnification) noexcept;

/** Local choice IDs only. Raw script settings are selected from the pack allowlist. */
class SkinVariantPreferences final {
public:
    explicit SkinVariantPreferences(std::filesystem::path path);
    [[nodiscard]] bool load(std::string& error);
    [[nodiscard]] SkinVariantSelection selection(std::string_view item) const;
    [[nodiscard]] bool set(std::string_view item,const SkinVariantDefinition&,const SkinVariantSelection&,std::string& error);
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
private:
    std::filesystem::path path_;
    std::map<std::string,SkinVariantSelection> choices_;
    std::uint64_t revision_{};
};
} // namespace battlespades::world
