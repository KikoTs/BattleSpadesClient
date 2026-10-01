#include "battlespades/updater/steam_library.hpp"

#include "battlespades/updater/binary_vdf.hpp"
#include "battlespades/updater/file_util.hpp"
#include "battlespades/updater/text_vdf.hpp"

#include <algorithm>
#include <array>
#include <charconv>

namespace battlespades::updater {
namespace {

template <typename Integer>
[[nodiscard]] std::optional<Integer> parse_unsigned(std::string_view text) noexcept {
    if (text.empty()) return std::nullopt;
    Integer value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return std::nullopt;
    return value;
}

[[nodiscard]] std::string lower_generic(const std::filesystem::path& path) {
    auto text = generic_utf8(path.lexically_normal());
    while (text.size() > 1U && text.back() == '/') text.pop_back();
    for (auto& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

[[nodiscard]] const TextVdfNode* first_root(const TextVdfDocument& document) {
    return document.roots().empty() ? nullptr : &document.roots().front();
}

[[nodiscard]] std::string quote_path(const std::filesystem::path& path) {
    return '"' + path_to_utf8(path.lexically_normal().make_preferred()) + '"';
}

[[nodiscard]] BinaryVdfNode* shortcuts_root(std::vector<BinaryVdfNode>& roots) {
    for (auto& node : roots) {
        if (node.type == BinaryVdfType::map && vdf_key_equals(node.key, "shortcuts")) return &node;
    }
    return nullptr;
}

[[nodiscard]] std::optional<std::vector<BinaryVdfNode>>
load_shortcuts(const std::vector<std::uint8_t>& file, std::string& error) {
    if (file.empty()) {
        std::vector<BinaryVdfNode> roots;
        roots.push_back(BinaryVdfNode::make_map("shortcuts"));
        return roots;
    }
    auto roots = parse_binary_vdf(file, error);
    if (!roots.has_value()) return std::nullopt;
    if (shortcuts_root(*roots) == nullptr) roots->push_back(BinaryVdfNode::make_map("shortcuts"));
    return roots;
}

[[nodiscard]] std::optional<std::uint32_t> entry_app_id(const BinaryVdfNode& entry) {
    if (const auto* id = entry.child("appid"); id != nullptr) return id->as_uint32();
    const auto* exe = entry.child("Exe");
    const auto* name = entry.child("AppName");
    if (exe != nullptr && name != nullptr && exe->type == BinaryVdfType::string &&
        name->type == BinaryVdfType::string) {
        return shortcut_app_id(exe->text, name->text);
    }
    return std::nullopt;
}

void set_string_field(BinaryVdfNode& entry, std::string_view key, std::string value) {
    if (auto* field = entry.child(key); field != nullptr && field->type == BinaryVdfType::string) {
        field->text = std::move(value);
        return;
    }
    entry.children.push_back(BinaryVdfNode::make_string(std::string{key}, std::move(value)));
}

void set_int_field(BinaryVdfNode& entry, std::string_view key, std::uint32_t value) {
    if (auto* field = entry.child(key); field != nullptr) {
        *field = BinaryVdfNode::make_int32(field->key, value);
        return;
    }
    entry.children.push_back(BinaryVdfNode::make_int32(std::string{key}, value));
}

} // namespace

std::optional<std::vector<SteamLibraryFolder>> parse_library_folders(std::string_view vdf_text,
                                                                     std::string& error) {
    const auto document = TextVdfDocument::parse(std::string{vdf_text}, error);
    if (!document.has_value()) return std::nullopt;
    const auto* root = first_root(*document);
    if (root == nullptr || !root->is_object || !vdf_key_equals(root->key, "libraryfolders")) {
        error = "libraryfolders.vdf has no \"libraryfolders\" root";
        return std::nullopt;
    }
    std::vector<SteamLibraryFolder> folders;
    for (const auto& entry : root->children) {
        if (!parse_unsigned<std::uint32_t>(entry.key).has_value()) continue;
        SteamLibraryFolder folder;
        if (entry.is_object) {
            const TextVdfNode* path = nullptr;
            for (const auto& child : entry.children) {
                if (vdf_key_equals(child.key, "path") && !child.is_object) path = &child;
                if (vdf_key_equals(child.key, "apps") && child.is_object) {
                    for (const auto& app : child.children) {
                        if (const auto id = parse_unsigned<std::uint32_t>(app.key); id.has_value()) {
                            folder.apps.push_back(*id);
                        }
                    }
                }
            }
            if (path == nullptr || path->value.empty()) continue;
            folder.path = path_from_utf8(path->value);
        } else {
            if (entry.value.empty()) continue;
            folder.path = path_from_utf8(entry.value);
        }
        folders.push_back(std::move(folder));
    }
    return folders;
}

std::optional<std::string> parse_app_manifest_install_dir(std::string_view acf_text) {
    std::string error;
    const auto document = TextVdfDocument::parse(std::string{acf_text}, error);
    if (!document.has_value()) return std::nullopt;
    const auto* root = first_root(*document);
    if (root == nullptr || !root->is_object) return std::nullopt;
    auto value = document->get_string({root->key, "installdir"});
    if (!value.has_value() || value->empty()) return std::nullopt;
    // An install directory is one path component; refuse anything that could
    // walk out of steamapps/common.
    if (value->find_first_of("/\\:") != std::string::npos || *value == "." || *value == "..") {
        return std::nullopt;
    }
    return value;
}

std::optional<SteamGameLocation> locate_steam_app(const std::filesystem::path& steam_root,
                                                  std::uint32_t app_id, std::string& error) {
    std::vector<SteamLibraryFolder> folders;
    const auto library_file = steam_root / "steamapps" / "libraryfolders.vdf";
    std::error_code code;
    if (std::filesystem::is_regular_file(library_file, code)) {
        std::string read_error;
        if (const auto text = read_text_file(library_file, read_error); text.has_value()) {
            std::string parse_error;
            if (auto parsed = parse_library_folders(*text, parse_error); parsed.has_value()) {
                folders = std::move(*parsed);
            }
        }
    }
    const auto root_key = lower_generic(steam_root);
    if (std::ranges::none_of(folders, [&](const auto& f) { return lower_generic(f.path) == root_key; })) {
        folders.insert(folders.begin(), SteamLibraryFolder{steam_root, {}});
    }
    std::ranges::stable_partition(folders, [&](const SteamLibraryFolder& folder) {
        return std::ranges::find(folder.apps, app_id) != folder.apps.end();
    });

    const auto manifest_name = "appmanifest_" + std::to_string(app_id) + ".acf";
    for (const auto& folder : folders) {
        const auto manifest = folder.path / "steamapps" / manifest_name;
        if (!std::filesystem::is_regular_file(manifest, code)) continue;
        std::string read_error;
        const auto text = read_text_file(manifest, read_error);
        if (!text.has_value()) continue;
        const auto install = parse_app_manifest_install_dir(*text);
        if (!install.has_value()) continue;
        const auto directory = folder.path / "steamapps" / "common" / path_from_utf8(*install);
        if (std::filesystem::is_directory(directory, code)) {
            error.clear();
            return SteamGameLocation{folder.path, directory};
        }
    }
    error = "app " + std::to_string(app_id) + " is not installed in any Steam library";
    return std::nullopt;
}

std::vector<SteamLoginUser> parse_login_users(std::string_view vdf_text) {
    std::vector<SteamLoginUser> users;
    std::string error;
    const auto document = TextVdfDocument::parse(std::string{vdf_text}, error);
    if (!document.has_value()) return users;
    const auto* root = first_root(*document);
    if (root == nullptr || !root->is_object) return users;
    for (const auto& entry : root->children) {
        const auto id = parse_unsigned<std::uint64_t>(entry.key);
        if (!id.has_value() || !entry.is_object || *id < steam_id64_base) continue;
        SteamLoginUser user;
        user.steam_id = *id;
        for (const auto& field : entry.children) {
            if (field.is_object) continue;
            if (vdf_key_equals(field.key, "AccountName")) user.account_name = field.value;
            if (vdf_key_equals(field.key, "MostRecent")) user.most_recent = field.value == "1";
            if (vdf_key_equals(field.key, "Timestamp")) {
                user.timestamp = parse_unsigned<std::uint64_t>(field.value).value_or(0U);
            }
        }
        users.push_back(std::move(user));
    }
    return users;
}

std::uint32_t account_id_from_steam_id(std::uint64_t steam_id) noexcept {
    return static_cast<std::uint32_t>(steam_id & 0xffffffffULL);
}

std::vector<std::uint32_t> list_userdata_accounts(const std::filesystem::path& steam_root) {
    std::vector<std::uint32_t> accounts;
    std::error_code code;
    const auto userdata = steam_root / "userdata";
    for (std::filesystem::directory_iterator it{userdata, code}, end; !code && it != end;
         it.increment(code)) {
        if (!it->is_directory(code)) continue;
        const auto id = parse_unsigned<std::uint32_t>(path_to_utf8(it->path().filename()));
        if (id.has_value() && *id != 0U) accounts.push_back(*id);
    }
    std::ranges::sort(accounts);
    return accounts;
}

std::optional<std::uint32_t> pick_steam_account(const std::filesystem::path& steam_root) {
    const auto accounts = list_userdata_accounts(steam_root);
    if (accounts.empty()) return std::nullopt;
    const auto has_userdata = [&](std::uint32_t id) {
        return std::ranges::binary_search(accounts, id);
    };

    std::string error;
    if (const auto text = read_text_file(steam_root / "config" / "loginusers.vdf", error);
        text.has_value()) {
        const auto users = parse_login_users(*text);
        for (const auto& user : users) {
            if (user.most_recent && has_userdata(account_id_from_steam_id(user.steam_id))) {
                return account_id_from_steam_id(user.steam_id);
            }
        }
        const SteamLoginUser* newest = nullptr;
        for (const auto& user : users) {
            if (!has_userdata(account_id_from_steam_id(user.steam_id))) continue;
            if (newest == nullptr || user.timestamp > newest->timestamp) newest = &user;
        }
        if (newest != nullptr) return account_id_from_steam_id(newest->steam_id);
    }

    std::optional<std::uint32_t> best;
    std::filesystem::file_time_type best_time{};
    for (const auto id : accounts) {
        std::error_code code;
        const auto time = std::filesystem::last_write_time(local_config_path(steam_root, id), code);
        if (!code && (!best.has_value() || time > best_time)) {
            best = id;
            best_time = time;
        }
    }
    return best.has_value() ? best : std::optional<std::uint32_t>{accounts.front()};
}

std::filesystem::path local_config_path(const std::filesystem::path& steam_root, std::uint32_t account) {
    return steam_root / "userdata" / std::to_string(account) / "config" / "localconfig.vdf";
}

std::filesystem::path shortcuts_path(const std::filesystem::path& steam_root, std::uint32_t account) {
    return steam_root / "userdata" / std::to_string(account) / "config" / "shortcuts.vdf";
}

std::vector<std::string> launch_options_key_path(std::uint32_t app_id) {
    return {"UserLocalConfigStore", "Software", "Valve", "Steam", "apps", std::to_string(app_id),
            "LaunchOptions"};
}

std::string make_launch_options(const std::filesystem::path& launcher) {
    return quote_path(launcher) + " %command%";
}

std::uint32_t crc32(std::string_view data) noexcept {
    static const auto table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t i = 0; i < 256U; ++i) {
            std::uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit) c = (c & 1U) != 0U ? 0xEDB88320U ^ (c >> 1U) : c >> 1U;
            values[i] = c;
        }
        return values;
    }();
    std::uint32_t crc = 0xffffffffU;
    for (const char c : data) {
        crc = table[(crc ^ static_cast<unsigned char>(c)) & 0xffU] ^ (crc >> 8U);
    }
    return crc ^ 0xffffffffU;
}

std::uint32_t shortcut_app_id(std::string_view quoted_exe, std::string_view app_name) noexcept {
    std::string key{quoted_exe};
    key += app_name;
    return crc32(key) | 0x80000000U;
}

std::optional<std::vector<std::uint8_t>> upsert_shortcut(const std::vector<std::uint8_t>& file,
                                                         const SteamShortcut& shortcut,
                                                         std::uint32_t& app_id, std::string& error) {
    auto roots = load_shortcuts(file, error);
    if (!roots.has_value()) return std::nullopt;
    auto& list = *shortcuts_root(*roots);
    const auto exe = quote_path(shortcut.exe);
    app_id = shortcut_app_id(exe, shortcut.app_name);

    BinaryVdfNode* entry = nullptr;
    for (auto& candidate : list.children) {
        if (candidate.type != BinaryVdfType::map) continue;
        if (entry_app_id(candidate) == app_id) {
            entry = &candidate;
            break;
        }
    }
    if (entry == nullptr) {
        std::size_t next{};
        for (const auto& candidate : list.children) {
            if (const auto index = parse_unsigned<std::size_t>(candidate.key); index.has_value()) {
                next = std::max(next, *index + 1U);
            }
        }
        list.children.push_back(BinaryVdfNode::make_map(std::to_string(next)));
        entry = &list.children.back();
        // Field order matches what current Steam clients write.
        entry->children.push_back(BinaryVdfNode::make_int32("appid", app_id));
        entry->children.push_back(BinaryVdfNode::make_string("AppName", shortcut.app_name));
        entry->children.push_back(BinaryVdfNode::make_string("Exe", exe));
        entry->children.push_back(BinaryVdfNode::make_string("StartDir", quote_path(shortcut.start_dir)));
        entry->children.push_back(BinaryVdfNode::make_string("icon", path_to_utf8(shortcut.icon)));
        entry->children.push_back(BinaryVdfNode::make_string("ShortcutPath", ""));
        entry->children.push_back(BinaryVdfNode::make_string("LaunchOptions", shortcut.launch_options));
        entry->children.push_back(BinaryVdfNode::make_int32("IsHidden", 0U));
        entry->children.push_back(BinaryVdfNode::make_int32("AllowDesktopConfig", 1U));
        entry->children.push_back(BinaryVdfNode::make_int32("AllowOverlay", 1U));
        entry->children.push_back(BinaryVdfNode::make_int32("OpenVR", 0U));
        entry->children.push_back(BinaryVdfNode::make_int32("Devkit", 0U));
        entry->children.push_back(BinaryVdfNode::make_string("DevkitGameID", ""));
        entry->children.push_back(BinaryVdfNode::make_int32("DevkitOverrideAppID", 0U));
        entry->children.push_back(BinaryVdfNode::make_int32("LastPlayTime", 0U));
        entry->children.push_back(BinaryVdfNode::make_string("FlatpakAppID", ""));
        entry->children.push_back(BinaryVdfNode::make_map("tags"));
    } else {
        set_int_field(*entry, "appid", app_id);
        set_string_field(*entry, "AppName", shortcut.app_name);
        set_string_field(*entry, "Exe", exe);
        set_string_field(*entry, "StartDir", quote_path(shortcut.start_dir));
        set_string_field(*entry, "icon", path_to_utf8(shortcut.icon));
        set_string_field(*entry, "LaunchOptions", shortcut.launch_options);
    }
    return serialize_binary_vdf(*roots);
}

std::optional<std::vector<std::uint8_t>> remove_shortcut(const std::vector<std::uint8_t>& file,
                                                         std::uint32_t app_id, bool& removed,
                                                         std::string& error) {
    removed = false;
    if (file.empty()) return file;
    auto roots = parse_binary_vdf(file, error);
    if (!roots.has_value()) return std::nullopt;
    auto* list = shortcuts_root(*roots);
    if (list == nullptr) return file;
    const auto before = list->children.size();
    std::erase_if(list->children, [&](const BinaryVdfNode& entry) {
        return entry.type == BinaryVdfType::map && entry_app_id(entry) == app_id;
    });
    removed = list->children.size() != before;
    if (!removed) return file;
    std::size_t index{};
    for (auto& entry : list->children) entry.key = std::to_string(index++);
    return serialize_binary_vdf(*roots);
}

bool has_shortcut(const std::vector<std::uint8_t>& file, std::uint32_t app_id) {
    std::string error;
    auto roots = parse_binary_vdf(file, error);
    if (!roots.has_value()) return false;
    const auto* list = shortcuts_root(*roots);
    if (list == nullptr) return false;
    return std::ranges::any_of(list->children, [&](const BinaryVdfNode& entry) {
        return entry.type == BinaryVdfType::map && entry_app_id(entry) == app_id;
    });
}

} // namespace battlespades::updater
