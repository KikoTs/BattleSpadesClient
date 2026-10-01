#include "battlespades/frontend/inventory_session.hpp"
#include "battlespades/network/cosmetic_slots.hpp"
#include "battlespades/world/scripted_weapon.hpp"
#include "battlespades/world/cosmetic_files.hpp"
#include "battlespades/world/class_models.hpp"
#include "battlespades/world/entity_catalog.hpp"
#include "battlespades/world/cosmetic_preview.hpp"
#include "battlespades/world/retail_character_pose.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <set>
#include <mutex>
#include <sodium.h>
#include <stdexcept>
#include <utility>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace battlespades::frontend {
namespace {
using Json = nlohmann::json;
// The embedded catalogues are far longer than the 65,536 characters the standard
// obliges a compiler to accept in one literal; every supported compiler takes them.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverlength-strings"
#endif
#include "inventory_catalog.inc"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
std::string bounded(const Json& value, const char* key, std::size_t max = 128U) {
    const auto& field = value.at(key);
    if (!field.is_string())
        throw std::runtime_error{"Invalid collection text."};
    const auto text = field.get<std::string>();
    if (text.size() > max || std::ranges::any_of(text, [](unsigned char c) { return c < 32U; }))
        throw std::runtime_error{"Invalid collection text."};
    return text;
}
std::string decimal(const Json& value, const char* key) {
    const auto text = bounded(value, key, 19U);
    std::uint64_t number{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        number > 9223372036854775807ULL || (text.size() > 1U && text[0] == '0'))
        throw std::runtime_error{"Invalid collection counter."};
    return text;
}
std::vector<InventoryCosmetic> builtins() {
    static const auto cached = [] {
    const auto catalog = Json::parse(inventory_catalog_json);
    const auto pools = Json::parse(inventory_pools_json);
    std::vector<InventoryCosmetic> result;
    for (const auto& item : catalog.at("items")) {
        result.push_back({item.at("id"),
                          item.at("name"),
                          item.at("kind"),
                          item.at("rarity"),
                          item.at("asset"),
                          item.at("sha256"),
                          item.at("palette").get<std::array<std::uint8_t, 3U>>(),
                          item.at("slots").get<std::vector<std::string>>(),
                          false,
                          false});
        auto& cosmetic = result.back();
        cosmetic.author = item.value("author", "");
        cosmetic.source = item.value("source", "");
        cosmetic.parent_asset = item.value("parent_asset", "");
        cosmetic.description = item.value("description", "");
        cosmetic.scripted_skin = item.value("scripted_skin", "");
        cosmetic.character_format = item.value("character_format", "");
        cosmetic.enabled = item.value("enabled", true);
        cosmetic.replacement_id = item.value("replacement_id", "");
        if(item.contains("character_parts"))for(const auto& [role,part]:item["character_parts"].items())
            cosmetic.character_parts.emplace(role,InventoryCosmetic::ModelPart{part.at("asset"),part.at("sha256")});
        for (const auto& pool : pools) for (const auto& entry : pool.at("items"))
            if (entry.at("id")==item.at("id") && entry.value("enabled",false)) {
                cosmetic.crate_versions.push_back(pool.at("version"));
                cosmetic.crate_rarities.emplace(pool.at("version"),entry.at("rarity"));
            }
        if (item.contains("parents")) for (const auto& parent : item.at("parents"))
            cosmetic.parents.push_back({parent.at("tool").get<std::uint8_t>(),parent.at("asset"),
                                       parent.at("scale"),parent.at("pivot").get<std::array<float,3U>>()});
        if(cosmetic.id=="community-bren-v2"){
            cosmetic.slots={"weapon:61:view","weapon:61:world"};
            cosmetic.parents={{61U,"kv6/lightMachineGun.kv6",0.35F,{13.5F,60.0F,23.5F}}};
            cosmetic.description="Bren Gun appearance for the Medic's light machine gun.";
        }
    }
    return result;
    }();
    return cached;
}
std::string hex(std::span<const unsigned char> bytes) {
    std::string out(bytes.size() * 2U + 1U, '\0');
    sodium_bin2hex(out.data(), out.size(), bytes.data(), bytes.size());
    out.pop_back();
    return out;
}
std::vector<unsigned char> unhex(std::string_view text) {
    if (text.size() != 64U)
        throw std::runtime_error{"Invalid receipt seed."};
    std::vector<unsigned char> bytes(32U);
    if (sodium_hex2bin(
            bytes.data(), bytes.size(), text.data(), text.size(), nullptr, nullptr, nullptr) != 0)
        throw std::runtime_error{"Invalid receipt seed."};
    return bytes;
}
std::array<unsigned char, 32U> sha(std::span<const unsigned char> input) {
    if (sodium_init() < 0)
        throw std::runtime_error{"Cosmetic verification is unavailable."};
    std::array<unsigned char, 32U> digest{};
    crypto_hash_sha256(digest.data(), input.data(), input.size());
    return digest;
}
std::array<unsigned char, 32U> sha(std::string_view input) {
    return sha(std::span{reinterpret_cast<const unsigned char*>(input.data()), input.size()});
}

std::shared_ptr<const world::Kv6Model> verified_asset(const std::filesystem::path& path,
                                                    std::string_view digest) {
    struct CachedModel {
        std::filesystem::file_time_type modified;
        std::uintmax_t size{};
        std::string digest;
        std::shared_ptr<const world::Kv6Model> model;
        std::uint64_t used{};
    };
    static std::mutex mutex;
    static std::map<std::filesystem::path, CachedModel> cache;
    static std::uint64_t clock{};
    std::error_code error;
    const auto absolute =
        std::filesystem::absolute(world::resolve_cosmetic_file(path), error).lexically_normal();
    if (error) return {};
    const auto size = std::filesystem::file_size(absolute, error);
    if (error || size > 2U * 1024U * 1024U) return {};
    const auto modified = std::filesystem::last_write_time(absolute, error);
    if (error) return {};
    {
        std::lock_guard lock{mutex};
        const auto found = cache.find(absolute);
        if (found != cache.end() && found->second.modified == modified &&
            found->second.size == size && found->second.digest == digest) {
            found->second.used = ++clock;
            return found->second.model;
        }
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    std::ifstream input{absolute, std::ios::binary};
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!input || hex(sha(bytes)) != digest) return {};
    auto decoded = world::Kv6Model::load(std::span{reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()});
    if (!decoded) return {};
    auto model = std::make_shared<const world::Kv6Model>(std::move(*decoded));
    {
        std::lock_guard lock{mutex};
        if (cache.size() >= 64U && !cache.contains(absolute)) {
            const auto oldest = std::ranges::min_element(cache, {}, [](const auto& entry) { return entry.second.used; });
            cache.erase(oldest);
        }
        cache.insert_or_assign(absolute, CachedModel{modified, size, std::string{digest}, model, ++clock});
    }
    return model;
}
InventoryOpening opening(const Json& receipt) {
    const auto id=bounded(receipt.at("item"),"id");
    const auto* original=find_inventory_cosmetic(id);
    const auto* replacement=original&&!original->replacement_id.empty()
        ?find_inventory_cosmetic(original->replacement_id):nullptr;
    return {bounded(receipt, "crate_id"),
            replacement?replacement->name:bounded(receipt.at("item"), "name"),
            replacement?replacement->rarity:bounded(receipt.at("item"), "rarity"),
            bounded(receipt, "opened_at"),
            bounded(receipt, "commitment", 64U),
            bounded(receipt, "seed", 64U),
            false, replacement?replacement->id:id};
}
std::filesystem::path cache_path(std::string_view account) {
    // Hash the account identity into a fixed filename, never a URL or supplied path.
    return network::default_revival_state_path().parent_path() / "collection-cache" /
           (hex(sha(account)) + ".json");
}
void save_cache(const std::filesystem::path& path, const Json& snapshot) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return;
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream output{temp, std::ios::binary | std::ios::trunc};
        output << snapshot.dump();
        if (!output)
            return;
    }
    // Cache replacement is atomic; a failure preserves the previous snapshot.
#if defined(_WIN32)
    if (!MoveFileExW(
            temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        std::filesystem::remove(temp, error);
#else
    std::filesystem::rename(temp, path, error);
    if (error)
        std::filesystem::remove(temp, error);
#endif
}
} // namespace

const InventoryCosmetic* find_inventory_cosmetic(std::string_view id) {
    static const auto items = builtins();
    const auto found = std::ranges::find(items,id,&InventoryCosmetic::id);
    return found == items.end() ? nullptr : &*found;
}

std::shared_ptr<const world::Kv6Model> inventory_verified_model(
    const InventoryCosmetic& item, const std::filesystem::path& root) {
    const auto* trusted = find_inventory_cosmetic(item.id);
    if (!trusted || !trusted->enabled || trusted->kind=="profile_badge") return {};
    const auto path = trusted->asset.starts_with("client/cosmetics/")
        ? root.parent_path()/trusted->asset : root/trusted->asset;
    return verified_asset(path, trusted->sha256);
}

std::optional<world::WeaponCosmeticFinish> inventory_weapon_finish(
    const InventoryCosmetic* item, const std::filesystem::path& root, std::uint8_t tool) {
    if (!item || !item->enabled) return std::nullopt;
    if (item->kind=="weapon_skin") return world::WeaponCosmeticFinish{item->asset,item->palette};
    const auto parent = std::ranges::find(item->parents,tool,&InventoryCosmetic::WeaponParent::tool);
    if (parent == item->parents.end()) return std::nullopt;
    auto model = inventory_verified_model(*item,root);
    if (!model) return std::nullopt; // Missing/unknown content always uses the retail parent.
    auto fit=world::WeaponCosmeticFinish{parent->asset,{},std::move(model),parent->scale,parent->pivot};
    const auto parts=inventory_character_parts(item,root);
    if(const auto sight=parts.find("sight");sight!=parts.end())fit.sight_replacement=std::make_shared<const world::Kv6Model>(sight->second);
    // Presentation fits are independent of the signed reward catalog. Align
    // the trigger grip, rather than the centre of an arbitrary KV6 volume.
    // STG44 source grip (5, 40, 25) maps to the assault rifle's (-.5,-3.5,-11)
    // render-space hand anchor, with a 41.6-voxel overall body length.
    if (item->id=="community-stg44-v2" && tool==60U) {
        fit.scale=0.32F; fit.pivot={6.5625F,74.375F,14.0625F};
        fit.sight_pivot=std::array{6.5625F,74.375F,30.25F};
    }
    // These two packs share the same parent but have different grip locations.
    if (item->id=="community-honey-badger-v2" && tool==60U) {
        fit.scale=0.36F; fit.pivot={5.388889F,70.555556F,15.277778F};
        fit.sight_pivot=std::array{5.388889F,70.555556F,27.333333F};
    }
    if (item->id=="community-aeon-hk416-v2" && tool==60U) {
        fit.scale=0.98F; fit.pivot={2.010204F,27.224490F,5.428571F};
        fit.sight_pivot=std::array{2.010204F,27.224490F,11.571429F};
    }
    if (const auto presentation = world::weapon_presentation(root,item->id);
        presentation && presentation->model_sha256 == item->sha256) {
        fit.sight_tags = presentation->sight;
        // An explicit null chooses the retail optic, including skins that had
        // a legacy one-point ADS fit before presentation files existed.
        if (!presentation->sight) fit.sight_pivot.reset();
    }
    return fit;
}

world::ClassModelOverrides inventory_character_parts(const InventoryCosmetic* item,const std::filesystem::path& root) {
    world::ClassModelOverrides parts;
    const auto* trusted=item?find_inventory_cosmetic(item->id):nullptr;
    if(!trusted||!trusted->enabled)return parts;
    parts.open_spades=trusted->character_format=="OpenSpades";
    for(const auto& [role,part]:trusted->character_parts){
        if(!part.asset.starts_with("client/cosmetics/")||part.asset.find("..")!=std::string::npos)return {};
        const auto model=verified_asset(root.parent_path()/part.asset,part.sha256);
        if(!model)return {};
        parts.emplace(role,*model);
    }
    if(parts.open_spades&&parts.contains("arms")&&!parts.contains("arm_upper")&&!parts.contains("arm_lower")){
        auto segments=parts.at("arms").articulated_classic_arms();
        if(segments.size()==2U){
            parts.emplace("arm_upper",std::move(segments[0]));
            parts.emplace("arm_lower",std::move(segments[1]));
        }
    }
    return parts;
}

InventoryData parse_inventory_snapshot(const Json& value) {
    const auto catalog_digest=bounded(value.at("catalog"),"digest",64U);
    const auto catalog_version=bounded(value.at("catalog"),"version");
    bool supported=catalog_digest==inventory_catalog_digest &&
        catalog_version==Json::parse(inventory_catalog_json).at("version").get<std::string>();
    // Account snapshots use the combined collection digest, not a crate pool's
    // digest. Keep published collections readable while client/server updates
    // roll out independently. Assets still come exclusively from builtins().
    for(const auto& catalog : Json::parse(inventory_legacy_catalogs_json))
        supported |= catalog.at("digest")==catalog_digest && catalog.at("version")==catalog_version;
    for(const auto& pool : Json::parse(inventory_pools_json))
        supported |= pool.at("digest")==catalog_digest && pool.at("version")==catalog_version;
    if (value.at("schema_version") != 1 || !supported)
        throw std::runtime_error{"This collection needs a newer client content pack."};
    const auto& p = value.at("progression");
    InventoryData data;
    data.level = decimal(p, "level");
    data.xp = decimal(p, "lifetime_xp");
    data.level_xp = decimal(p, "level_xp");
    data.next_xp = decimal(p, "xp_to_next");
    data.revision = decimal(p, "inventory_revision");
    data.crate_count = decimal(p, "unopened_crates");
    data.pity = p.at("pity").get<std::array<std::uint32_t, 3U>>();
    if (p.contains("pity_by_family")) for (const auto* family : {"weapons","characters","cosmetics"}) {
        const auto& family_values = p.at("pity_by_family");
        if (!family_values.contains(family)) continue;
        auto counters = family_values.at(family).get<std::array<std::uint32_t,3U>>();
        if (std::ranges::any_of(counters,[](auto n){return n>100U;}))
            throw std::runtime_error{"Invalid crate family counters."};
        data.pity_by_family[family]=counters;
    }
    if (std::ranges::any_of(data.pity, [](auto n) { return n > 100U; }))
        throw std::runtime_error{"Invalid rarity counters."};
    data.guest = value.value("account_type", "") == "guest";
    data.opening_enabled = p.at("features").value("opening", false);
    data.equip_enabled = p.at("features").value("equip", false);
    data.awards_enabled = p.at("features").value("awards", false);
    data.items = builtins();
    const auto &inventory = value.at("inventory").at("items"), &equipped = value.at("equipped");
    if (!inventory.is_array() || inventory.size() > 2048U || !equipped.is_array() ||
        equipped.size() > 256U)
        throw std::runtime_error{"Collection exceeds this client's limit."};
    for (const auto& owned : inventory) {
        const auto id = bounded(owned, "cosmetic_id");
        for (auto& item : data.items)
            if (item.id == id)
                item.owned = true;
    }
    for (const auto& slot : equipped) {
        const auto id = bounded(slot, "cosmetic_id"), stored = bounded(slot, "slot");
        const std::string target{network::cosmetic_display_slot(id,stored)};
        if(target!=stored&&std::ranges::any_of(equipped,[&](const auto& other){return other.at("slot")==target;}))
            continue; // Preserve an explicitly equipped item in the destination.
        for (auto& item : data.items)
            if (item.id == id && item.owned &&
                std::ranges::find(item.slots, target) != item.slots.end()) {
                item.equipped = true;
                data.equipped.emplace_back(target, id);
            }
    }
    const auto &crates = value.at("crates").at("items"), &history = value.at("history").at("items");
    if (!crates.is_array() || crates.size() > 50U || !history.is_array() || history.size() > 50U)
        throw std::runtime_error{"Collection page is too large."};
    for (const auto& crate : crates)
        data.crates.push_back(
            {bounded(crate, "id"), decimal(crate, "level"), bounded(crate, "commitment", 64U),
             crate.value("catalog_version", "supply-v1")});
    for (const auto& entry : history)
        data.history.push_back(opening(entry.at("receipt")));
    if (value.at("crates").at("next_cursor").is_string())
        data.next_crates = bounded(value.at("crates"), "next_cursor", 256U);
    if (value.at("history").at("next_cursor").is_string())
        data.next_history = bounded(value.at("history"), "next_cursor", 256U);
    return data;
}

InventoryData inventory_preview_fixture() {
    InventoryData data;
    data.items = builtins();
    data.level = "12";
    data.xp = "26750";
    data.level_xp = "2000";
    data.next_xp = "3750";
    data.crate_count = "3";
    std::size_t count{};
    for (auto& item : data.items) {
        if (!item.enabled || count==8U) continue;
        item.owned = true;
        const auto i=count++;
        data.history.push_back({"preview-history-"+std::to_string(i),item.name,item.rarity,
                                "2026-09-06T12:00:00Z","","",false,item.id});
    }
    for(auto& item:data.items)if(item.id=="community-stg44-v2") {
        item.owned=item.equipped=true;
        data.equipped.emplace_back("weapon:60:view",item.id);
    }
    data.crates = {{"preview-1", "10", "", "weapons-v5"},
                   {"preview-2", "11", "", "characters-v5"},
                   {"preview-3", "12", "", "cosmetics-v5"}};
    return data;
}

std::optional<world::ChunkMesh> inventory_preview_mesh(
    const InventoryCosmetic& item, const std::filesystem::path& root, bool blue_team,
    std::optional<std::uint8_t> class_id, const InventoryCosmetic* hat, bool head_only) {
    auto source=inventory_verified_model(item,root);
    if (!source) return std::nullopt;
    const auto team=blue_team?world::VxlColor{72,111,181,255}:world::VxlColor{79,132,61,255};
    if(item.kind=="prop_model"&&!item.slots.empty()){
        const auto number=std::string_view{item.slots[0]}.substr(7);unsigned type{};
        if(std::from_chars(number.data(),number.data()+number.size(),type).ec!=std::errc{}||type>255)return std::nullopt;
        const auto* definition=world::find_entity_definition(static_cast<std::uint8_t>(type));if(!definition)return std::nullopt;
        const auto parts=inventory_character_parts(&item,root);world::ChunkMesh combined;
        combined.minimum.fill(std::numeric_limits<float>::max());combined.maximum.fill(std::numeric_limits<float>::lowest());
        for(const auto& part:definition->parts){const auto found=parts.find(std::string{part.kv6});if(found==parts.end())continue;auto model=found->second;model.apply_default_color(team);const auto mesh=model.mesh();const auto first=static_cast<std::uint32_t>(combined.vertices.size());
            for(auto v:mesh.vertices){v.x=v.x*part.scale+part.offset[0];v.y=v.y*part.scale-part.offset[2];v.z=v.z*part.scale+part.offset[1];combined.vertices.push_back(v);const std::array xyz{v.x,v.y,v.z};for(std::size_t i=0;i<3;++i){combined.minimum[i]=std::min(combined.minimum[i],xyz[i]);combined.maximum[i]=std::max(combined.maximum[i],xyz[i]);}}
            for(const auto index:mesh.indices)combined.indices.push_back(first+index);
        }return combined.empty()?std::nullopt:std::optional{std::move(combined)};
    }
    if (item.kind=="character_skin" || item.kind=="hat") {
        for (const auto& slot:item.slots) if (slot.starts_with("class:")) {
            unsigned id{};
            const auto number=std::string_view{slot}.substr(6);
            if (std::from_chars(number.data(),number.data()+number.size(),id).ec!=std::errc{} || id>255U) return std::nullopt;
            if(class_id)id=*class_id;
            const auto parts=inventory_character_parts(&item,root);
            const auto hat_model=hat?inventory_verified_model(*hat,root):nullptr;
            auto loaded=world::load_class_models(root,static_cast<std::uint8_t>(id),team,1U,
                item.kind=="hat"?std::nullopt:std::optional{item.palette},item.kind=="hat"?source.get():hat_model.get(),&parts);
            if (!loaded) return std::nullopt;
            auto mesh=head_only?std::move(loaded.models->head_preview):std::move(loaded.models->standing_preview);
            // The assembled mannequin uses game XYZ/z-down; the orbit renderer uses X/-Z/Y.
            for (auto& v:mesh.vertices) { const auto y=v.y; v.y=-v.z; v.z=y; }
            const auto low=mesh.minimum,high=mesh.maximum;
            mesh.minimum={low[0],-high[2],low[1]}; mesh.maximum={high[0],-low[2],high[1]};
            if(head_only)return mesh;
            if(loaded.models->combined_arms){
                const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
                for(auto v:loaded.models->combined_arms->vertices){const auto y=v.y;v.y=-v.z;v.z=y;mesh.vertices.push_back(v);}
                for(const auto index:loaded.models->combined_arms->indices)mesh.indices.push_back(first+index);
            }
            // The standing body deliberately excludes articulated arms. Attach the class's
            // visible upper/lower meshes using its retail joints, with hands lowered.
            if (loaded.models->first_person_arms.size()==2U) {
                const auto pose=world::evaluate_retail_third_person_pose(6U,0U,0.0,0U,70.0);
                const auto rotate=[](std::array<double,3U>& p,std::size_t axis,double degrees) {
                    const auto a=(axis+1U)%3U,b=(axis+2U)%3U;
                    const auto radians=degrees*3.14159265358979323846/180.0;
                    const auto c=std::cos(radians),s=std::sin(radians),old=p[a];
                    p[a]=c*old-s*p[b]; p[b]=s*old+c*p[b];
                };
                for (std::size_t i{}; i<pose.arms.size(); ++i) {
                    const auto& arm=pose.arms[i];
                    const auto& source_mesh=loaded.models->first_person_arms[i%2U];
                    const auto offset=world::retail_display_vector(arm.model_offset);
                    const auto position=world::retail_display_vector(arm.position);
                    const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
                    for (auto v:source_mesh.vertices) {
                        std::array<double,3U> p{v.x,v.y,v.z};
                        rotate(p,2U,arm.extra_roll_degrees); rotate(p,1U,arm.extra_yaw_degrees);
                        p[0]+=offset.x; p[1]+=offset.y; p[2]+=offset.z;
                        rotate(p,1U,arm.yaw_degrees); rotate(p,0U,arm.pitch_degrees); rotate(p,2U,arm.roll_degrees);
                        v.x=static_cast<float>(p[0]*pose.arm_model_scale+position.x);
                        v.y=static_cast<float>(p[1]*pose.arm_model_scale+position.y);
                        v.z=static_cast<float>(p[2]*pose.arm_model_scale+position.z);
                        mesh.vertices.push_back(v);
                        const std::array xyz{v.x,v.y,v.z};
                        for (std::size_t axis{}; axis<3U; ++axis) {
                            mesh.minimum[axis]=std::min(mesh.minimum[axis],xyz[axis]);
                            mesh.maximum[axis]=std::max(mesh.maximum[axis],xyz[axis]);
                        }
                    }
                    for (const auto index:source_mesh.indices) mesh.indices.push_back(first+index);
                }
            }
            return mesh;
        }
    }
    auto model=*source;
    if (item.kind!="weapon_model" && item.kind!="hat") model.apply_cosmetic_palette(item.palette);
    model.apply_default_color(team);
    return model.mesh();
}

std::optional<world::ChunkMesh> inventory_weapon_preview_mesh(const InventoryCosmetic& item,
        const std::filesystem::path& root,bool blue_team,const world::SkinVariantSelection& variants){
    if(item.scripted_skin.empty())return inventory_preview_mesh(item,root,blue_team);
    world::ScriptedWeapon skin;std::string error;
    if(!skin.load(root.parent_path()/item.scripted_skin,error,variants))return inventory_preview_mesh(item,root,blue_team);
    world::SkinInput input;input.dt=1.F/60.F;input.muted=true;
    for(int frame=0;frame<90;++frame)if(!skin.update(input,error))return std::nullopt;
    world::ChunkMesh combined;combined.minimum.fill(std::numeric_limits<float>::max());combined.maximum.fill(std::numeric_limits<float>::lowest());
    for(const auto& draw:skin.frame().models){
        if(skin.resources()[draw.resource].arm_part!=world::SkinArmPart::none)continue;
        auto part=skin.model_mesh(draw.resource,blue_team?world::VxlColor{72,111,181,255}:world::VxlColor{79,132,61,255});
        const auto offset=static_cast<std::uint32_t>(combined.vertices.size());const auto& m=draw.transform;
        for(auto vertex:part.vertices){
            const std::array xyz{m[0]*vertex.x+m[4]*vertex.y+m[8]*vertex.z+m[12],
                m[1]*vertex.x+m[5]*vertex.y+m[9]*vertex.z+m[13],m[2]*vertex.x+m[6]*vertex.y+m[10]*vertex.z+m[14]};
            vertex.x=xyz[0];vertex.y=xyz[1];vertex.z=xyz[2];combined.vertices.push_back(vertex);
            for(std::size_t k=0;k<3;++k){combined.minimum[k]=std::min(combined.minimum[k],xyz[k]);combined.maximum[k]=std::max(combined.maximum[k],xyz[k]);}
        }
        for(const auto index:part.indices)combined.indices.push_back(offset+index);
    }
    return combined.empty()?inventory_preview_mesh(item,root,blue_team):std::optional{std::move(combined)};
}

std::vector<std::uint8_t> build_inventory_preview(const InventoryCosmetic& item,
                                                  const std::filesystem::path& root,
                                                  bool blue_team,
                                                  double angle,
                                                  double zoom) {
    const auto trusted_items=builtins();
    const auto trusted_item=std::ranges::find(trusted_items,item.id,&InventoryCosmetic::id);
    if (trusted_item!=trusted_items.end() && trusted_item->kind=="weapon_model" && !trusted_item->scripted_skin.empty()) {
        if (const auto mesh=inventory_weapon_preview_mesh(*trusted_item,root,blue_team,{}))
            return world::cosmetic_preview(*mesh,angle,zoom);
    }
    if (item.kind=="character_skin" || item.kind=="hat" || item.kind=="prop_model") {
        if (const auto mesh=inventory_preview_mesh(item,root,blue_team)) return world::cosmetic_preview(*mesh,angle,zoom);
        throw std::runtime_error{"Character preview requires the verified game models."};
    }
    auto trusted = builtins();
    trusted.push_back({"supply-crate-preview",
                       "Supply Crate",
                       "crate",
                       "common",
                       "kv6/ammocrate.kv6",
                       "9d934d276712031d4acf50a178e2441c9bcbba3044b3ad932d294b6b4d9f70c9",
                       {},
                       {},
                       false,
                       false});
    const auto found = std::ranges::find(trusted, item.id, &InventoryCosmetic::id);
    if (found == trusted.end() || found->kind == "profile_badge")
        return {};
    const auto path = found->asset.starts_with("client/cosmetics/")
        ? world::resolve_cosmetic_file(root.parent_path()/found->asset) : root/found->asset;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > 2U * 1024U * 1024U)
        throw std::runtime_error{"Preview model is missing or too large."};
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    std::ifstream input{path, std::ios::binary};
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!input || hex(sha(bytes)) != found->sha256)
        throw std::runtime_error{"Preview requires the original verified game asset."};
    const auto model = world::Kv6Model::load(
        std::span{reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()});
    if (!model)
        throw std::runtime_error{"Preview model could not be decoded."};
    return world::cosmetic_preview(*model,
                                   (found->kind == "crate" || found->kind == "weapon_model" || found->kind == "hat") ? std::nullopt
                                                          : std::optional{found->palette},
                                   blue_team,
                                   angle,
                                   zoom);
}

bool verify_inventory_receipt(const Json& receipt, const InventoryData& before) {
    try {
        if (receipt.at("schema_version") != 1 || receipt.at("algorithm") != "aos-crate-v1")
            return false;
        const auto catalogs = Json::parse(inventory_pools_json);
        const auto version = bounded(receipt,"catalog_version");
        bool catalog_valid{};
        for (const auto& catalog : catalogs) if (catalog.at("version")==version &&
            catalog.at("digest")==bounded(receipt,"catalog_digest",64U)) catalog_valid=true;
        if (!catalog_valid) return false;
        const auto before_pool = inventory_crate_pool(before,version);
        const auto seed = unhex(bounded(receipt, "seed", 64U));
        const auto crate = bounded(receipt, "crate_id"), account = bounded(receipt, "account_id"),
                   nonce = bounded(receipt, "client_nonce", 64U);
        const auto granted = std::ranges::find(before.crates, crate, &InventoryCrate::id);
        if (granted == before.crates.end() || granted->catalog_version != version ||
            granted->commitment != bounded(receipt, "commitment", 64U))
            return false;
        std::string commit = "aos-crate-v1" + crate;
        commit.append(reinterpret_cast<const char*>(seed.data()), seed.size());
        if (hex(sha(commit)) != bounded(receipt, "commitment", 64U))
            return false;
        const auto pity = receipt.at("pity_before").get<std::array<std::uint32_t, 3U>>();
        if (pity != before_pool.pity)
            return false;
        if (std::ranges::any_of(pity, [](auto n) { return n > 100U; }))
            return false;
        const auto pity_hash = hex(sha(Json(pity).dump()));
        if (pity_hash != bounded(receipt, "pity_snapshot_hash", 64U))
            return false;
        std::string input = "aos-crate-roll-v1";
        input.append(reinterpret_cast<const char*>(seed.data()), seed.size());
        input += nonce + crate + account + bounded(receipt, "catalog_version") + pity_hash;
        const auto roll_seed = sha(input);
        if (hex(roll_seed) != bounded(receipt, "roll_digest", 64U))
            return false;
        constexpr std::array<std::string_view, 5U> rarities{
            "common", "uncommon", "rare", "epic", "legendary"};
        constexpr std::array<std::uint32_t, 5U> weights{55U, 27U, 12U, 5U, 1U};
        const auto tier = [&](const InventoryCosmetic& item) {
            for (std::size_t i = 0U; i < rarities.size(); ++i)
                if (rarities[i] == item.rarity)
                    return i;
            throw std::runtime_error{"Unknown receipt rarity."};
        };
        std::vector<InventoryCosmetic> pool;
        for (const auto& item : before_pool.items)
            if (!item.owned)
                pool.push_back(item);
        constexpr std::array<std::uint32_t, 3U> thresholds{9U, 39U, 99U};
        for (std::size_t offset = 0U; offset < 3U; ++offset) {
            const auto counter = 2U - offset, minimum = 4U - offset;
            if (pity[counter] < thresholds[counter])
                continue;
            if (std::ranges::any_of(pool,
                                    [&](const auto& item) { return tier(item) >= minimum; })) {
                std::erase_if(pool, [&](const auto& item) { return tier(item) < minimum; });
                break;
            }
        }
        std::ranges::sort(pool, {}, &InventoryCosmetic::id);
        std::array<std::uint32_t, 5U> counts{};
        for (const auto& item : pool)
            ++counts[tier(item)];
        if (Json(counts) != receipt.at("candidate_counts") || pool.empty())
            return false;
        std::uint32_t counter{};
        const auto uniform = [&](std::uint32_t bound) {
            if (bound == 0U || bound > 1000000U)
                throw std::runtime_error{"Invalid receipt draw pool."};
            const auto limit = (0x100000000ULL / bound) * bound;
            for (;;) {
                std::string draw = "aos-crate-draw-v1";
                draw.append(reinterpret_cast<const char*>(roll_seed.data()), roll_seed.size());
                for (int byte = 3; byte >= 0; --byte)
                    draw.push_back(
                        static_cast<char>((counter >> (8U * static_cast<unsigned>(byte))) & 255U));
                ++counter;
                const auto digest = sha(draw);
                const auto n = (static_cast<std::uint32_t>(digest[0]) << 24U) |
                               (static_cast<std::uint32_t>(digest[1]) << 16U) |
                               (static_cast<std::uint32_t>(digest[2]) << 8U) | digest[3];
                if (n < limit)
                    return n % bound;
            }
        };
        std::uint32_t total{};
        for (std::size_t i = 0U; i < 5U; ++i)
            if (counts[i])
                total += weights[i];
        auto roll = uniform(total);
        std::size_t selected_tier{};
        for (; selected_tier < 5U; ++selected_tier) {
            const auto weight = counts[selected_tier] ? weights[selected_tier] : 0U;
            if (roll < weight)
                break;
            roll -= weight;
        }
        std::erase_if(pool, [&](const auto& item) { return tier(item) != selected_tier; });
        const auto& selected = pool[uniform(static_cast<std::uint32_t>(pool.size()))];
        const std::array<std::uint32_t, 3U> after{
            selected_tier >= 2U ? 0U : std::min(100U, pity[0] + 1U),
            selected_tier >= 3U ? 0U : std::min(100U, pity[1] + 1U),
            selected_tier >= 4U ? 0U : std::min(100U, pity[2] + 1U)};
        if (Json(after) != receipt.at("pity_after"))
            return false;
        return selected.id == bounded(receipt.at("item"), "id") &&
               selected.rarity == bounded(receipt.at("item"), "rarity");
    } catch (const std::exception&) {
        return false;
    }
}

InventorySession::InventorySession(std::shared_ptr<network::RevivalIdentityService> identity)
    : identity_{std::move(identity)} {}
InventorySession::~InventorySession() {
    cancel();
}
void InventorySession::cancel() noexcept {
    stop_.request_stop();
}
void InventorySession::start(InventoryMenuModel& model, InventoryAction action) {
    if (worker_.valid())
        return;
    if (action == InventoryAction::none || action == InventoryAction::creators || model.reveal)
        return;
    const bool mutation = action == InventoryAction::open || action == InventoryAction::equip ||
        action == InventoryAction::unequip || action == InventoryAction::equip_weapon ||
        action == InventoryAction::unequip_weapon;
    if (mutation && (!model.online || !model.loaded || model.data.guest ||
        (action == InventoryAction::open ? !model.data.opening_enabled : !model.data.equip_enabled)))
        return;
    // Browsing installed content does not require an account response. Only a
    // verified snapshot supplies ownership, equipped slots and crate balances.
    if (model.data.items.empty()) model.data.items = builtins();
    const auto account = identity_->cached_account();
    if (!account) {
        model.fail("Sign in to use your persistent collection.");
        return;
    }
    network::InventoryRequest request;
    request.expected_account = account->public_id;
    if (action == InventoryAction::open) {
        const auto* crate = model.selected_crate();
        if (!crate)
            return;
        request.kind = network::InventoryRequestKind::open;
        request.target = crate->id;
        std::array<unsigned char, 32U> nonce{};
        randombytes_buf(nonce.data(), nonce.size());
        request.nonce = hex(nonce);
        std::array<unsigned char, 16U> id{};
        randombytes_buf(id.data(), id.size());
        id[6] = (id[6] & 15U) | 64U;
        id[8] = (id[8] & 63U) | 128U;
        const auto encoded = hex(id);
        request.idempotency_key = encoded.substr(0, 8) + "-" + encoded.substr(8, 4) + "-" +
                                  encoded.substr(12, 4) + "-" + encoded.substr(16, 4) + "-" +
                                  encoded.substr(20);
    } else if (action == InventoryAction::equip || action == InventoryAction::unequip ||
               action == InventoryAction::equip_weapon || action == InventoryAction::unequip_weapon) {
        const auto* item = model.selected_item();
        if (!item || !item->owned || item->slots.empty())
            return;
        request.kind = (action == InventoryAction::equip || action == InventoryAction::equip_weapon) ? network::InventoryRequestKind::equip
                                                        : network::InventoryRequestKind::unequip;
        request.target = item->slots[std::min(model.slot_index, item->slots.size() - 1U)];
        request.cosmetic_id = item->id;
        request.revision = model.data.revision;
        if ((action==InventoryAction::equip_weapon || action==InventoryAction::unequip_weapon) && request.target.starts_with("weapon:")) {
            const auto prefix=request.target.substr(0,request.target.rfind(':')+1U);
            const auto paired_slot=prefix+(request.target.ends_with(":view")?"world":"view");
            if (std::ranges::find(item->slots,paired_slot)==item->slots.end()) return;
        }
    } else if (action == InventoryAction::more_crates || action == InventoryAction::more_history ||
               action == InventoryAction::previous_crates ||
               action == InventoryAction::previous_history) {
        const auto crates =
            action == InventoryAction::more_crates || action == InventoryAction::previous_crates;
        request.kind =
            crates ? network::InventoryRequestKind::crates : network::InventoryRequestKind::history;
        request.target = crates ? model.data.next_crates : model.data.next_history;
        if (action == InventoryAction::previous_crates ||
            action == InventoryAction::previous_history) {
            const auto& cursors = crates ? model.data.crate_cursors : model.data.history_cursors;
            if (cursors.size() < 2U)
                return;
            request.target = cursors[cursors.size() - 2U];
        }
    }
    model.busy = true;
    model.error.clear();
    stop_ = std::stop_source{};
    worker_ = std::async(
        std::launch::async,
        [identity = identity_,
         request,
         action,
         before = model.data,
         account_id = account->public_id,
         stop = stop_.get_token()] () mutable {
            Outcome out;
            out.account = account_id;
            network::InventoryRequest snapshot_request;
            snapshot_request.expected_account = account_id;
            try {
                auto result = identity->inventory_request(request, stop);
                // A lost opening response must retry the same key and nonce.
                if (!result && request.kind == network::InventoryRequestKind::open && !stop.stop_requested() &&
                    (result.http_status == 0 || result.http_status == 502 || result.http_status == 503 || result.http_status == 504))
                    result = identity->inventory_request(request, stop);
                if (!result && !stop.stop_requested() &&
                    (request.kind==network::InventoryRequestKind::equip || request.kind==network::InventoryRequestKind::unequip) &&
                    (result.http_status==0 || result.http_status==502 || result.http_status==503 || result.http_status==504)) {
                    const auto fresh=identity->inventory_request(snapshot_request,stop);
                    if(fresh) {
                        before=parse_inventory_snapshot(fresh.payload);
                        const auto confirmed=[&](const std::string& target) {
                            const auto found=std::ranges::find_if(before.equipped,[&](const auto& entry){return entry.first==target;});
                            return request.kind==network::InventoryRequestKind::equip
                                ? found!=before.equipped.end()&&found->second==request.cosmetic_id : found==before.equipped.end();
                        };
                        const auto pair=request.target.starts_with("weapon:")
                            ? request.target.substr(0,request.target.rfind(':')+1U)+(request.target.ends_with(":view")?"world":"view") : request.target;
                        if(confirmed(request.target)&&confirmed(pair)) {
                            out.data=before;save_cache(cache_path(account_id),fresh.payload);return out;
                        }
                        request.revision=before.revision;
                        result=identity->inventory_request(request,stop);
                    }
                }
                if (!result && result.error_code == "inventory_conflict") {
                    const auto fresh = identity->inventory_request(snapshot_request, stop);
                    if (fresh) {
                        before = parse_inventory_snapshot(fresh.payload);
                        out.data = before;
                        request.revision = before.revision;
                        // Retry the selected slot once against the new authoritative revision.
                        result = identity->inventory_request(request, stop);
                    }
                }
                if (!result) {
                    out.offline = result.http_status == 0 || result.http_status >= 500 ||
                        result.http_status == 401 || result.http_status == 403;
                    throw std::runtime_error{result.error};
                }
                if (request.kind == network::InventoryRequestKind::crates ||
                    request.kind == network::InventoryRequestKind::history) {
                    auto data = before;
                    const auto& items = result.payload.at("items");
                    if (!items.is_array() || items.size() > 50U)
                        throw std::runtime_error{"Invalid collection page."};
                    const auto cursor = result.payload.at("next_cursor").is_string()
                                            ? bounded(result.payload, "next_cursor", 256U)
                                            : std::string{};
                    auto& cursors = request.kind == network::InventoryRequestKind::crates
                                        ? data.crate_cursors
                                        : data.history_cursors;
                    if (action == InventoryAction::previous_crates ||
                        action == InventoryAction::previous_history)
                        cursors.pop_back();
                    else {
                        // Bound navigation memory independently of lifetime account size.
                        if (cursors.size() == 256U)
                            cursors.erase(cursors.begin());
                        cursors.push_back(request.target);
                    }
                    if (request.kind == network::InventoryRequestKind::crates) {
                        data.crates.clear();
                        for (const auto& item : items)
                            data.crates.push_back({bounded(item, "id"),
                                                   decimal(item, "level"),
                                                   bounded(item, "commitment", 64U),item.value("catalog_version","supply-v1")});
                        data.next_crates = cursor;
                    } else {
                        data.history.clear();
                        for (const auto& item : items)
                            data.history.push_back(opening(item.at("receipt")));
                        data.next_history = cursor;
                    }
                    out.data = std::move(data);
                    return out;
                }
                if (request.kind == network::InventoryRequestKind::open) {
                    if (bounded(result.payload, "crate_id") != request.target)
                        throw std::runtime_error{"Opening receipt belongs to a different crate. "
                                                 "Refresh your collection."};
                    out.receipt = opening(result.payload);
                    out.receipt->verified = verify_inventory_receipt(result.payload, before);
                }
                auto snapshot = result;
                if (request.kind != network::InventoryRequestKind::snapshot) {
                    if (result.payload.contains("collection")) snapshot.payload = result.payload.at("collection");
                    else snapshot = identity->inventory_request(snapshot_request, stop); // Older service during rollout.
                }
                if (!snapshot)
                    throw std::runtime_error{
                        out.receipt ? "Your reward is saved. Refresh to reload the collection."
                                    : snapshot.error};
                out.data = parse_inventory_snapshot(snapshot.payload);
                save_cache(cache_path(account_id), snapshot.payload);
            } catch (const std::exception& error) {
                out.error = error.what();
                if (request.kind == network::InventoryRequestKind::snapshot) {
                    out.offline = true;
                    try {
                        const auto path = cache_path(account_id);
                        std::error_code ec;
                        if (std::filesystem::file_size(path, ec) <= 512U * 1024U && !ec) {
                            std::ifstream input{path};
                            Json cached;
                            input >> cached;
                            out.data = parse_inventory_snapshot(cached);
                            out.error = "Offline collection. Reconnect to open or equip.";
                        }
                    } catch (const std::exception&) { /* A corrupt cache is disposable. */
                    }
                }
            }
            return out;
        });
}
void InventorySession::pump(InventoryMenuModel& model) {
    using namespace std::chrono_literals;
    if (!worker_.valid() || worker_.wait_for(0ms) != std::future_status::ready)
        return;
    const auto result = worker_.get();
    // cancel() also invalidates an already-completed reply, including when
    // the user signs back into the same account before the next UI pump.
    if (stop_.stop_requested()) {
        model.busy = false;
        return;
    }
    const auto account = identity_->cached_account();
    if (!account || account->public_id != result.account) {
        model = InventoryMenuModel{};
        return;
    }
    if (result.data)
        model.complete(*result.data);
    model.busy = false;
    if (result.receipt)
        model.reveal = result.receipt;
    if (!result.error.empty()) {
        if (model.loaded && !result.offline)
            model.error = result.error;
        else
            model.fail(result.error);
    }
}
} // namespace battlespades::frontend
