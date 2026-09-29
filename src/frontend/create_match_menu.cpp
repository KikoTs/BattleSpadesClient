#include "battlespades/frontend/create_match_menu.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace battlespades::frontend {
namespace {

using StringList = std::span<const std::string_view>;

constexpr std::array<std::string_view, 7U> classic_maps{
    "Crossroads", "Hiesville", "ToTheBridge", "Trenches", "WinterValley", "WW1", "Classic"};
constexpr std::array<std::string_view, 6U> ctf_maps{
    "Atlantis", "BlockNess", "CastleWars", "DoubleDragon", "Invasion", "TokyoNeon"};
constexpr std::array<std::string_view, 9U> demolition_maps{
    "Atlantis", "BlockNess", "CastleWars", "DoubleDragon", "DragonIsland", "Frontier",
    "GreatWall", "LunarBase", "TokyoNeon"};
constexpr std::array<std::string_view, 16U> diamond_maps{
    "AncientEgypt", "ArcticBase", "Atlantis", "BlockNess", "BranCastle", "CastleWars",
    "DoubleDragon", "DragonIsland", "Frontier", "GreatWall", "London", "LunarBase",
    "MayanJungle", "SpookyMansion", "TheColosseum", "TokyoNeon"};
constexpr std::array<std::string_view, 15U> multihill_maps{
    "AncientEgypt", "Atlantis", "BlockNess", "BranCastle", "CastleWars", "DoubleDragon",
    "DragonIsland", "Frontier", "GreatWall", "Invasion", "London", "LunarBase",
    "MayanJungle", "SpookyMansion", "TheColosseum"};
constexpr std::array<std::string_view, 14U> occupation_maps{
    "AncientEgypt", "ArcticBase", "Atlantis", "BlockNess", "BranCastle", "DragonIsland",
    "Frontier", "GreatWall", "Invasion", "London", "LunarBase", "MayanJungle",
    "SpookyMansion", "TheColosseum"};
constexpr std::array<std::string_view, 2U> mafia_maps{"Alcatraz", "CityOfChicago"};
constexpr std::array<std::string_view, 16U> tdm_maps{
    "AncientEgypt", "ArcticBase", "Atlantis", "BlockNess", "CastleWars", "DoubleDragon",
    "DragonIsland", "Frontier", "GreatWall", "Invasion", "London", "LunarBase",
    "MayanJungle", "SpookyMansion", "TheColosseum", "TokyoNeon"};
constexpr std::array<std::string_view, 16U> zombie_maps{
    "AncientEgypt", "ArcticBase", "Atlantis", "BlockNess", "BranCastle", "CastleWars",
    "DoubleDragon", "DragonIsland", "Frontier", "GreatWall", "Invasion", "London",
    "MayanJungle", "SpookyMansion", "TheColosseum", "TokyoNeon"};

// The panel sorts localized names. This is the resulting English retail order.
constexpr std::array<CreateMatchModeDefinition, 10U> modes{{
    {3U, "ctf", "CTF_TITLE", CreateMatchModeFamily::standard, ctf_maps, 16U, 30U},
    {2U, "cctf", "CLASSIC_CTF_TITLE", CreateMatchModeFamily::classic, classic_maps, 32U, 90U},
    {4U, "dem", "DEMOLITION_TITLE", CreateMatchModeFamily::standard, demolition_maps, 16U, 15U},
    {5U, "dia", "DIAMOND_MINE_TITLE", CreateMatchModeFamily::standard, diamond_maps, 16U, 15U},
    {6U, "mh", "MULTIHILL_TITLE", CreateMatchModeFamily::standard, multihill_maps, 16U, 25U},
    {7U, "oc", "OCCUPATION_MODE_TITLE", CreateMatchModeFamily::standard, occupation_maps, 16U, 15U},
    {9U, "tdm", "TDM_TITLE", CreateMatchModeFamily::standard, tdm_maps, 16U, 15U},
    {8U, "tc", "TC_TITLE", CreateMatchModeFamily::mafia, mafia_maps, 16U, 25U},
    {12U, "vip", "VIP_MODE_TITLE", CreateMatchModeFamily::mafia, mafia_maps, 16U, 15U},
    {13U, "zom", "ZOMBIE_MODE_TITLE", CreateMatchModeFamily::standard, zombie_maps, 16U, 10U},
}};

constexpr std::array<std::string_view, 2U> on_off{"OFF", "ON"};
constexpr std::array<std::string_view, 13U> zero_to_sixty_by_five{
    "0", "5", "10", "15", "20", "25", "30", "35", "40", "45", "50", "55", "60"};
constexpr std::array<std::string_view, 12U> five_to_sixty_by_five{
    "5", "10", "15", "20", "25", "30", "35", "40", "45", "50", "55", "60"};
constexpr std::array<std::string_view, 11U> ten_to_sixty_by_five{
    "10", "15", "20", "25", "30", "35", "40", "45", "50", "55", "60"};
constexpr std::array<std::string_view, 10U> one_to_ten{
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
constexpr std::array<std::string_view, 6U> ten_to_sixty_by_ten{"10", "20", "30", "40", "50", "60"};
constexpr std::array<std::string_view, 17U> score_targets{
    "OFF", "5", "10", "15", "20", "25", "30", "35", "40", "45", "50", "60",
    "70", "80", "90", "100", "200"};
constexpr std::array<std::string_view, 11U> occupation_score_targets{
    "OFF", "3", "6", "9", "15", "30", "45", "60", "75", "90", "150"};
constexpr std::array<std::string_view, 4U> two_to_five{"2", "3", "4", "5"};
constexpr std::array<std::string_view, 5U> one_to_five{"1", "2", "3", "4", "5"};
constexpr std::array<std::string_view, 3U> one_to_three{"1", "2", "3"};
constexpr std::array<std::string_view, 4U> character_speeds{"50%", "100%", "150%", "200%"};
constexpr std::array<std::string_view, 3U> percentages{"50%", "100%", "200%"};
constexpr std::array<std::string_view, 12U> large_intervals{
    "30", "60", "90", "120", "180", "240", "300", "360", "420", "480", "540", "600"};
constexpr std::array<std::string_view, 13U> build_lengths{
    "OFF", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100", "110", "120"};
constexpr std::array<std::string_view, 4U> bomb_fuses{"5", "10", "15", "20"};
constexpr std::array<std::string_view, 4U> spawn_protection{"OFF", "1", "2", "3"};

constexpr std::array<std::string_view, 1U> soldier{"RULE_ENABLE_CLASS_COMMANDO"};
constexpr std::array<std::string_view, 1U> scout{"RULE_ENABLE_CLASS_MARKSMAN"};
constexpr std::array<std::string_view, 1U> miner{"RULE_ENABLE_CLASS_MINER"};
constexpr std::array<std::string_view, 1U> rocketeer{"RULE_ENABLE_CLASS_ROCKETEER"};
constexpr std::array<std::string_view, 1U> engineer{"RULE_ENABLE_CLASS_ENGINEER"};
constexpr std::array<std::string_view, 1U> specialist{"RULE_ENABLE_CLASS_SPECIALIST"};
constexpr std::array<std::string_view, 1U> medic{"RULE_ENABLE_CLASS_MEDIC"};
constexpr std::array<std::string_view, 2U> soldier_rocketeer{
    "RULE_ENABLE_CLASS_COMMANDO", "RULE_ENABLE_CLASS_ROCKETEER"};
constexpr std::array<std::string_view, 2U> soldier_scout{
    "RULE_ENABLE_CLASS_COMMANDO", "RULE_ENABLE_CLASS_MARKSMAN"};
constexpr std::array<std::string_view, 2U> engineer_rocketeer{
    "RULE_ENABLE_CLASS_ENGINEER", "RULE_ENABLE_CLASS_ROCKETEER"};
constexpr std::array<std::string_view, 3U> engineer_rocketeer_scout{
    "RULE_ENABLE_CLASS_ENGINEER", "RULE_ENABLE_CLASS_ROCKETEER", "RULE_ENABLE_CLASS_MARKSMAN"};
constexpr std::array<std::string_view, 6U> all_standard_classes{
    "RULE_ENABLE_CLASS_COMMANDO", "RULE_ENABLE_CLASS_MARKSMAN", "RULE_ENABLE_CLASS_MINER",
    "RULE_ENABLE_CLASS_ENGINEER", "RULE_ENABLE_CLASS_SPECIALIST", "RULE_ENABLE_CLASS_MEDIC"};

enum class Values : std::uint8_t {
    toggle,
    zero_to_sixty,
    five_to_sixty,
    ten_to_sixty,
    one_to_ten,
    ten_to_sixty_ten,
    score,
    occupation_score,
    two_to_five,
    one_to_five,
    one_to_three,
    speed,
    percentage,
    intervals,
    build,
    fuse,
    spawn_protection,
};

enum class FamilyMask : std::uint8_t {
    none,
    all,
    standard,
    classic,
    mafia,
    standard_mafia,
};

enum class Dependency : std::uint8_t {
    none,
    soldier,
    scout,
    miner,
    rocketeer,
    engineer,
    specialist,
    medic,
    soldier_rocketeer,
    soldier_scout,
    engineer_rocketeer,
    engineer_rocketeer_scout,
    all_standard_classes,
};

struct RuleSeed final {
    std::string_view category;
    std::string_view key;
    std::string_view default_value;
    Values values{Values::toggle};
    FamilyMask families{FamilyMask::all};
    Dependency dependency{Dependency::none};
};

[[nodiscard]] StringList values_for(Values values) noexcept {
    switch (values) {
    case Values::toggle: return on_off;
    case Values::zero_to_sixty: return zero_to_sixty_by_five;
    case Values::five_to_sixty: return five_to_sixty_by_five;
    case Values::ten_to_sixty: return ten_to_sixty_by_five;
    case Values::one_to_ten: return one_to_ten;
    case Values::ten_to_sixty_ten: return ten_to_sixty_by_ten;
    case Values::score: return score_targets;
    case Values::occupation_score: return occupation_score_targets;
    case Values::two_to_five: return two_to_five;
    case Values::one_to_five: return one_to_five;
    case Values::one_to_three: return one_to_three;
    case Values::speed: return character_speeds;
    case Values::percentage: return percentages;
    case Values::intervals: return large_intervals;
    case Values::build: return build_lengths;
    case Values::fuse: return bomb_fuses;
    case Values::spawn_protection: return spawn_protection;
    }
    return on_off;
}

[[nodiscard]] StringList dependency_for(Dependency dependency) noexcept {
    switch (dependency) {
    case Dependency::none: return {};
    case Dependency::soldier: return soldier;
    case Dependency::scout: return scout;
    case Dependency::miner: return miner;
    case Dependency::rocketeer: return rocketeer;
    case Dependency::engineer: return engineer;
    case Dependency::specialist: return specialist;
    case Dependency::medic: return medic;
    case Dependency::soldier_rocketeer: return soldier_rocketeer;
    case Dependency::soldier_scout: return soldier_scout;
    case Dependency::engineer_rocketeer: return engineer_rocketeer;
    case Dependency::engineer_rocketeer_scout: return engineer_rocketeer_scout;
    case Dependency::all_standard_classes: return all_standard_classes;
    }
    return {};
}

[[nodiscard]] constexpr std::array<bool, 3U> availability(FamilyMask families) noexcept {
    switch (families) {
    case FamilyMask::none: return {false, false, false};
    case FamilyMask::all: return {true, true, true};
    case FamilyMask::standard: return {true, false, false};
    case FamilyMask::classic: return {false, true, false};
    case FamilyMask::mafia: return {false, false, true};
    case FamilyMask::standard_mafia: return {true, false, true};
    }
    return {false, false, false};
}

constexpr std::array<RuleSeed, 20U> general_rules{{
    {"GENERAL", "RULE_ENABLE_BLOCKS", "ON"},
    {"GENERAL", "RULE_ENABLE_FLARE_BLOCKS", "ON", Values::toggle, FamilyMask::standard_mafia, Dependency::all_standard_classes},
    {"GENERAL", "RULE_ENABLE_PREFABS", "ON", Values::toggle, FamilyMask::standard_mafia, Dependency::all_standard_classes},
    {"GENERAL", "RULE_ONE_HIT_KILL", "OFF"},
    {"GENERAL", "RULE_ENABLE_GRAVESTONES", "ON"},
    {"GENERAL", "RULE_ENABLE_CORPSE_EXPLOSION", "ON"},
    {"GENERAL", "RULE_ENABLE_SNIPER_BEAM", "ON", Values::toggle, FamilyMask::standard, Dependency::scout},
    {"GENERAL", "RULE_ENABLE_DEATH_CAM", "ON"},
    {"GENERAL", "RULE_ENABLE_MINI_MAP", "ON"},
    {"GENERAL", "RULE_ENABLE_SPECTATORS", "ON"},
    {"GENERAL", "RULE_ENABLE_FALL_ON_WATER_DAMAGE", "ON"},
    {"GENERAL", "RULE_RESPAWN_TIMES", "10", Values::zero_to_sixty},
    {"GENERAL", "RULE_BLOCK_HEALTH", "100%", Values::percentage},
    {"GENERAL", "RULE_WEAPON_DAMAGE", "100%", Values::percentage},
    {"GENERAL", "RULE_SPAWN_PROTECTION_TIME", "3", Values::spawn_protection},
    {"GENERAL", "RULE_CHARACTER_BLOCK_WALLETS", "100%", Values::percentage},
    {"GENERAL", "RULE_CHARACTER_SPEED", "100%", Values::speed},
    {"GENERAL", "RULE_POINTS_FROM_TEABAGGING", "OFF"},
    {"GENERAL", "RULE_CRATES_SPAWN_TIME", "25", Values::ten_to_sixty},
    {"GENERAL", "RULE_ENABLE_COLOUR_PICKER", "ON"},
}};

constexpr std::array<RuleSeed, 7U> class_rules{{
    {"CLASSES", "RULE_ENABLE_CLASS_COMMANDO", "ON", Values::toggle, FamilyMask::standard},
    {"CLASSES", "RULE_ENABLE_CLASS_MARKSMAN", "ON", Values::toggle, FamilyMask::standard},
    {"CLASSES", "RULE_ENABLE_CLASS_MINER", "ON", Values::toggle, FamilyMask::standard},
    {"CLASSES", "RULE_ENABLE_CLASS_ENGINEER", "ON", Values::toggle, FamilyMask::standard},
    {"CLASSES", "RULE_ENABLE_CLASS_ROCKETEER", "ON", Values::toggle, FamilyMask::none},
    {"CLASSES", "RULE_ENABLE_CLASS_SPECIALIST", "ON", Values::toggle, FamilyMask::standard},
    {"CLASSES", "RULE_ENABLE_CLASS_MEDIC", "ON", Values::toggle, FamilyMask::standard},
}};

constexpr std::array<RuleSeed, 27U> weapon_rules{{
    {"WEAPONS", "RULE_ENABLE_WEAPON_KNIFE", "ON", Values::toggle, FamilyMask::standard, Dependency::soldier_scout},
    {"WEAPONS", "RULE_ENABLE_WEAPON_MINIGUN", "ON", Values::toggle, FamilyMask::standard, Dependency::soldier},
    {"WEAPONS", "RULE_ENABLE_WEAPON_RPG", "ON", Values::toggle, FamilyMask::standard, Dependency::soldier},
    {"WEAPONS", "RULE_ENABLE_WEAPON_TRIPLE_BARREL_RPG", "ON", Values::toggle, FamilyMask::standard, Dependency::soldier},
    {"WEAPONS", "RULE_ENABLE_WEAPON_PISTOL", "ON", Values::toggle, FamilyMask::standard, Dependency::scout},
    {"WEAPONS", "RULE_ENABLE_WEAPON_SNIPER_RIFLE", "ON", Values::toggle, FamilyMask::standard, Dependency::scout},
    {"WEAPONS", "RULE_ENABLE_WEAPON_SNIPER_RIFLE2", "ON", Values::toggle, FamilyMask::standard, Dependency::scout},
    {"WEAPONS", "RULE_ENABLE_WEAPON_RIFLE", "ON", Values::toggle, FamilyMask::classic},
    {"WEAPONS", "RULE_ENABLE_WEAPON_DOUBLE_BARREL_SHOTGUN", "ON", Values::toggle, FamilyMask::standard, Dependency::miner},
    {"WEAPONS", "RULE_ENABLE_WEAPON_PUMP_ACTION_SHOTGUN", "ON", Values::toggle, FamilyMask::standard, Dependency::miner},
    {"WEAPONS", "RULE_ENABLE_WEAPON_SMG", "ON", Values::toggle, FamilyMask::standard, Dependency::engineer_rocketeer},
    {"WEAPONS", "RULE_ENABLE_WEAPON_CLASSIC_SHOTGUN", "OFF", Values::toggle, FamilyMask::classic},
    {"WEAPONS", "RULE_ENABLE_WEAPON_CLASSIC_SMG", "OFF", Values::toggle, FamilyMask::classic},
    {"WEAPONS", "RULE_ENABLE_WEAPON_TOMMYGUN", "ON", Values::toggle, FamilyMask::mafia},
    {"WEAPONS", "RULE_ENABLE_WEAPON_SNUB_PISTOL", "ON", Values::toggle, FamilyMask::mafia},
    {"WEAPONS", "RULE_ENABLE_WEAPON_CROWBAR", "ON", Values::toggle, FamilyMask::mafia},
    {"WEAPONS", "RULE_ENABLE_WEAPON_MOLOTOV", "ON", Values::toggle, FamilyMask::mafia},
    {"WEAPONS", "RULE_ENABLE_WEAPON_RIOTSTICK", "ON", Values::toggle, FamilyMask::standard, Dependency::medic},
    {"WEAPONS", "RULE_ENABLE_WEAPON_MACHETE", "ON", Values::toggle, FamilyMask::standard, Dependency::specialist},
    {"WEAPONS", "RULE_ENABLE_WEAPON_AUTOPISTOL", "ON", Values::toggle, FamilyMask::standard, Dependency::specialist},
    {"WEAPONS", "RULE_ENABLE_WEAPON_GRENADE_LAUNCHER", "ON", Values::toggle, FamilyMask::standard, Dependency::specialist},
    {"WEAPONS", "RULE_ENABLE_WEAPON_STICKY_GRENADE", "ON", Values::toggle, FamilyMask::standard, Dependency::specialist},
    {"WEAPONS", "RULE_ENABLE_WEAPON_MINE_LAUNCHER", "ON", Values::toggle, FamilyMask::standard, Dependency::engineer},
    {"WEAPONS", "RULE_ENABLE_WEAPON_ASSAULTRIFLE", "ON", Values::toggle, FamilyMask::standard, Dependency::soldier},
    {"WEAPONS", "RULE_ENABLE_WEAPON_LIGHTMACHINEGUN", "ON", Values::toggle, FamilyMask::standard, Dependency::medic},
    {"WEAPONS", "RULE_ENABLE_WEAPON_AUTOSHOTGUN", "ON", Values::toggle, FamilyMask::standard, Dependency::specialist},
    {"WEAPONS", "RULE_ENABLE_WEAPON_BLOCKSUCKER", "ON", Values::toggle, FamilyMask::standard, Dependency::miner},
}};

constexpr std::array<RuleSeed, 20U> equipment_rules{{
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_CLASSIC_SPADE", "ON", Values::toggle, FamilyMask::classic},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_CLASSIC_GRENADE", "ON", Values::toggle, FamilyMask::classic},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_SPADE", "ON", Values::toggle, FamilyMask::standard, Dependency::soldier_rocketeer},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_GRENADE", "ON", Values::toggle, FamilyMask::standard, Dependency::soldier_rocketeer},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_ANTIPERSONNEL_GRENADE", "ON", Values::toggle, FamilyMask::standard, Dependency::soldier},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_SNOWBLOWER", "ON", Values::toggle, FamilyMask::standard, Dependency::engineer},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_PICKAXE", "ON", Values::toggle, FamilyMask::standard, Dependency::engineer_rocketeer_scout},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_LANDMINE", "ON", Values::toggle, FamilyMask::standard, Dependency::scout},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_ROCKET_TURRET", "ON", Values::toggle, FamilyMask::standard, Dependency::engineer_rocketeer},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_GLIDE_JETPACK", "ON", Values::toggle, FamilyMask::none, Dependency::rocketeer},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_JUMP_JETPACK", "ON", Values::toggle, FamilyMask::none, Dependency::rocketeer},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_JETPACK", "ON", Values::toggle, FamilyMask::standard, Dependency::engineer},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_SUPER_SPADE", "ON", Values::toggle, FamilyMask::standard, Dependency::miner},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_DRILL_CANNON", "ON", Values::toggle, FamilyMask::standard, Dependency::miner},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_DYNAMITE", "ON", Values::toggle, FamilyMask::standard, Dependency::miner},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_MEDPACK", "ON", Values::toggle, FamilyMask::standard, Dependency::medic},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_CHEMICALBOMB", "ON", Values::toggle, FamilyMask::standard, Dependency::specialist},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_RADAR_STATION", "ON", Values::toggle, FamilyMask::standard, Dependency::scout},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_C4", "ON", Values::toggle, FamilyMask::standard, Dependency::miner},
    {"EQUIPMENT", "RULE_ENABLE_EQUIPMENT_DISGUISE", "ON", Values::toggle, FamilyMask::standard, Dependency::engineer},
}};

constexpr std::array<RuleSeed, 23U> mode_rules{{
    {"tdm", "RULE_TDM_SCORE_TARGET", "200", Values::score},
    {"zom", "RULE_ZOMBIE_NOOF_ROUNDS", "3", Values::one_to_five},
    {"zom", "RULE_NOOF_FIRST_INFECTED_ZOMBIES", "2", Values::one_to_five},
    {"zom", "RULE_CLASS_SPEED", "100%", Values::percentage},
    {"zom", "RULE_ZOMBIE_CLASS_DAMAGE", "100%", Values::percentage},
    {"vip", "RULE_VIP_NOOF_ROUNDS", "3", Values::one_to_five},
    {"vip", "RULE_VIP_HEALTH", "100%", Values::percentage},
    {"vip", "RULE_ENABLE_SUDDEN_DEATH", "ON"},
    {"ctf", "RULE_CTF_ENABLE_SHOOT_WITH_INTEL", "OFF"},
    {"ctf", "RULE_CTF_ENABLE_INTEL_RETURN_ON_TOUCH", "OFF"},
    {"ctf", "RULE_CTF_SCORE_TARGET", "5", Values::one_to_ten},
    {"ctf", "RULE_CTF_ENABLE_INTEL_AUTO_RETURN", "ON"},
    {"mh", "RULE_MULTIHILL_MAX_ACTIVE_BASES", "1", Values::one_to_five},
    {"mh", "RULE_BASE_ACTIVE_TIME", "240", Values::intervals},
    {"tc", "RULE_TC_MAX_ACTIVE_BASES", "5", Values::two_to_five},
    {"tc", "RULE_CAPTURE_RATE", "100%", Values::percentage},
    {"dia", "RULE_DIAMOND_MAX_ACTIVE_BASES", "1", Values::one_to_five},
    {"dia", "RULE_DIA_SCORE_TARGET", "15", Values::five_to_sixty},
    {"dia", "RULE_MAX_ACTIVE_DIAMONDS", "2", Values::one_to_five},
    {"dia", "RULE_DIAMOND_LIFETIME", "60", Values::ten_to_sixty_ten},
    {"dem", "RULE_BUILD_STATE_LENGTH", "30", Values::build},
    {"oc", "RULE_OCC_SCORE_TARGET", "30", Values::occupation_score},
    {"oc", "RULE_MAX_ACTIVE_BOMBS", "1", Values::one_to_three},
}};

constexpr RuleSeed occupation_fuse{
    "oc", "RULE_BOMB_FUSE_TIME", "10", Values::fuse};

template <std::size_t Size>
void append_rule_seeds(std::vector<CreateMatchRuleDefinition>& output,
                       const std::array<RuleSeed, Size>& seeds) {
    output.reserve(output.size() + seeds.size());
    for (const auto& seed : seeds) {
        const auto flags = availability(seed.families);
        output.push_back(CreateMatchRuleDefinition{
            seed.category,
            seed.key,
            seed.default_value,
            values_for(seed.values),
            flags[0],
            flags[1],
            flags[2],
            dependency_for(seed.dependency),
        });
    }
}

[[nodiscard]] const std::vector<CreateMatchRuleDefinition>& rules() noexcept {
    static const auto catalog = [] {
        std::vector<CreateMatchRuleDefinition> result;
        result.reserve(general_rules.size() + class_rules.size() + weapon_rules.size() +
                       equipment_rules.size() + mode_rules.size() + 1U);
        append_rule_seeds(result, general_rules);
        append_rule_seeds(result, class_rules);
        append_rule_seeds(result, weapon_rules);
        append_rule_seeds(result, equipment_rules);
        append_rule_seeds(result, mode_rules);
        append_rule_seeds(result, std::array{occupation_fuse});
        return result;
    }();
    return catalog;
}

constexpr std::array<std::uint16_t, 12U> player_counts{
    2U, 4U, 6U, 8U, 10U, 12U, 14U, 16U, 18U, 20U, 22U, 24U};
constexpr std::array<std::uint16_t, 13U> match_lengths{
    5U, 10U, 15U, 20U, 25U, 30U, 35U, 40U, 45U, 50U, 55U, 60U, 90U};
constexpr std::array<std::uint16_t, 13U> bot_counts{
    0U, 2U, 4U, 6U, 8U, 10U, 12U, 14U, 16U, 18U, 20U, 22U, 24U};
constexpr std::array<std::string_view, 4U> bot_difficulties{
    "casual", "normal", "hard", "mixed"};
constexpr std::array<std::uint16_t, 4U> server_ports{
    27015U, 32887U, 38886U, 40000U};
constexpr std::array<std::string_view, 3U> privacy_values{"INVITE", "FRIENDS", "OPEN"};
constexpr std::array<std::string_view, 9U> setting_keys{
    "PRIVACY", "PLAYLIST", "MAX_PLAYERS", "MATCH_LENGTH",
    "MAP_ROTATION_FILENAME", "GAME_RULES", "BOTS", "BOT_DIFFICULTY", "SERVER_PORT"};

[[nodiscard]] const CreateMatchModeDefinition* mode_by_id(std::uint16_t id) noexcept {
    const auto found = std::find_if(modes.begin(), modes.end(), [id](const auto& mode) {
        return mode.retail_playlist_id == id;
    });
    return found == modes.end() ? nullptr : &*found;
}

[[nodiscard]] bool contains(StringList list, std::string_view value) noexcept {
    return std::find(list.begin(), list.end(), value) != list.end();
}

[[nodiscard]] bool has_mode(const std::vector<std::string>& modes_list,
                            std::string_view mode) noexcept {
    return std::find(modes_list.begin(), modes_list.end(), mode) != modes_list.end();
}

template <typename Range, typename Value>
[[nodiscard]] std::size_t index_of(const Range& range, const Value& value) noexcept {
    const auto found = std::find(range.begin(), range.end(), value);
    return found == range.end() ? 0U : static_cast<std::size_t>(std::distance(range.begin(), found));
}

[[nodiscard]] bool valid_utf8_with_code_point_limit(std::string_view value,
                                                     std::size_t maximum) noexcept {
    if (value.empty()) {
        return false;
    }
    std::size_t count{};
    for (std::size_t index{}; index < value.size();) {
        const auto lead = static_cast<unsigned char>(value[index]);
        std::size_t width{};
        if (lead <= 0x7FU) width = 1U;
        else if (lead >= 0xC2U && lead <= 0xDFU) width = 2U;
        else if (lead >= 0xE0U && lead <= 0xEFU) width = 3U;
        else if (lead >= 0xF0U && lead <= 0xF4U) width = 4U;
        else return false;
        if (index + width > value.size()) return false;
        for (std::size_t continuation = 1U; continuation < width; ++continuation) {
            const auto byte = static_cast<unsigned char>(value[index + continuation]);
            if ((byte & 0xC0U) != 0x80U) return false;
        }
        if (++count > maximum) return false;
        index += width;
    }
    return true;
}

[[nodiscard]] std::string to_string(std::uint16_t value) {
    return std::to_string(static_cast<unsigned int>(value));
}

[[nodiscard]] CreateMatchVisualState state_for(std::string_view key,
                                               bool enabled,
                                               const std::optional<std::string>& focused,
                                               const std::optional<std::string>& hovered) noexcept {
    if (!enabled) return CreateMatchVisualState::disabled;
    if (hovered && *hovered == key) return CreateMatchVisualState::hovered;
    if (focused && *focused == key) return CreateMatchVisualState::focused;
    return CreateMatchVisualState::normal;
}

[[nodiscard]] bool page_can_open(CreateMatchPage page) noexcept {
    switch (page) {
    case CreateMatchPage::match_settings:
    case CreateMatchPage::choose_game_mode:
    case CreateMatchPage::choose_map:
    case CreateMatchPage::game_rules:
        return true;
    }
    return false;
}

} // namespace

std::span<const CreateMatchModeDefinition> retail_create_match_modes() noexcept {
    return modes;
}

std::span<const CreateMatchRuleDefinition> retail_create_match_rules() noexcept {
    return rules();
}

CreateMatchMenuModel::CreateMatchMenuModel(CreateMatchConfiguration initial)
    : configuration_(std::move(initial)) {
    const auto* mode = mode_by_id(configuration_.retail_playlist_id);
    if (mode == nullptr) {
        mode = mode_by_id(9U);
        configuration_.retail_playlist_id = mode->retail_playlist_id;
    }
    // A fresh model has no custom catalog; set_custom_maps() re-admits one.
    if (configuration_.custom_map || !contains(mode->maps, configuration_.map_name)) {
        select_stock_map(*mode);
    }
    if (std::find(player_counts.begin(), player_counts.end(), configuration_.max_players) ==
        player_counts.end()) {
        configuration_.max_players = 12U;
    }
    if (std::find(match_lengths.begin(), match_lengths.end(), configuration_.match_minutes) ==
        match_lengths.end()) {
        configuration_.match_minutes = mode->default_match_minutes;
    }
    if (std::find(bot_counts.begin(), bot_counts.end(), configuration_.bot_count) ==
            bot_counts.end() ||
        configuration_.bot_count >= configuration_.max_players) {
        configuration_.bot_count = 0U;
    }
    if (!contains(bot_difficulties, configuration_.bot_difficulty)) {
        configuration_.bot_difficulty = "mixed";
    }
    if (std::find(server_ports.begin(), server_ports.end(), configuration_.server_port) ==
        server_ports.end()) {
        configuration_.server_port = 27015U;
    }

    for (auto iterator = configuration_.rule_overrides.begin();
         iterator != configuration_.rule_overrides.end();) {
        const auto definition = std::find_if(rules().begin(), rules().end(), [&](const auto& rule) {
            return rule.rule_key == iterator->first;
        });
        if (definition == rules().end() || !contains(definition->values, iterator->second)) {
            iterator = configuration_.rule_overrides.erase(iterator);
        } else {
            ++iterator;
        }
    }

    retail_defaults_ = configuration_;
    retail_defaults_.privacy = CreateMatchPrivacy::open;
    retail_defaults_.retail_playlist_id = 9U;
    retail_defaults_.max_players = 12U;
    retail_defaults_.match_minutes = 15U;
    retail_defaults_.bot_count = 0U;
    retail_defaults_.bot_difficulty = "mixed";
    retail_defaults_.server_port = 27015U;
    retail_defaults_.map_name = "AncientEgypt";
    retail_defaults_.custom_map = false;
    retail_defaults_.subscribed_map = false;
    retail_defaults_.map_title.clear();
    retail_defaults_.map_author.clear();
    retail_defaults_.rule_overrides.clear();
    for (const auto& category : std::array<std::string_view, 14U>{
             "tdm", "zom", "vip", "ctf", "cctf", "mh", "tc", "dia", "dem", "oc",
             "GENERAL", "CLASSES", "WEAPONS", "EQUIPMENT"}) {
        expanded_categories_.emplace(std::string{category}, true);
    }
    repair_focus();
}

std::string create_match_custom_map_key(const CreateMatchCustomMap& map) {
    return std::string{map.subscribed ? "SUBSCRIBED_MAPS/" : "SAVED_MAPS/"} + map.stem;
}

void CreateMatchMenuModel::set_custom_maps(std::vector<CreateMatchCustomMap> maps) {
    constexpr std::size_t maximum_custom_maps{4'096U};
    if (maps.size() > maximum_custom_maps) maps.resize(maximum_custom_maps);
    std::erase_if(maps, [](const CreateMatchCustomMap& map) {
        return map.stem.empty() || map.stem.size() > 64U ||
               map.stem.find_first_of("/\\:. ") != std::string::npos;
    });
    for (auto& map : maps) {
        if (map.title.empty()) map.title = map.stem;
        for (auto& mode : map.mode_keys) {
            std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
        }
    }
    // Retail sorts neither list; title order keeps the rows stable.
    std::stable_sort(maps.begin(), maps.end(), [](const auto& left, const auto& right) {
        return left.title < right.title;
    });
    custom_maps_ = std::move(maps);
    if (configuration_.custom_map && !custom_map_valid(selected_mode())) {
        select_stock_map(selected_mode());
        emit_configuration_changed();
    }
    clamp_scroll();
    repair_focus(false);
}

std::span<const CreateMatchCustomMap> CreateMatchMenuModel::custom_maps() const noexcept {
    return custom_maps_;
}

const CreateMatchCustomMap* CreateMatchMenuModel::listed_custom_map(
    std::string_view row_key) const {
    // mapsPanel: A2450[game_modes[0]] in get_available_game_modes(map); Classic
    // CTF shares MODE_CTF, so a CTF-valid map also hosts cctf.
    auto mode_key = std::string{selected_mode().mode_key};
    if (mode_key == "cctf") mode_key = "ctf";
    const auto found = std::find_if(custom_maps_.begin(), custom_maps_.end(), [&](const auto& map) {
        return create_match_custom_map_key(map) == row_key;
    });
    if (found == custom_maps_.end() || !has_mode(found->mode_keys, mode_key)) return nullptr;
    return &*found;
}

bool CreateMatchMenuModel::custom_map_valid(const CreateMatchModeDefinition& mode) const {
    if (!configuration_.custom_map) return false;
    auto mode_key = std::string{mode.mode_key};
    if (mode_key == "cctf") mode_key = "ctf";
    return std::any_of(custom_maps_.begin(), custom_maps_.end(), [&](const auto& map) {
        return map.stem == configuration_.map_name &&
               map.subscribed == configuration_.subscribed_map && has_mode(map.mode_keys, mode_key);
    });
}

void CreateMatchMenuModel::select_stock_map(const CreateMatchModeDefinition& mode) {
    configuration_.custom_map = false;
    configuration_.subscribed_map = false;
    configuration_.map_title.clear();
    configuration_.map_author.clear();
    if (!contains(mode.maps, configuration_.map_name)) {
        configuration_.map_name = std::string{*std::min_element(mode.maps.begin(), mode.maps.end())};
    }
}

CreateMatchPage CreateMatchMenuModel::page() const noexcept { return page_; }

const CreateMatchConfiguration& CreateMatchMenuModel::configuration() const noexcept {
    return configuration_;
}

const std::string& CreateMatchMenuModel::lobby_name() const noexcept { return lobby_name_; }

bool CreateMatchMenuModel::host_authority() const noexcept { return host_authority_; }

void CreateMatchMenuModel::apply_authoritative_configuration(
    CreateMatchConfiguration configuration) {
    // The lobby pump can deliver the same snapshot every frame. It must not
    // turn a background refresh into keyboard navigation after a wheel/drag.
    if(configuration==configuration_)return;
    // Lobby metadata carries only the file stem; re-admit it when it names a
    // listed custom map so an owner's SAVED/SUBSCRIBED choice survives echo.
    const auto incoming_map = configuration.map_name;
    const auto keep_custom = !configuration.custom_map && configuration_.custom_map &&
                             incoming_map == configuration_.map_name;
    auto custom = configuration_;
    CreateMatchMenuModel normalized{std::move(configuration)};
    configuration_ = normalized.configuration_;
    if (keep_custom) {
        const auto previous_map = configuration_.map_name;
        configuration_.map_name = custom.map_name;
        configuration_.custom_map = true;
        configuration_.subscribed_map = custom.subscribed_map;
        configuration_.map_title = custom.map_title;
        configuration_.map_author = custom.map_author;
        if (!custom_map_valid(selected_mode())) {
            configuration_.map_name = previous_map;
            select_stock_map(selected_mode());
        }
    }
    retail_defaults_ = normalized.retail_defaults_;
    clamp_scroll();
    repair_focus(false);
}

void CreateMatchMenuModel::set_host_authority(bool host) noexcept {
    host_authority_ = host;
}

void CreateMatchMenuModel::set_match_join_available(bool available, bool busy) noexcept {
    match_join_available_ = available;
    match_join_busy_ = available && busy;
}

void CreateMatchMenuModel::set_players(std::vector<CreateMatchPlayer> players) {
    players_ = std::move(players);
    first_visible_player_ = std::min(first_visible_player_,
        players_.empty() ? 0U : ((players_.size() - 1U) / 8U) * 8U);
    // Retail fills the editable header with PLAYER_SQUAD.format(leader_name)
    // as soon as the owner appears in the newly-created lobby.
    if (lobby_name_ == "Private Match") {
        const auto owner = std::find_if(players_.begin(), players_.end(), [](const auto& player) {
            return player.host;
        });
        if (owner != players_.end() && !owner->display_name.empty()) {
            lobby_name_ = owner->display_name + "'s Lobby";
        }
    }
}

void CreateMatchMenuModel::set_chat_lines(std::vector<CreateMatchChatLine> lines) {
    constexpr std::size_t maximum_chat_history{32U};
    for (auto& line : lines) {
        line.author = line.author.empty() ? std::string{"Player"} :
                                            std::string{line.author};
        line.author = std::string{line.author.begin(), line.author.end()};
        line.message = std::string{line.message.begin(), line.message.end()};
    }
    std::erase_if(lines, [](const CreateMatchChatLine& line) {
        return line.message.empty() ||
               !valid_utf8_with_code_point_limit(line.author, 32U) ||
               !valid_utf8_with_code_point_limit(line.message, 160U);
    });
    if (lines.size() > maximum_chat_history) {
        lines.erase(lines.begin(), lines.end() -
                                      static_cast<std::ptrdiff_t>(maximum_chat_history));
    }
    chat_lines_ = std::move(lines);
}

bool CreateMatchMenuModel::append_chat_text(std::string_view utf8) {
    if (!chat_focused_ || utf8.empty()) return false;
    auto next = chat_draft_;
    next.append(utf8);
    if (!valid_utf8_with_code_point_limit(next, 160U)) return false;
    if (std::ranges::any_of(next, [](char value) {
            const auto byte = static_cast<unsigned char>(value);
            return byte < 0x20U && byte != '\t';
        })) {
        return false;
    }
    chat_draft_ = std::move(next);
    return true;
}

bool CreateMatchMenuModel::erase_chat_code_point() noexcept {
    if (!chat_focused_ || chat_draft_.empty()) return false;
    auto index = chat_draft_.size() - 1U;
    while (index > 0U &&
           (static_cast<unsigned char>(chat_draft_[index]) & 0xC0U) == 0x80U) {
        --index;
    }
    chat_draft_.erase(index);
    return true;
}

bool CreateMatchMenuModel::submit_chat() {
    if (!chat_focused_) return false;
    const auto first = chat_draft_.find_first_not_of(" \t");
    if (first == std::string::npos) return false;
    const auto last = chat_draft_.find_last_not_of(" \t");
    auto message = chat_draft_.substr(first, last - first + 1U);
    if (!valid_utf8_with_code_point_limit(message, 160U)) return false;
    effects_.push_back(CreateMatchChatEffect{std::move(message)});
    chat_draft_.clear();
    return true;
}

void CreateMatchMenuModel::cancel_chat() noexcept {
    chat_focused_ = false;
    chat_draft_.clear();
}

bool CreateMatchMenuModel::chat_focused() const noexcept { return chat_focused_; }

bool CreateMatchMenuModel::set_lobby_name(std::string value) {
    if (!valid_utf8_with_code_point_limit(value, 19U) || value == lobby_name_) {
        return false;
    }
    lobby_name_ = std::move(value);
    return true;
}

const CreateMatchModeDefinition& CreateMatchMenuModel::selected_mode() const noexcept {
    if (const auto* mode = mode_by_id(configuration_.retail_playlist_id)) return *mode;
    return *mode_by_id(9U);
}

std::string CreateMatchMenuModel::playlist_default_value(
    const CreateMatchRuleDefinition& rule) const {
    if (selected_mode().mode_key == "zom" && rule.rule_key == "RULE_CRATES_SPAWN_TIME") {
        return "60";
    }
    if (selected_mode().mode_key == "cctf") {
        if (rule.rule_key == "RULE_CTF_ENABLE_SHOOT_WITH_INTEL") return "ON";
        if (rule.rule_key == "RULE_CTF_ENABLE_INTEL_AUTO_RETURN") return "OFF";
    }
    // The two classic weapon defaults are already OFF in the shared table.
    return std::string{rule.default_value};
}

std::string CreateMatchMenuModel::resolved_rule_value(
    const CreateMatchRuleDefinition& rule) const {
    if (const auto found = configuration_.rule_overrides.find(rule.rule_key);
        found != configuration_.rule_overrides.end()) {
        return found->second;
    }
    return playlist_default_value(rule);
}

bool CreateMatchMenuModel::rule_available(const CreateMatchRuleDefinition& rule) const {
    const auto family = selected_mode().family;
    const auto family_available = family == CreateMatchModeFamily::standard
                                      ? rule.available_standard
                                  : family == CreateMatchModeFamily::classic
                                      ? rule.available_classic
                                      : rule.available_mafia;
    if (!family_available) return false;
    const auto category = rule.category_key;
    const auto mode_category = selected_mode().mode_key == "cctf"
                                   ? std::string_view{"ctf"}
                                   : selected_mode().mode_key;
    return category == "GENERAL" || category == "CLASSES" || category == "WEAPONS" ||
           category == "EQUIPMENT" || category == mode_category;
}

bool CreateMatchMenuModel::rule_enabled(const CreateMatchRuleDefinition& rule) const {
    if (!rule_available(rule)) return false;
    if (selected_mode().family != CreateMatchModeFamily::standard ||
        rule.enabling_class_rules.empty()) {
        return true;
    }
    return std::any_of(rule.enabling_class_rules.begin(),
                       rule.enabling_class_rules.end(),
                       [&](std::string_view class_rule) {
                           const auto definition = std::find_if(
                               rules().begin(), rules().end(), [&](const auto& candidate) {
                                   return candidate.rule_key == class_rule;
                               });
                           return definition != rules().end() && rule_available(*definition) &&
                                  resolved_rule_value(*definition) == "ON";
                       });
}

std::vector<std::string> CreateMatchMenuModel::expanded_row_keys() const {
    if (page_ == CreateMatchPage::match_settings) {
        return {setting_keys.begin(), setting_keys.end()};
    }
    if (page_ == CreateMatchPage::choose_game_mode) {
        std::vector<std::string> output;
        output.reserve(modes.size());
        for (const auto& mode : modes) output.emplace_back(mode.mode_key);
        return output;
    }
    if (page_ == CreateMatchPage::choose_map) {
        std::vector<std::string> output;
        // Retail packs order: SAVED_MAPS, SUBSCRIBED_MAPS, then the stock
        // pack; an empty custom category is not shown at all.
        for (const auto subscribed : {false, true}) {
            std::vector<std::string> rows;
            for (const auto& map : custom_maps_) {
                if (map.subscribed != subscribed) continue;
                auto key = create_match_custom_map_key(map);
                if (listed_custom_map(key) != nullptr) rows.push_back(std::move(key));
            }
            if (rows.empty()) continue;
            const std::string category{subscribed ? "SUBSCRIBED_MAPS" : "SAVED_MAPS"};
            output.push_back(category);
            const auto state = expanded_categories_.find(category);
            if (state == expanded_categories_.end() || state->second) {
                for (auto& row : rows) output.push_back(std::move(row));
            }
        }
        output.emplace_back(selected_mode().family == CreateMatchModeFamily::classic
                                ? "A2362"
                            : selected_mode().family == CreateMatchModeFamily::mafia
                                ? "MAFIA_PACK"
                                : "STANDARD");
        const auto expanded = expanded_categories_.find(output.back());
        if (expanded == expanded_categories_.end() || expanded->second) {
            for (const auto map : selected_mode().maps) output.emplace_back(map);
        }
        return output;
    }

    std::vector<std::string> output;
    const std::array category_order{
        selected_mode().mode_key, std::string_view{"GENERAL"}, std::string_view{"CLASSES"},
        std::string_view{"WEAPONS"}, std::string_view{"EQUIPMENT"}};
    for (const auto category : category_order) {
        const auto source_category = category == "cctf" ? std::string_view{"ctf"} : category;
        const auto has_rows = std::any_of(rules().begin(), rules().end(), [&](const auto& rule) {
            return rule.category_key == source_category && rule_available(rule);
        });
        if (!has_rows) continue;
        output.emplace_back(category);
        const auto state = expanded_categories_.find(category);
        if (state != expanded_categories_.end() && !state->second) continue;
        for (const auto& rule : rules()) {
            if (rule.category_key == source_category && rule_available(rule)) {
                output.emplace_back(rule.rule_key);
            }
        }
    }
    return output;
}

std::size_t CreateMatchMenuModel::visible_capacity() const noexcept {
    // ListPanelBase has 285 px below a 40 px header in the 355 px panel.
    // GameRulesPanel is deliberately 30 px shorter; Match Settings reserves
    // its own Defaults strip and uses 32 px rows with 2 px spacing.
    switch (page_) {
    case CreateMatchPage::match_settings: return 6U;
    case CreateMatchPage::choose_game_mode:
    case CreateMatchPage::choose_map: return 10U;
    case CreateMatchPage::game_rules: return 9U;
    }
    return 6U;
}

std::size_t CreateMatchMenuModel::maximum_scroll() const {
    const auto size = expanded_row_keys().size();
    return size > visible_capacity() ? size - visible_capacity() : 0U;
}

void CreateMatchMenuModel::clamp_scroll() {
    first_visible_row_ = std::min(first_visible_row_, maximum_scroll());
}

void CreateMatchMenuModel::reveal_focus() {
    if (!focused_key_) return;
    const auto keys = expanded_row_keys();
    const auto found = std::find(keys.begin(), keys.end(), *focused_key_);
    if (found == keys.end()) return;
    const auto index = static_cast<std::size_t>(std::distance(keys.begin(), found));
    if (index < first_visible_row_) first_visible_row_ = index;
    if (index >= first_visible_row_ + visible_capacity()) {
        first_visible_row_ = index - visible_capacity() + 1U;
    }
    clamp_scroll();
}

void CreateMatchMenuModel::repair_focus(bool reveal) {
    const auto keys = expanded_row_keys();
    if (keys.empty()) {
        focused_key_.reset();
        return;
    }
    if (!focused_key_ || std::find(keys.begin(), keys.end(), *focused_key_) == keys.end()) {
        const auto frame=presentation();
        const bool valid_button=focused_key_&&std::ranges::any_of(frame.buttons,[&](const auto& button){
            return button.enabled&&button.stable_key==*focused_key_;
        });
        if(!valid_button)focused_key_=keys[reveal?0U:std::min(first_visible_row_,keys.size()-1U)];
    }
    // Explicit keyboard navigation reveals focus. Server updates only repair
    // removed controls and clamp shorter lists, retaining the user's viewport.
    if(reveal)reveal_focus();
}

void CreateMatchMenuModel::emit_configuration_changed() {
    effects_.push_back(CreateMatchConfigurationChangedEffect{configuration_});
}

void CreateMatchMenuModel::select_mode(const CreateMatchModeDefinition& mode) {
    const auto old_mode = selected_mode();
    const auto old_default_length = old_mode.default_match_minutes;
    configuration_.retail_playlist_id = mode.retail_playlist_id;
    if (configuration_.custom_map ? !custom_map_valid(mode)
                                  : !contains(mode.maps, configuration_.map_name)) {
        select_stock_map(mode);
    }
    if (configuration_.match_minutes == old_default_length) {
        configuration_.match_minutes = mode.default_match_minutes;
    }
    for (auto iterator = configuration_.rule_overrides.begin();
         iterator != configuration_.rule_overrides.end();) {
        const auto definition = std::find_if(rules().begin(), rules().end(), [&](const auto& rule) {
            return rule.rule_key == iterator->first;
        });
        if (definition == rules().end() || !rule_available(*definition)) {
            iterator = configuration_.rule_overrides.erase(iterator);
        } else {
            ++iterator;
        }
    }
    emit_configuration_changed();
}

bool CreateMatchMenuModel::open_page(CreateMatchPage next) {
    if (!page_can_open(next) || next == page_) return false;
    const auto from = page_;
    previous_page_ = page_;
    page_ = next;
    first_visible_row_ = 0U;
    focused_key_.reset();
    hovered_key_.reset();
    repair_focus();
    effects_.push_back(CreateMatchSoundEffect{CreateMatchSound::confirm});
    effects_.push_back(CreateMatchPageChangedEffect{
        from, next, next == CreateMatchPage::match_settings
                        ? CreateMatchNavigationDirection::backward
                        : CreateMatchNavigationDirection::forward});
    return true;
}

bool CreateMatchMenuModel::activate_back() {
    effects_.push_back(CreateMatchSoundEffect{CreateMatchSound::back});
    if (page_ != CreateMatchPage::match_settings) {
        const auto from = page_;
        page_ = CreateMatchPage::match_settings;
        previous_page_ = from;
        first_visible_row_ = 0U;
        focused_key_.reset();
        hovered_key_.reset();
        repair_focus();
        effects_.push_back(CreateMatchPageChangedEffect{
            from, page_, CreateMatchNavigationDirection::backward});
        return true;
    }
    effects_.push_back(CreateMatchRouteEffect{
        CreateMatchRouteAction::leave_lobby, configuration_});
    return true;
}

bool CreateMatchMenuModel::activate_done() {
    if (page_ != CreateMatchPage::match_settings) {
        // Retail calls this Confirm and returns to the previous lobby panel.
        return activate_back();
    }
    if (match_join_busy_ || (!host_authority_ && !match_join_available_)) return false;
    effects_.push_back(CreateMatchSoundEffect{CreateMatchSound::confirm});
    effects_.push_back(CreateMatchRouteEffect{
        match_join_available_ ? CreateMatchRouteAction::join_game
                              : CreateMatchRouteAction::start_game, configuration_});
    return true;
}

void CreateMatchMenuModel::activate_defaults() {
    if (!host_authority_) return;
    if (page_ == CreateMatchPage::match_settings) {
        configuration_ = retail_defaults_;
    } else if (page_ == CreateMatchPage::game_rules) {
        configuration_.rule_overrides.clear();
    } else {
        return;
    }
    clamp_scroll();
    repair_focus();
    effects_.push_back(CreateMatchSoundEffect{CreateMatchSound::confirm});
    emit_configuration_changed();
}

bool CreateMatchMenuModel::activate_local_game() {
    if (page_ != CreateMatchPage::match_settings || !host_authority_ ||
        match_join_available_ || players_.size() > 1U)
        return false;
    effects_.push_back(CreateMatchSoundEffect{CreateMatchSound::confirm});
    effects_.push_back(CreateMatchRouteEffect{
        CreateMatchRouteAction::start_local_game, configuration_});
    return true;
}

bool CreateMatchMenuModel::set_focus(std::string_view stable_key) {
    auto keys = expanded_row_keys();
    for (const auto& button : presentation().buttons) {
        if (button.enabled) keys.push_back(button.stable_key);
    }
    if (std::find(keys.begin(), keys.end(), stable_key) == keys.end()) return false;
    focused_key_ = std::string{stable_key};
    reveal_focus();
    return true;
}

bool CreateMatchMenuModel::activate_focused() {
    if (focused_key_) {
        for (const auto& button : presentation().buttons) {
            if (button.enabled && button.stable_key == *focused_key_) {
                const ui::Point center{button.bounds.x + button.bounds.width / 2,
                                       button.bounds.y + button.bounds.height / 2};
                pointer_press(center);
                pointer_release(center);
                return true;
            }
        }
    }
    return focused_key_.has_value() && activate_row(*focused_key_);
}

bool CreateMatchMenuModel::activate_row(std::string_view key) {
    if (page_ == CreateMatchPage::match_settings) {
        if (key == "PLAYLIST") return open_page(CreateMatchPage::choose_game_mode);
        if (key == "MAP_ROTATION_FILENAME") return open_page(CreateMatchPage::choose_map);
        if (key == "GAME_RULES") return open_page(CreateMatchPage::game_rules);
        return adjust_row(key, 1);
    }
    if (page_ == CreateMatchPage::choose_game_mode) {
        if (!host_authority_) return false;
        const auto found = std::find_if(modes.begin(), modes.end(), [&](const auto& mode) {
            return mode.mode_key == key;
        });
        if (found == modes.end()) return false;
        if (found->retail_playlist_id != configuration_.retail_playlist_id) select_mode(*found);
        effects_.push_back(CreateMatchSoundEffect{CreateMatchSound::confirm});
        return true;
    }
    if (page_ == CreateMatchPage::choose_map) {
        const auto family_key = selected_mode().family == CreateMatchModeFamily::classic
                                    ? std::string_view{"A2362"}
                                : selected_mode().family == CreateMatchModeFamily::mafia
                                    ? std::string_view{"MAFIA_PACK"}
                                    : std::string_view{"STANDARD"};
        if (key == family_key || key == "SAVED_MAPS" || key == "SUBSCRIBED_MAPS") {
            const auto expanded = expanded_categories_.find(key);
            return set_category_expanded(key,
                                         expanded == expanded_categories_.end() || !expanded->second);
        }
        if (!host_authority_) return false;
        if (const auto* custom = listed_custom_map(key); custom != nullptr) {
            // mapsPanel.on_row_selected: Custom_UGC_Map + its author, and the
            // saved/subscribed file becomes hosted_ugc_map_filename.
            if (!configuration_.custom_map || configuration_.map_name != custom->stem ||
                configuration_.subscribed_map != custom->subscribed) {
                configuration_.custom_map = true;
                configuration_.subscribed_map = custom->subscribed;
                configuration_.map_name = custom->stem;
                configuration_.map_title = custom->title;
                configuration_.map_author = custom->author;
                emit_configuration_changed();
            }
            effects_.push_back(CreateMatchSoundEffect{CreateMatchSound::confirm});
            return true;
        }
        if (!contains(selected_mode().maps, key)) return false;
        if (configuration_.custom_map || configuration_.map_name != key) {
            configuration_.custom_map = false;
            configuration_.subscribed_map = false;
            configuration_.map_title.clear();
            configuration_.map_author.clear();
            configuration_.map_name = std::string{key};
            emit_configuration_changed();
        }
        effects_.push_back(CreateMatchSoundEffect{CreateMatchSound::confirm});
        return true;
    }

    const auto category = std::find_if(expanded_categories_.begin(),
                                       expanded_categories_.end(),
                                       [&](const auto& item) { return item.first == key; });
    if (category != expanded_categories_.end()) {
        return set_category_expanded(key, !category->second);
    }
    if (!host_authority_) return false;
    const auto definition = std::find_if(rules().begin(), rules().end(), [&](const auto& rule) {
        return rule.rule_key == key;
    });
    if (definition == rules().end() || !rule_enabled(*definition)) return false;
    if (definition->values.size() == 2U && contains(definition->values, "ON") &&
        contains(definition->values, "OFF")) {
        return set_rule_value(key, resolved_rule_value(*definition) == "ON" ? "OFF" : "ON");
    }
    return adjust_row(key, 1);
}

bool CreateMatchMenuModel::adjust_row(std::string_view key, std::int32_t direction) {
    if (direction == 0 || !host_authority_) return false;
    const auto step_index = [direction](std::size_t index, std::size_t count) {
        if (count == 0U) return std::size_t{};
        if (direction > 0) return std::min(index + 1U, count - 1U);
        return index == 0U ? std::size_t{} : index - 1U;
    };

    if (page_ == CreateMatchPage::match_settings) {
        if (key == "PRIVACY") {
            const auto index = static_cast<std::size_t>(configuration_.privacy);
            const auto next = step_index(index, 3U);
            if (next == index) return false;
            configuration_.privacy = static_cast<CreateMatchPrivacy>(next);
        } else if (key == "MAX_PLAYERS") {
            const auto index = index_of(player_counts, configuration_.max_players);
            const auto next = step_index(index, player_counts.size());
            if (next == index) return false;
            configuration_.max_players = player_counts[next];
            if (configuration_.bot_count >= configuration_.max_players) {
                const auto maximum_bots =
                    static_cast<std::uint16_t>(configuration_.max_players - 1U);
                const auto valid = std::find_if(
                    bot_counts.rbegin(), bot_counts.rend(),
                    [maximum_bots](std::uint16_t count) { return count <= maximum_bots; });
                configuration_.bot_count =
                    valid == bot_counts.rend() ? 0U : *valid;
            }
        } else if (key == "MATCH_LENGTH") {
            const auto index = index_of(match_lengths, configuration_.match_minutes);
            const auto next = step_index(index, match_lengths.size());
            if (next == index) return false;
            configuration_.match_minutes = match_lengths[next];
        } else if (key == "BOTS") {
            std::vector<std::uint16_t> available;
            std::copy_if(bot_counts.begin(), bot_counts.end(),
                         std::back_inserter(available),
                         [this](std::uint16_t count) {
                             return count < configuration_.max_players;
                         });
            const auto index = index_of(available, configuration_.bot_count);
            const auto next = step_index(index, available.size());
            if (next == index) return false;
            configuration_.bot_count = available[next];
        } else if (key == "BOT_DIFFICULTY") {
            const auto index = index_of(bot_difficulties, configuration_.bot_difficulty);
            const auto next = step_index(index, bot_difficulties.size());
            if (next == index) return false;
            configuration_.bot_difficulty = std::string{bot_difficulties[next]};
        } else if (key == "SERVER_PORT") {
            const auto index = index_of(server_ports, configuration_.server_port);
            const auto next = step_index(index, server_ports.size());
            if (next == index) return false;
            configuration_.server_port = server_ports[next];
        } else {
            return false;
        }
        effects_.push_back(CreateMatchSoundEffect{CreateMatchSound::scroll});
        emit_configuration_changed();
        return true;
    }

    if (page_ != CreateMatchPage::game_rules) return false;
    const auto definition = std::find_if(rules().begin(), rules().end(), [&](const auto& rule) {
        return rule.rule_key == key;
    });
    if (definition == rules().end() || !rule_enabled(*definition)) return false;
    const auto current = resolved_rule_value(*definition);
    const auto index = index_of(definition->values, current);
    return set_rule_value(key, definition->values[step_index(index, definition->values.size())]);
}

bool CreateMatchMenuModel::set_rule_value(std::string_view key, std::string_view value) {
    if (!host_authority_) return false;
    const auto definition = std::find_if(rules().begin(), rules().end(), [&](const auto& rule) {
        return rule.rule_key == key;
    });
    if (definition == rules().end() || !rule_enabled(*definition) ||
        !contains(definition->values, value)) {
        return false;
    }
    if (resolved_rule_value(*definition) == value) return false;
    if (value == playlist_default_value(*definition)) {
        if (const auto override = configuration_.rule_overrides.find(key);
            override != configuration_.rule_overrides.end()) {
            configuration_.rule_overrides.erase(override);
        }
    } else {
        configuration_.rule_overrides.insert_or_assign(std::string{key}, std::string{value});
    }
    effects_.push_back(CreateMatchSoundEffect{CreateMatchSound::scroll});
    emit_configuration_changed();
    return true;
}

bool CreateMatchMenuModel::set_category_expanded(std::string_view category_key, bool expanded) {
    const auto known = expanded_categories_.find(category_key);
    const auto is_map_category = category_key == "STANDARD" || category_key == "A2362" ||
                                 category_key == "MAFIA_PACK" || category_key == "SAVED_MAPS" ||
                                 category_key == "SUBSCRIBED_MAPS";
    if (known == expanded_categories_.end() && !is_map_category) return false;
    expanded_categories_.insert_or_assign(std::string{category_key}, expanded);
    clamp_scroll();
    repair_focus();
    effects_.push_back(CreateMatchSoundEffect{CreateMatchSound::confirm});
    return true;
}

bool CreateMatchMenuModel::set_scroll(std::size_t first_visible_row) {
    const auto clamped = std::min(first_visible_row, maximum_scroll());
    if (clamped == first_visible_row_) return false;
    first_visible_row_ = clamped;
    effects_.push_back(CreateMatchSoundEffect{CreateMatchSound::scroll});
    return true;
}

bool CreateMatchMenuModel::mouse_wheel(std::int32_t vertical_steps) {
    if (vertical_steps == 0) return false;
    const auto current = static_cast<std::int64_t>(first_visible_row_);
    const auto requested = current - static_cast<std::int64_t>(vertical_steps);
    const auto clamped = static_cast<std::size_t>(
        std::clamp<std::int64_t>(requested, 0, static_cast<std::int64_t>(maximum_scroll())));
    return set_scroll(clamped);
}

void CreateMatchMenuModel::set_member_management_enabled(bool enabled) noexcept {
    member_management_enabled_ = enabled;
}

bool CreateMatchMenuModel::request_member_action(std::uint64_t account_id, bool kick) {
    if (!host_authority_ || !member_management_enabled_) return false;
    const auto member = std::ranges::find(players_, account_id, &CreateMatchPlayer::account_id);
    if (member == players_.end() || member->in_game || (kick && member->host)) return false;
    const std::uint8_t next_team = member->team_key == "TEAM1_COLOR" ? 3U
                                 : member->team_key == "TEAM2_COLOR" ? 0U : 2U;
    effects_.push_back(CreateMatchMemberEffect{account_id, kick, next_team});
    return true;
}

bool CreateMatchMenuModel::handle(ui::InputEvent event) {
    if (!event.triggers_action()) return false;
    if (event.action == ui::InputAction::cancel && chat_focused_) {
        cancel_chat();
        return true;
    }
    if (event.action == ui::InputAction::cancel) return activate_back();
    if (event.action == ui::InputAction::activate) return activate_focused();
    if (event.action == ui::InputAction::navigate_left) {
        return focused_key_ && adjust_row(*focused_key_, -1);
    }
    if (event.action == ui::InputAction::navigate_right) {
        return focused_key_ && adjust_row(*focused_key_, 1);
    }
    if (event.action != ui::InputAction::navigate_up &&
        event.action != ui::InputAction::navigate_down &&
        event.action != ui::InputAction::focus_next &&
        event.action != ui::InputAction::focus_previous) {
        return false;
    }
    auto keys = expanded_row_keys();
    for (const auto& button : presentation().buttons) {
        if (button.enabled) keys.push_back(button.stable_key);
    }
    if (keys.empty()) return false;
    const auto current = focused_key_
                             ? std::find(keys.begin(), keys.end(), *focused_key_)
                             : keys.end();
    auto index = current == keys.end()
                     ? std::size_t{}
                     : static_cast<std::size_t>(std::distance(keys.begin(), current));
    const auto forward = event.action == ui::InputAction::navigate_down ||
                         event.action == ui::InputAction::focus_next;
    index = forward ? (index + 1U) % keys.size() : (index == 0U ? keys.size() - 1U : index - 1U);
    focused_key_ = keys[index];
    reveal_focus();
    return true;
}

std::optional<CreateMatchRowPresentation>
CreateMatchMenuModel::row_with_key(std::string_view stable_key) const {
    const auto frame = presentation();
    const auto found = std::find_if(frame.rows.begin(), frame.rows.end(), [&](const auto& row) {
        return row.stable_key == stable_key;
    });
    return found == frame.rows.end() ? std::nullopt
                                     : std::optional<CreateMatchRowPresentation>{*found};
}

void CreateMatchMenuModel::pointer_move(std::optional<ui::Point> point) {
    hovered_key_.reset();
    if (!point) return;
    if (scrollbar_dragging_) {
        scroll_from_pointer(*point);
        return;
    }
    const auto frame = presentation();
    for (const auto& row : frame.rows) {
        const auto whole_row = row.kind == CreateMatchRowKind::category ||
                               row.kind == CreateMatchRowKind::selectable_item;
        if ((whole_row && row.bounds.contains(*point)) ||
            (!whole_row && row.control_bounds.contains(*point))) {
            hovered_key_ = row.stable_key;
            return;
        }
    }
    for (const auto& button : frame.buttons) {
        if (button.bounds.contains(*point)) {
            hovered_key_ = button.stable_key;
            return;
        }
    }
}

void CreateMatchMenuModel::scroll_from_pointer(ui::Point point) {
    const auto frame = presentation();
    if (!frame.show_scrollbar || frame.maximum_scroll == 0U) return;
    constexpr std::int32_t arrow_height{22};
    constexpr std::int32_t track_padding{2};
    const auto track_top = frame.scrollbar_bounds.y + arrow_height + track_padding;
    const auto track_bottom = frame.scrollbar_bounds.y + frame.scrollbar_bounds.height -
                              arrow_height - track_padding;
    const auto clamped_y = std::clamp(point.y, track_top, track_bottom);
    const auto span = std::max(1, track_bottom - track_top);
    const auto ratio = static_cast<double>(clamped_y - track_top) /
                       static_cast<double>(span);
    static_cast<void>(set_scroll(static_cast<std::size_t>(
        std::lround(ratio * static_cast<double>(frame.maximum_scroll)))));
}

void CreateMatchMenuModel::pointer_press(std::optional<ui::Point> point) {
    scrollbar_dragging_ = false;
    pressed_member_key_.reset();
    if (!point) return;
    const auto frame = presentation();
    for (const auto& button : frame.buttons) {
        if ((button.stable_key.starts_with("TEAM:") || button.stable_key.starts_with("KICK:"))
            && button.enabled && button.bounds.contains(*point)) {
            pressed_member_key_ = button.stable_key;
            return;
        }
    }
    if (!frame.show_scrollbar || !frame.scrollbar_bounds.contains(*point)) return;
    constexpr std::int32_t arrow_height{22};
    const ui::Rect up{frame.scrollbar_bounds.x,
                      frame.scrollbar_bounds.y,
                      frame.scrollbar_bounds.width,
                      arrow_height};
    const ui::Rect down{frame.scrollbar_bounds.x,
                        frame.scrollbar_bounds.y + frame.scrollbar_bounds.height - arrow_height,
                        frame.scrollbar_bounds.width,
                        arrow_height};
    if (up.contains(*point)) {
        if (first_visible_row_ > 0U) static_cast<void>(set_scroll(first_visible_row_ - 1U));
        return;
    }
    if (down.contains(*point)) {
        static_cast<void>(set_scroll(first_visible_row_ + 1U));
        return;
    }
    scrollbar_dragging_ = true;
    scroll_from_pointer(*point);
}

void CreateMatchMenuModel::pointer_release(ui::Point point) {
    if (scrollbar_dragging_) {
        scroll_from_pointer(point);
        scrollbar_dragging_ = false;
        return;
    }
    const auto frame = presentation();
    if (frame.chat_input.contains(point)) {
        chat_focused_ = true;
        focused_key_.reset();
        return;
    }
    chat_focused_ = false;
    for (const auto& row : frame.rows) {
        if (!row.enabled) continue;
        if (row.kind == CreateMatchRowKind::category ||
            row.kind == CreateMatchRowKind::selectable_item) {
            if (!row.bounds.contains(point)) continue;
            focused_key_ = row.stable_key;
            static_cast<void>(activate_row(row.stable_key));
            return;
        }
        if (!row.control_bounds.contains(point)) continue;
        focused_key_ = row.stable_key;
        if (row.kind == CreateMatchRowKind::toggle) {
            static_cast<void>(activate_row(row.stable_key));
            return;
        }
        constexpr std::int32_t inset{4};
        const auto control_size = row.control_bounds.height - inset * 2;
        const ui::Rect left{row.control_bounds.x + inset,
                            row.control_bounds.y + inset,
                            control_size,
                            control_size};
        const ui::Rect right{row.control_bounds.x + row.control_bounds.width - inset - control_size,
                             row.control_bounds.y + inset,
                             control_size,
                             control_size};
        if (row.kind == CreateMatchRowKind::menu_link) {
            if (right.contains(point)) static_cast<void>(activate_row(row.stable_key));
            return;
        }
        if (left.contains(point)) static_cast<void>(adjust_row(row.stable_key, -1));
        else if (right.contains(point)) static_cast<void>(adjust_row(row.stable_key, 1));
        return;
    }
    for (const auto& button : frame.buttons) {
        if (!button.enabled || !button.bounds.contains(point)) continue;
        if (button.stable_key.starts_with("TEAM:") || button.stable_key.starts_with("KICK:")) {
            if (pressed_member_key_ != button.stable_key) return;
            pressed_member_key_.reset();
            for (const auto& member : players_) {
                if (button.stable_key == "TEAM:" + std::to_string(member.account_id) ||
                    button.stable_key == "KICK:" + std::to_string(member.account_id)) {
                    static_cast<void>(request_member_action(member.account_id,
                        button.stable_key.starts_with("KICK:")));
                    break;
                }
            }
        } else if (button.stable_key == "PLAYERS_PREV") {
            first_visible_player_ = first_visible_player_ >= 8U ? first_visible_player_ - 8U : 0U;
        } else if (button.stable_key == "PLAYERS_NEXT") {
            if (first_visible_player_ + 8U < players_.size()) first_visible_player_ += 8U;
        } else if (button.stable_key == "DEFAULTS") activate_defaults();
        else if (button.stable_key == "CONFIRM" || button.stable_key == "START_GAME" ||
                 button.stable_key == "JOIN_GAME") {
            static_cast<void>(activate_done());
        } else if (button.stable_key == "START_LOCAL") {
            static_cast<void>(activate_local_game());
        } else if (button.stable_key == "BACK") {
            static_cast<void>(activate_back());
        } else if (button.stable_key == "INVITE") {
            effects_.push_back(CreateMatchSoundEffect{CreateMatchSound::confirm});
            effects_.push_back(CreateMatchPlatformActionEffect{
                CreateMatchPlatformAction::invite_friends});
        }
        return;
    }
}

CreateMatchMenuPresentation CreateMatchMenuModel::presentation() const {
    CreateMatchMenuPresentation frame;
    frame.page = page_;
    frame.panel_title_key = create_match_page_title(page_);
    frame.lobby_name = lobby_name_;
    frame.players = players_;
    frame.first_visible_player = first_visible_player_;
    frame.member_management = host_authority_ && member_management_enabled_;
    frame.chat_lines = chat_lines_;
    frame.chat_draft = chat_draft_;
    frame.chat_focused = chat_focused_;
    frame.first_visible_row = first_visible_row_;
    frame.maximum_scroll = maximum_scroll();
    frame.show_scrollbar = frame.maximum_scroll > 0U;
    frame.show_content_frame = page_ == CreateMatchPage::choose_map ||
                               page_ == CreateMatchPage::game_rules;
    frame.show_defaults_help = page_ == CreateMatchPage::match_settings ||
                               page_ == CreateMatchPage::game_rules;
    if (page_ == CreateMatchPage::game_rules) {
        frame.content_panel.height = 325;
        frame.scrollbar_bounds.height = 255;
    }
    frame.dirty = configuration_ != retail_defaults_;
    frame.focused_key = focused_key_;
    frame.hovered_key = hovered_key_;

    const auto keys = expanded_row_keys();
    const auto end = std::min(keys.size(), first_visible_row_ + visible_capacity());
    const auto row_width = frame.show_scrollbar ? 288 : 320;
    std::int32_t y = 155;
    for (auto index = first_visible_row_; index < end; ++index) {
        const auto& key = keys[index];
        CreateMatchRowPresentation row;
        row.stable_key = key;
        row.label_key = key;
        row.enabled = host_authority_;
        row.selected = false;
        row.expanded = false;

        if (page_ == CreateMatchPage::match_settings) {
            row.kind = key == "PLAYLIST" || key == "MAP_ROTATION_FILENAME" || key == "GAME_RULES"
                           ? CreateMatchRowKind::menu_link
                           : CreateMatchRowKind::stepped_choice;
            // Members may inspect nested Mode/Map/Rules panels, but only the
            // authoritative owner can change the values inside them.
            if (row.kind == CreateMatchRowKind::menu_link) row.enabled = true;
            if (key == "PRIVACY") {
                row.value_index = static_cast<std::size_t>(configuration_.privacy);
                row.value_count = privacy_values.size();
                row.value_text = std::string{privacy_values[row.value_index]};
            } else if (key == "PLAYLIST") {
                row.label_key = "MODE";
                row.value_text = std::string{selected_mode().label_key};
            } else if (key == "MAX_PLAYERS") {
                row.value_index = index_of(player_counts, configuration_.max_players);
                row.value_count = player_counts.size();
                row.value_text = to_string(configuration_.max_players);
            } else if (key == "MATCH_LENGTH") {
                row.value_index = index_of(match_lengths, configuration_.match_minutes);
                row.value_count = match_lengths.size();
                row.value_text = to_string(configuration_.match_minutes);
            } else if (key == "BOTS") {
                row.value_index = index_of(bot_counts, configuration_.bot_count);
                row.value_count = static_cast<std::size_t>(std::count_if(
                    bot_counts.begin(), bot_counts.end(),
                    [this](std::uint16_t count) {
                        return count < configuration_.max_players;
                    }));
                row.value_text = to_string(configuration_.bot_count);
            } else if (key == "BOT_DIFFICULTY") {
                row.value_index =
                    index_of(bot_difficulties, configuration_.bot_difficulty);
                row.value_count = bot_difficulties.size();
                row.value_text = configuration_.bot_difficulty;
            } else if (key == "SERVER_PORT") {
                row.value_index = index_of(server_ports, configuration_.server_port);
                row.value_count = server_ports.size();
                row.value_text = to_string(configuration_.server_port);
            } else if (key == "MAP_ROTATION_FILENAME") {
                row.label_key = "MAP";
                row.value_text = configuration_.custom_map && !configuration_.map_title.empty()
                                     ? configuration_.map_title
                                     : configuration_.map_name;
            } else if (key == "GAME_RULES") {
                row.value_text = configuration_.rule_overrides.empty() ? "DEFAULT" : "DEFINED";
            }
        } else if (page_ == CreateMatchPage::choose_game_mode) {
            const auto mode = std::find_if(modes.begin(), modes.end(), [&](const auto& item) {
                return item.mode_key == key;
            });
            row.kind = CreateMatchRowKind::selectable_item;
            row.label_key = mode == modes.end() ? key : std::string{mode->label_key};
            row.selected = mode != modes.end() &&
                           mode->retail_playlist_id == configuration_.retail_playlist_id;
        } else if (page_ == CreateMatchPage::choose_map) {
            const auto category = key == "STANDARD" || key == "A2362" || key == "MAFIA_PACK" ||
                                  key == "SAVED_MAPS" || key == "SUBSCRIBED_MAPS";
            row.kind = category ? CreateMatchRowKind::category
                                : CreateMatchRowKind::selectable_item;
            if (category) {
                row.enabled = true;
                const auto state = expanded_categories_.find(key);
                row.expanded = state == expanded_categories_.end() || state->second;
            } else if (const auto* custom = listed_custom_map(key); custom != nullptr) {
                // OwnableItemBase(custom_map=True, author=...): title + author.
                row.label_key = custom->title;
                row.value_text = custom->author;
                row.selected = configuration_.custom_map &&
                               configuration_.subscribed_map == custom->subscribed &&
                               configuration_.map_name == custom->stem;
            } else {
                row.selected = !configuration_.custom_map && configuration_.map_name == key;
            }
        } else {
            const auto definition = std::find_if(rules().begin(), rules().end(), [&](const auto& item) {
                return item.rule_key == key;
            });
            if (definition == rules().end()) {
                row.kind = CreateMatchRowKind::category;
                row.enabled = true;
                const auto state = expanded_categories_.find(key);
                row.expanded = state == expanded_categories_.end() || state->second;
                if (key == selected_mode().mode_key) row.label_key = std::string{selected_mode().label_key};
            } else {
                const auto value = resolved_rule_value(*definition);
                row.value_text = value;
                row.value_count = definition->values.size();
                row.value_index = index_of(definition->values, value);
                row.kind = definition->values.size() == 2U && contains(definition->values, "ON") &&
                                   contains(definition->values, "OFF")
                               ? CreateMatchRowKind::toggle
                               : CreateMatchRowKind::stepped_choice;
                row.enabled = host_authority_ && rule_enabled(*definition);
                row.selected = value == "ON";
            }
        }

        const auto height = page_ == CreateMatchPage::match_settings ? 32 : 26;
        row.bounds = ui::Rect{410, y, row_width, height};
        if (row.kind == CreateMatchRowKind::category ||
            row.kind == CreateMatchRowKind::selectable_item) {
            row.control_bounds = row.bounds;
        } else if (page_ == CreateMatchPage::match_settings) {
            // MatchSettingsListItem: 1/3 label plus three 14 px text paddings.
            const auto label_width = row_width / 3;
            row.control_bounds = ui::Rect{410 + 14 + label_width + 14,
                                          y + 4,
                                          row_width - label_width - 42,
                                          height - 8};
        } else if (row.kind == CreateMatchRowKind::toggle) {
            // GameRulesToggleListItem places a 2/3-row-height checkbox 14 px
            // from the right edge, without a surrounding value bar.
            const auto size = height * 2 / 3;
            row.control_bounds = ui::Rect{410 + row_width - 14 - size,
                                          y + (height - size) / 2,
                                          size,
                                          size};
        } else {
            // GameRulesSliderListItem gives roughly two thirds to its label.
            const auto spacing = row_width / 30;
            const auto name_width = row_width * 2 / 3 - spacing * 2;
            row.control_bounds = ui::Rect{410 + name_width + height / 10 - 7,
                                          y + height / 10,
                                          row_width - name_width - spacing,
                                          height - height / 5};
        }
        row.visual_state = state_for(row.stable_key,
                                     row.enabled,
                                     focused_key_,
                                     hovered_key_);
        frame.rows.push_back(std::move(row));
        y += height + (page_ == CreateMatchPage::match_settings ? 2 : 0);
    }

    if (page_ == CreateMatchPage::match_settings || page_ == CreateMatchPage::game_rules) {
        frame.buttons.push_back(CreateMatchButtonPresentation{
            "DEFAULTS",
            "DEFAULTS",
            ui::Rect{410, 415, 80, 30},
            host_authority_,
            state_for("DEFAULTS", host_authority_, focused_key_, hovered_key_)});
    }
    const auto nested = page_ != CreateMatchPage::match_settings;
    const auto local_option = !nested && host_authority_ && !match_join_available_ && players_.size() <= 1U;
    if (local_option) {
        frame.buttons.push_back(CreateMatchButtonPresentation{
            "START_LOCAL", "LOCAL MATCH", ui::Rect{405, 456, 162, 50}, true,
            state_for("START_LOCAL", true, focused_key_, hovered_key_)});
    }
    const auto action_key = nested ? "CONFIRM" : match_join_available_ ? "JOIN_GAME" : "START_GAME";
    const auto action_enabled = nested || (!match_join_busy_ && (host_authority_ || match_join_available_));
    frame.buttons.push_back(CreateMatchButtonPresentation{
        action_key,
        match_join_available_ && !nested ? "JOIN GAME" : action_key,
        local_option ? ui::Rect{575, 456, 162, 50} : ui::Rect{405, 456, 332, 50},
        action_enabled,
        state_for(action_key, action_enabled, focused_key_, hovered_key_)});
    if (players_.size() > 8U) {
        frame.buttons.push_back({"PLAYERS_PREV", "<", {186, 367, 30, 20},
                                 first_visible_player_ > 0U});
        frame.buttons.push_back({"PLAYERS_NEXT", ">", {246, 367, 30, 20},
                                 first_visible_player_ + 8U < players_.size()});
    }
    if (frame.member_management) {
        const auto member_end = std::min(players_.size(), first_visible_player_ + 8U);
        for (auto index = first_visible_player_; index < member_end; ++index) {
            const auto& member = players_[index];
            const auto member_y = 157 + static_cast<std::int32_t>(index - first_visible_player_) * 25;
            frame.buttons.push_back({"TEAM:" + std::to_string(member.account_id),
                member.team_key, {277, member_y, 100, 21}, !member.in_game});
            if (!member.host && !member.in_game) {
                frame.buttons.push_back({"KICK:" + std::to_string(member.account_id),
                    "Kick", {229, member_y, 43, 21}, true});
            }
        }
    }
    const auto invite_enabled = players_.size() < configuration_.max_players;
    frame.buttons.push_back(CreateMatchButtonPresentation{
        "INVITE",
        "INVITE",
        ui::Rect{300, 109, 80, 30},
        invite_enabled,
        state_for("INVITE", invite_enabled, focused_key_, hovered_key_)});
    frame.buttons.push_back(CreateMatchButtonPresentation{
        "BACK",
        nested ? "BACK" : "LEAVE_LOBBY",
        ui::Rect{54, 541, 190, 32},
        true,
        state_for("BACK", true, focused_key_, hovered_key_)});
    return frame;
}

std::vector<CreateMatchEffect> CreateMatchMenuModel::take_effects() noexcept {
    auto output = std::move(effects_);
    effects_.clear();
    return output;
}

std::string_view create_match_page_title(CreateMatchPage page) noexcept {
    switch (page) {
    case CreateMatchPage::match_settings: return "MATCH_SETTINGS";
    case CreateMatchPage::choose_game_mode: return "CHOOSE_GAME_MODE";
    case CreateMatchPage::choose_map: return "MAPS";
    case CreateMatchPage::game_rules: return "RULES";
    }
    return "MATCH_SETTINGS";
}

} // namespace battlespades::frontend
