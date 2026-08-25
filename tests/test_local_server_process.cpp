#include "battlespades/platform/local_server_process.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

void serializes_one_disposable_match_without_public_services() {
    battlespades::platform::LocalServerLaunchConfig config;
    config.server_name = "KikoTs's Lobby";
    config.mode = "ctf";
    config.map_name = "TokyoNeon";
    config.maximum_players = 16U;
    config.match_minutes = 30U;
    config.bot_count = 4U;
    config.bot_difficulty = "hard";
    config.rule_overrides = {
        {"RULE_CTF_SCORE_TARGET", "5"},
        {"RULE_ENABLE_PREFABS", "OFF"},
    };
    const auto toml = battlespades::platform::build_local_server_toml(config, 27018U);
    expect(toml.find("port = 27018") != std::string::npos &&
               toml.find("default_mode = \"ctf\"") != std::string::npos &&
               toml.find("default_map = \"TokyoNeon\"") != std::string::npos &&
               toml.find("match_length_minutes = 30") != std::string::npos &&
               toml.find("fill_target = 4") != std::string::npos &&
               toml.find("RULE_CTF_SCORE_TARGET = \"5\"") != std::string::npos &&
               toml.find("[steam]\nenabled = false") != std::string::npos &&
               toml.find("[revival]\nenabled = false") != std::string::npos,
           "Create Match TOML must preserve the selected map/mode/rules and remain private");
}

void rejects_untrusted_mode_and_rule_names() {
    battlespades::platform::LocalServerLaunchConfig config;
    config.mode = "../server";
    expect(battlespades::platform::build_local_server_toml(config, 27015U).empty(),
           "unknown mode identifiers must fail closed");
    config.mode = "tdm";
    config.rule_overrides.emplace("not_a_rule", "ON");
    expect(battlespades::platform::build_local_server_toml(config, 27015U).empty(),
           "non-retail rule keys must fail closed");
}

void serializes_the_isolated_map_creator_program() {
    using namespace battlespades::platform;
    LocalServerLaunchConfig config;
    config.program = LocalServerProgram::map_creator;
    config.server_name = "Map Creator - Castle";
    config.mode = "ugc";
    config.map_name = "Castle";
    config.bot_count = 0U;
    config.map_creator = LocalMapCreatorLaunchConfig{
        "Castle", "water", "ctf", "Castle", "KikoTs",
        std::filesystem::path{"C:/AoS/hosted_ugc"},
        std::filesystem::path{"C:/AoS/assets/original"},
    };
    const auto toml = build_local_server_toml(config, 27022U);
    expect(toml.find("default_mode = \"ugc\"") != std::string::npos &&
               toml.find("[map_creator]") != std::string::npos &&
               toml.find("project = \"Castle\"") != std::string::npos &&
               toml.find("terrain = \"water\"") != std::string::npos &&
               toml.find("target_mode = \"ctf\"") != std::string::npos &&
               toml.find("publish_root = \"C:/AoS/hosted_ugc\"") != std::string::npos,
           "Map Creator TOML must preserve project/baseplate/mode paths separately");

    config.map_creator->terrain = "../../escape";
    expect(build_local_server_toml(config, 27022U).empty(),
           "Map Creator must reject unknown baseplates before launching a process");
}

} // namespace

int main() {
    try {
        serializes_one_disposable_match_without_public_services();
        rejects_untrusted_mode_and_rule_names();
        serializes_the_isolated_map_creator_program();
        std::cout << "Local server process: disposable config and validation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Local server process failure: " << error.what() << '\n';
        return 1;
    }
}
