#include "battlespades/frontend/local_match_store.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace battlespades::frontend;
namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("battlespades-match-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto path = root / "local_match.json";
    try {
        std::string error;
        check(!load_local_match_configuration(path, error) && error.empty(), "first run uses defaults");
        CreateMatchConfiguration saved;
        saved.privacy = CreateMatchPrivacy::invite_only;
        saved.retail_playlist_id = 13;
        saved.map_name = "SpookyMansion";
        saved.max_players = 16;
        saved.match_minutes = 10;
        saved.bot_count = 6;
        saved.bot_difficulty = "hard";
        saved.server_port = 32887;
        saved.rule_overrides["RULE_ZOMBIE_NOOF_ROUNDS"] = "3";
        check(save_local_match_configuration(path, saved, error), "save complete match choices");
        const auto loaded = load_local_match_configuration(path, error);
        check(loaded && *loaded == saved, "map, mode, bots, privacy, port and rules survive restart");
        saved.bot_count = 8;
        check(save_local_match_configuration(path, saved, error), "replace existing save atomically");
        check(load_local_match_configuration(path, error) == saved, "replacement survives restart");
        auto bad = saved;
        bad.map_name = "../../bad";
        check(!save_local_match_configuration(path, bad, error), "reject path traversal");
        check(load_local_match_configuration(path, error) == saved, "failed save preserves previous file");
        saved.custom_map = true;
        saved.map_name = "MyArena";
        saved.map_title = "My Arena";
        saved.map_author = "Player";
        check(save_local_match_configuration(path, saved, error), "save custom map identity");
        check(load_local_match_configuration(path, error) == saved, "retain custom map until catalog scan");
        CreateMatchMenuModel lobby;
        lobby.set_custom_maps({CreateMatchCustomMap{"MyArena", "My Arena", "Player", {"zom"}, false}});
        lobby.apply_authoritative_configuration(saved);
        check(lobby.configuration() == saved, "restore an installed compatible custom map");
        CreateMatchMenuModel missing;
        missing.apply_authoritative_configuration(saved);
        check(!missing.configuration().custom_map, "missing custom maps fall back to a stock map");
        CreateMatchMenuModel classic_zombies;
        check(classic_zombies.open_page(CreateMatchPage::choose_game_mode), "open mode picker");
        check(classic_zombies.activate_row("czom"), "select Classic+ Zombies");
        check(classic_zombies.configuration().retail_playlist_id == 14U,
              "Classic+ Zombies has a distinct saved playlist identity");
        check(classic_zombies.open_page(CreateMatchPage::game_rules), "open Zombie rules");
        const auto rules = classic_zombies.presentation().rows;
        check(std::ranges::any_of(rules, [](const auto& row) {
                  return row.stable_key == "RULE_ZOMBIE_NOOF_ROUNDS";
              }), "Classic+ Zombies retains infection settings");
        { std::ofstream output{path}; output << "{ broken"; }
        check(!load_local_match_configuration(path, error) && !error.empty(), "corrupt save reports error");
        { std::ofstream output{path}; output << std::string(65537, ' '); }
        check(!load_local_match_configuration(path, error) && !error.empty(), "oversized save is bounded");
        std::filesystem::remove(path);
        std::filesystem::remove(root);
        std::cout << "Local match autosave tests passed\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        std::filesystem::remove(root, ignored);
        return 1;
    }
}
