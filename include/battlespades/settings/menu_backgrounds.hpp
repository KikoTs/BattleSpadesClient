#pragma once

#include <array>
#include <string_view>

namespace battlespades::settings {

struct MenuBackgroundOption final {
    std::string_view id, label, asset, fallback;
    double width, height;
    bool map_scene;
    double crop_x{}, crop_y{}, crop_width{}, crop_height{};
};

inline constexpr std::array menu_background_options{
    MenuBackgroundOption{"miner-and-zombies", "Miner and Zombies",
        "png/ui/menu_backgrounds/miner-and-zombies.png", "png/ui/main_splash.png", 1280.0, 960.0, false},
    MenuBackgroundOption{"original-battle-artwork", "Original Battle Artwork",
        "png/ui/menu_backgrounds/original-battle-artwork.png", "", 1251.0, 916.0, false},
    MenuBackgroundOption{"battle-builder-construction", "Battle Builder Construction",
        "png/ui/menu_backgrounds/battle-builder-construction.png", "png/ui/ugc_splash.png", 1280.0, 960.0, false},
    MenuBackgroundOption{"valentines-mafia-warehouse", "Valentines Mafia Warehouse",
        "png/ui/menu_backgrounds/valentines-mafia-warehouse.png", "", 1280.0, 960.0, false},
    MenuBackgroundOption{"matchmaking-battle", "Matchmaking Battle",
        "png/ui/menu_backgrounds/matchmaking-battle.png", "png/ui/searching/ui_searching.png", 1024.0, 512.0, false,
        136.0, 84.0, 752.0, 388.0},
    MenuBackgroundOption{"tutorial-blueprints", "Tutorial Blueprints",
        "png/ui/menu_backgrounds/tutorial-blueprints.png", "png/ui/searching/searching_tut.png", 1024.0, 512.0, false,
        136.0, 84.0, 752.0, 388.0},
    MenuBackgroundOption{"alcatraz", "Alcatraz",
        "png/ui/menu_backgrounds/alcatraz.png", "png/ui/game_loading/map_images/loading_mapimage_alcatraz.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"ancient-egypt", "Ancient Egypt",
        "png/ui/menu_backgrounds/ancient-egypt.png", "png/ui/game_loading/map_images/loading_mapimage_ancientegypt.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"arctic-base", "Arctic Base",
        "png/ui/menu_backgrounds/arctic-base.png", "png/ui/game_loading/map_images/loading_mapimage_arcticbase.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"atlantis", "Atlantis",
        "png/ui/menu_backgrounds/atlantis.png", "png/ui/game_loading/map_images/loading_mapimage_atlantis.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"block-ness", "Block Ness",
        "png/ui/menu_backgrounds/block-ness.png", "png/ui/game_loading/map_images/loading_mapimage_blockness.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"bran-castle", "Bran Castle",
        "png/ui/menu_backgrounds/bran-castle.png", "png/ui/game_loading/map_images/loading_mapimage_brancastle.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"castle-wars", "Castle Wars",
        "png/ui/menu_backgrounds/castle-wars.png", "png/ui/game_loading/map_images/loading_mapimage_castlewars.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"city-of-chicago", "City of Chicago",
        "png/ui/menu_backgrounds/city-of-chicago.png", "png/ui/game_loading/map_images/loading_mapimage_cityofchicago.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"classic", "Classic",
        "png/ui/menu_backgrounds/classic.png", "png/ui/game_loading/map_images/loading_mapimage_classic.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"crossroads", "Crossroads",
        "png/ui/menu_backgrounds/crossroads.png", "png/ui/game_loading/map_images/loading_mapimage_crossroads.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"default-blueprints", "Default Blueprints",
        "png/ui/menu_backgrounds/default-blueprints.png", "png/ui/game_loading/map_images/loading_mapimage_default.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"double-dragon", "Double Dragon",
        "png/ui/menu_backgrounds/double-dragon.png", "png/ui/game_loading/map_images/loading_mapimage_doubledragon.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"dragon-island", "Dragon Island",
        "png/ui/menu_backgrounds/dragon-island.png", "png/ui/game_loading/map_images/loading_mapimage_dragonisland.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"frontier", "Frontier",
        "png/ui/menu_backgrounds/frontier.png", "png/ui/game_loading/map_images/loading_mapimage_frontier.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"great-wall", "Great Wall",
        "png/ui/menu_backgrounds/great-wall.png", "png/ui/game_loading/map_images/loading_mapimage_greatwall.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"hiesville", "Hiesville",
        "png/ui/menu_backgrounds/hiesville.png", "png/ui/game_loading/map_images/loading_mapimage_hiesville.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"invasion", "Invasion",
        "png/ui/menu_backgrounds/invasion.png", "png/ui/game_loading/map_images/loading_mapimage_invasion.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"london", "London",
        "png/ui/menu_backgrounds/london.png", "png/ui/game_loading/map_images/loading_mapimage_london.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"lunar-base", "Lunar Base",
        "png/ui/menu_backgrounds/lunar-base.png", "png/ui/game_loading/map_images/loading_mapimage_lunarbase.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"mayan-jungle", "Mayan Jungle",
        "png/ui/menu_backgrounds/mayan-jungle.png", "png/ui/game_loading/map_images/loading_mapimage_mayanjungle.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"spooky-mansion", "Spooky Mansion",
        "png/ui/menu_backgrounds/spooky-mansion.png", "png/ui/game_loading/map_images/loading_mapimage_spookymansion.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"the-colosseum", "The Colosseum",
        "png/ui/menu_backgrounds/the-colosseum.png", "png/ui/game_loading/map_images/loading_mapimage_thecolosseum.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"tokyo-neon", "Tokyo Neon",
        "png/ui/menu_backgrounds/tokyo-neon.png", "png/ui/game_loading/map_images/loading_mapimage_tokyoneon.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"to-the-bridge", "To the Bridge",
        "png/ui/menu_backgrounds/to-the-bridge.png", "png/ui/game_loading/map_images/loading_mapimage_tothebridge.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"trenches", "Trenches",
        "png/ui/menu_backgrounds/trenches.png", "png/ui/game_loading/map_images/loading_mapimage_trenches.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"winter-valley", "Winter Valley",
        "png/ui/menu_backgrounds/winter-valley.png", "png/ui/game_loading/map_images/loading_mapimage_wintervalley.png", 1064.0, 475.0, true},
    MenuBackgroundOption{"ww1", "WW1",
        "png/ui/menu_backgrounds/ww1.png", "png/ui/game_loading/map_images/loading_mapimage_ww1.png", 1064.0, 475.0, true},
};

[[nodiscard]] constexpr const MenuBackgroundOption* menu_background_option(std::string_view id) noexcept {
    for (const auto& option : menu_background_options) {
        if (option.id == id) return &option;
    }
    return nullptr;
}

} // namespace battlespades::settings
