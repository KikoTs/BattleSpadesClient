#include "battlespades/frontend/loading_presentation.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        battlespades::frontend::BootLoadingPresentation boot_presentation;
        battlespades::frontend::MatchLoadingPresentation match_presentation;
        const battlespades::frontend::BootLoadingSnapshot boot{
            18U, 0.5, battlespades::assets::PreloadBatchState::active};
        const auto boot_list = boot_presentation.build(boot);
        if (boot_list.size() != 56U) {
            throw std::runtime_error{
                "boot screen must draw two layers, 36 dark bullets, and 18 filled bullets"};
        }

        battlespades::frontend::MatchLoadingModel model;
        model.begin("Atlantis", "CTF", false, "classic");
        model.initial_info("Atlantis", "CTF", false, "classic");
        model.receiving_map();
        model.map_progress(0.5);
        const auto snapshot = model.snapshot();
        const auto match_list = match_presentation.build(snapshot);
        if (match_list.empty() || snapshot.textures.map_image_asset.find("atlantis") ==
                                      std::string::npos) {
            throw std::runtime_error{"match screen must resolve and draw its map artwork"};
        }
        std::cout << "2/2 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
