#include "battlespades/world/player_movement.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using namespace battlespades::world;
using Json = nlohmann::json;

Json read_fixture(const char* path) {
    std::ifstream stream{path};
    if (!stream) {
        throw std::runtime_error{std::string{"Cannot open retail fixture: "} + path};
    }
    return Json::parse(stream);
}

void check_class_profiles() {
    const auto fixture = read_fixture(AOS_RETAIL_CLASS_FIXTURE);
    if (!fixture.at("bytecode_verified").get<bool>()) {
        throw std::runtime_error{"Class fixture must retain its bytecode provenance"};
    }
    const auto& tables = fixture.at("tables");
    const std::array<std::pair<const char*, double MovementClassConfig::*>, 9> fields{{
        {"accel_multiplier", &MovementClassConfig::accel_multiplier},
        {"sprint_multiplier", &MovementClassConfig::sprint_multiplier},
        {"jump_multiplier", &MovementClassConfig::jump_multiplier},
        {"crouch_sneak_multiplier", &MovementClassConfig::crouch_sneak_multiplier},
        {"water_friction", &MovementClassConfig::water_friction},
        {"fall_on_water_damage_multiplier", &MovementClassConfig::fall_on_water_damage_multiplier},
        {"falling_damage_min_distance", &MovementClassConfig::falling_damage_min_distance},
        {"falling_damage_max_distance", &MovementClassConfig::falling_damage_max_distance},
        {"falling_damage_max_damage", &MovementClassConfig::falling_damage_max_damage},
    }};
    for (std::uint8_t class_id = 0; class_id < 18; ++class_id) {
        const auto profile = movement_config_for_class(class_id);
        const auto key = std::to_string(class_id);
        for (const auto& [name, member] : fields) {
            if (profile.*member != tables.at(name).at(key).get<double>()) {
                throw std::runtime_error{"Retail class " + key + " differs for " + name};
            }
        }
        if (profile.can_sprint_uphill !=
            tables.at("can_sprint_uphill").at(key).get<bool>()) {
            throw std::runtime_error{"Retail class " + key + " differs for can_sprint_uphill"};
        }
    }
    std::cout << "18 retail class profiles match all 10 movement fields\n";
}

void check_movement_core() {
    // Verbatim server fixture from scripts/reverse_movement_core.py, executing
    // original world.pyd at 0x10012B80 until 0x1001304A, before collision.
    // Its x87 fsqrt stub/control word and fixed basis remain documented in JSON.
    // This checks arithmetic and jump initiation, not collision, fuel or timing.
    const auto fixture = read_fixture(AOS_RETAIL_MOVEMENT_FIXTURE);
    if (fixture.at("binary_sha256") !=
        "ae45ec007e312c8d650620bc2779169f7b7461c74192b7a7480342c21237c1a0" ||
        fixture.at("entry") != "0x10012B80" ||
        fixture.at("stop_before_collision") != "0x1001304A") {
        throw std::runtime_error{"Unexpected movement fixture provenance"};
    }
    std::size_t checked = 0;
    std::size_t unreachable_hover = 0;
    std::size_t failures = 0;
    std::size_t index = 0;
    for (const auto& sample : fixture.at("cases")) {
        const auto case_index = index++;
        // The binary harness can seed states that the public input gateway
        // rejects. Only UGC pack 4 accepts hover; retain and report these vectors
        // in the copied fixture rather than silently changing their oracle.
        if (sample.value("hover", false) && sample.at("pack") != 4) {
            ++unreachable_hover;
            continue;
        }
        PlayerMovementState state{};
        state.position = {100.5, 100.5, 100.0};
        state.orientation = {1.0, 0.0, 0.0};
        const auto& velocity = sample.at("velocity");
        state.velocity = {static_cast<float>(velocity.at(0).get<double>()),
                          static_cast<float>(velocity.at(1).get<double>()),
                          static_cast<float>(velocity.at(2).get<double>())};
        state.airborne = sample.at("airborne").get<bool>();
        state.wade = sample.at("wade").get<bool>();
        state.crouch = sample.value("crouch", false);
        state.burdened = sample.value("burdened", false);
        state.jetpack = sample.at("pack").get<std::uint8_t>();
        state.jetpack_active = sample.at("active").get<bool>();
        state.jetpack_passive = sample.at("passive").get<bool>();
        state.parachute = sample.at("parachute").get<bool>();
        state.parachute_active = sample.at("parachute_active").get<bool>();
        PlayerInputState input{};
        input.forward = sample.value("up", false);
        input.backward = sample.value("down", false);
        input.left = sample.value("left", false);
        input.right = sample.value("right", false);
        input.jump = sample.value("jump", false);
        input.crouch = sample.value("crouch", false);
        input.sneak = sample.value("sneak", false);
        input.sprint = sample.value("sprint", false);
        input.hover = sample.value("hover", false);
        const auto result = step_player(state, input, nullptr, sample.at("dt").get<double>());
        const auto& expected = sample.at("expected");
        const auto& expected_velocity = expected.at("velocity");
        const std::array<double, 3> actual{state.velocity.x, state.velocity.y, state.velocity.z};
        bool matches = result.jumped == expected.at("jump_this_frame").get<bool>();
        for (std::size_t axis = 0; axis < actual.size(); ++axis) {
            matches = matches && actual[axis] == expected_velocity.at(axis).get<double>();
        }
        // The public input is immutable, so the binary's retained jump flag is
        // not exposed here. result.jumped is its separate jump_this_frame flag.
        if (!matches) {
            if (failures < 8) {
                std::cerr << std::setprecision(17) << "case " << case_index
                          << " actual velocity [" << actual[0] << ',' << actual[1]
                          << ',' << actual[2] << "] jumped=" << result.jumped
                          << " oracle=" << sample.dump() << '\n';
            }
            ++failures;
        }
        ++checked;
    }
    std::cout << checked << " reachable arithmetic vectors; " << unreachable_hover
              << " raw hover/non-UGC states excluded at the public input boundary; "
              << failures << " mismatches\n";
    if (checked != 1344 || unreachable_hover != 256 || failures != 0) {
        throw std::runtime_error{"Retail movement core differential failed"};
    }
}

} // namespace

int main() {
    try {
        check_class_profiles();
        check_movement_core();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
