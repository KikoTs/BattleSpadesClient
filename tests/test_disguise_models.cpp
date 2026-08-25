#include "battlespades/world/disguise_models.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

} // namespace

int main() {
    try {
        const std::filesystem::path assets{AOS_TEST_ASSET_ROOT};
        const battlespades::world::VxlColor environment{42U, 137U, 81U, 255U};
        const auto loaded = battlespades::world::load_disguise_models(
            assets, environment);
        expect(static_cast<bool>(loaded), "retail disguise models must load");
        expect(!loaded.models->standing.empty() &&
                   !loaded.models->crouching.empty(),
               "both disguise stances must have geometry");
        expect(loaded.models->standing.maximum[0U] -
                       loaded.models->standing.minimum[0U] <
                   3.0F,
               "retail 0.1 disguise scale must be baked into the world mesh");
        expect(loaded.models->crouching.maximum[2U] -
                       loaded.models->crouching.minimum[2U] <
                   loaded.models->standing.maximum[2U] -
                       loaded.models->standing.minimum[2U],
               "crouched disguise must be shorter than standing disguise");
        expect(std::abs(battlespades::world::disguise_ground_offset(false) -
                            0.8F) < 0.0001F &&
                   std::abs(battlespades::world::disguise_ground_offset(true) -
                            0.4F) < 0.0001F,
               "retail disguise offsets must be converted to map Z-down");
        const auto exact_environment_abgr =
            (static_cast<std::uint32_t>(environment.blue) << 16U) |
            (static_cast<std::uint32_t>(environment.green) << 8U) |
            static_cast<std::uint32_t>(environment.red);
        expect(std::ranges::any_of(
                   loaded.models->standing.vertices,
                   [exact_environment_abgr](const auto& vertex) {
                       return vertex.abgr == exact_environment_abgr;
                   }),
               "disguise blocks must carry the selected environment colour");
        std::cout << "Disguise model tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
