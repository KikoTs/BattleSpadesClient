#include "battlespades/frontend/runtime_vfx_adapter.hpp"
#include "battlespades/network/protocol168_runtime.hpp"
#include "battlespades/world/particle_system.hpp"
#include "battlespades/world/terrain_effects.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] battlespades::world::VxlMap empty_world() {
    using battlespades::world::VxlMap;
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(VxlMap::width) * VxlMap::depth * 4U);
    for (std::size_t column{};
         column < static_cast<std::size_t>(VxlMap::width) * VxlMap::depth;
         ++column) {
        bytes.insert(bytes.end(), {std::byte{0U}, std::byte{1U},
                                   std::byte{0U}, std::byte{0U}});
    }
    auto loaded = VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "synthetic VXL must parse");
    return std::move(*loaded.map);
}

void decoded_corpse_packet_reaches_retail_particle_composition() {
    using namespace battlespades;

    const std::array packet_bytes{std::byte{0x24U}, std::byte{0x03U}, std::byte{0x01U}};
    const auto decoded = network::decode_runtime_packet(packet_bytes);
    expect(static_cast<bool>(decoded), "ExplodeCorpse wire packet must decode");
    const auto* packet = std::get_if<network::ExplodeCorpsePacket>(&*decoded.packet);
    expect(packet != nullptr, "packet 36 must retain its runtime type");

    const auto impact = frontend::make_corpse_explosion_impact(
        *packet, {123.25F, 77.75F, 228.5F}, world::VxlColor{55U, 125U, 215U, 255U});
    expect(impact.has_value() && impact->position.has_value() &&
               *impact->position == std::array<float, 3U>{123.25F, 77.75F, 228.5F},
           "packet 36 must preserve the retained corpse transform");

    world::ParticleSystem particles;
    world::TerrainEffectSimulation effects;
    effects.set_particle_sink(&particles);
    effects.spawn_impact(*impact);
    particles.build_draw_list({}, 0.0F);

    expect(particles.live_count() == 40U && particles.instances().size() == 40U,
           "live packet 36 must reach Character.explode_corpse's 40 body particles");
    for (const auto& instance : particles.instances()) {
        expect(instance.rgba[0U] == 1.0F && instance.rgba[1U] == 0.0F &&
                   instance.rgba[2U] == 0.0F,
               "corpse packet particles must be saturated retail red");
    }

    const auto airburst = frontend::make_jetpack_death_airburst(
        {123.25F, 77.75F, 228.5F});
    expect(airburst.kind == world::TerrainImpactKind::explosion &&
               airburst.position ==
                   std::optional<std::array<float, 3U>>{
                       std::array<float, 3U>{123.25F, 77.75F, 228.5F}} &&
               airburst.radius > impact->radius,
           "jetpack death must retain a larger airborne explosion at the corpse transform");
    world::ParticleSystem jetpack_particles;
    world::TerrainEffectSimulation jetpack_effects;
    jetpack_effects.set_particle_sink(&jetpack_particles);
    jetpack_effects.spawn_impact(
        airburst, world::TerrainImpactSoundPolicy::silent);
    const auto map = empty_world();
    jetpack_effects.tick(1.0 / 60.0, map);
    expect(!jetpack_effects.lights().empty(),
           "jetpack death airburst must contribute an enhanced-renderer light");

    const std::array cleanup_bytes{std::byte{0x24U}, std::byte{0x03U}, std::byte{0x00U}};
    const auto cleanup_decoded = network::decode_runtime_packet(cleanup_bytes);
    expect(static_cast<bool>(cleanup_decoded), "silent ExplodeCorpse cleanup must decode");
    const auto& cleanup = std::get<network::ExplodeCorpsePacket>(*cleanup_decoded.packet);
    expect(!frontend::make_corpse_explosion_impact(
                cleanup, {123.25F, 77.75F, 228.5F}, world::VxlColor{55U, 125U, 215U, 255U})
                .has_value(),
           "packet 36 effect flag zero must remain silent cleanup");
}

} // namespace

int main() {
    try {
        decoded_corpse_packet_reaches_retail_particle_composition();
        std::cout << "runtime VFX adapter tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "runtime VFX adapter tests failed: " << error.what() << '\n';
        return 1;
    }
}
