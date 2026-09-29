#include "battlespades/world/prefab_placement.hpp"

#include <cstddef>
#include <cmath>
#include <chrono>
#include <numbers>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using namespace battlespades::world;

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

[[nodiscard]] VxlMap empty_world() {
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

void overlap_is_legal_and_only_air_costs_blocks() {
    auto map = empty_world();
    constexpr VxlColor stone{90U, 100U, 110U, 255U};
    expect(map.set_voxel(20U, 20U, 40U, stone), "fixture support must be placed");
    const std::vector<PrefabPlacementCell> footprint{
        {20, 20, 40}, {21, 20, 40}, {22, 20, 40}};
    const auto result = evaluate_prefab_placement(map, footprint);
    expect(result.all_in_bounds && result.touches_world && result.required_blocks == 2U,
           "an occupied prefab voxel must attach the shape and fill only two air cells");
    expect(result.model_blocks == 3U,
           "the retail wallet gate is len(model_points), overlap included");
}

void competitive_prefab_colour_and_smoke_rings_follow_build_prefab() {
    // shared.common.blend_color(base, voxel, 0.5): int(voxel + (base-voxel)/2);
    // live sample base (44,117,179) + voxel (176,179,179) -> (110,148,179).
    expect(retail_prefab_blend({44U, 117U, 179U, 255U}, {176U, 179U, 179U, 9U}) ==
               VxlColor{110U, 148U, 179U, 255U},
           "prefab colour must be the exact truncating 50/50 blend, no jitter");
    expect(retail_prefab_blend({1U, 3U, 255U, 255U}, {2U, 4U, 0U, 255U}) ==
               VxlColor{1U, 3U, 127U, 255U},
           "blend must truncate like Python int()");

    // One ring per voxel on the model's max-z (lowest) layer, at cell + z 1.
    const std::vector<PrefabModelVoxel> model{
        {0, 0, 0}, {1, 0, 0}, {0, 0, 2}, {1, 0, 2}, {2, 0, 2}};
    const auto rings = prefab_smoke_rings(model, 3U, {100, 100, 50}, 0U, 0U, 0U);
    expect(rings.size() == 3U, "one smoke ring per bottom-layer model voxel");
    expect(rings.front().position == std::array<float, 3U>{100.0F, 100.0F, 53.0F} &&
               rings.front().radius == 1.0F,
           "ring sits one below the voxel with radius (x_size - 1) / 2");
    const auto turned = prefab_smoke_rings(model, 3U, {100, 100, 50}, 1U, 0U, 0U);
    expect(turned.size() == 3U && turned[1U].position[0U] == 100.0F &&
               turned[1U].position[1U] == 99.0F,
           "ring positions follow the packet-30 rotation");
}

void block_drag_cost_and_support_match_the_authoritative_build() {
    auto map = empty_world();
    constexpr VxlColor stone{90U, 100U, 110U, 255U};
    expect(map.set_voxel(20U, 20U, 40U, stone), "fixture support must be placed");
    const std::array crossing{VoxelCell{19, 20, 40}, VoxelCell{20, 20, 40},
                              VoxelCell{21, 20, 40}, VoxelCell{22, 20, 40}};
    const auto result = evaluate_block_line_placement(map, crossing);
    expect(result.required_blocks == 3U && result.can_place(3),
           "dragging through an existing block must charge only the three empty cells");
    expect(!result.can_place(2) && result.can_place(0, true),
           "block cost must reject insufficient finite stock and honor infinite stock");
    expect(!map.solid(19, 20, 40) && !map.solid(22, 20, 40),
           "evaluating a preview must never mutate terrain");

    const std::array occupied{VoxelCell{20, 20, 40}};
    expect(!evaluate_block_line_placement(map, occupied).can_place(100, true),
           "a fully occupied drag must remain a no-op even with infinite stock");
    const std::array backwards{VoxelCell{22, 20, 40}, VoxelCell{21, 20, 40}};
    expect(!evaluate_block_line_placement(map, backwards).can_place(100),
           "a later supported cell cannot retroactively support the first floating cell");

    const std::array water_surface{VoxelCell{20, 20, 238}};
    expect(evaluate_block_line_placement(map, water_surface).can_place(1),
           "z=238 is the retail buildable layer immediately above the immutable bed");
    for (const auto invalid : {VoxelCell{20, 20, 0}, VoxelCell{20, 20, 239},
                                VoxelCell{512, 20, 40}}) {
        const std::array cells{invalid};
        expect(!evaluate_block_line_placement(map, cells).can_place(100, true),
               "sky, bed, and horizontal out-of-world cells must fail before placement");
    }
    std::vector<VoxelCell> overlong;
    for (std::uint32_t x = 21; x <= 85; ++x) overlong.push_back({x, 20, 40});
    expect(!evaluate_block_line_placement(map, overlong).can_place(100, true),
           "the 65th cell must invalidate a long drag instead of presenting a truncated valid line");
}

void no_op_and_out_of_bounds_fail_closed() {
    auto map = empty_world();
    constexpr VxlColor stone{90U, 100U, 110U, 255U};
    expect(map.set_voxel(20U, 20U, 40U, stone), "fixture support must be placed");
    const std::vector<PrefabPlacementCell> no_op{{20, 20, 40}};
    const auto occupied = evaluate_prefab_placement(map, no_op);
    expect(occupied.touches_world && !occupied.has_effect(),
           "a fully occupied footprint must not consume inventory or emit a build");

    const std::vector<PrefabPlacementCell> clipped{{20, 20, 40}, {-1, 20, 40}};
    expect(!evaluate_prefab_placement(map, clipped).all_in_bounds,
           "a clipped prefab must fail atomically");
}

void streamed_prefab_is_revealed_atomically_on_completion() {
    auto map = empty_world();
    constexpr VxlColor old_stone{90U, 100U, 110U, 255U};
    constexpr VxlColor team_block{20U, 120U, 220U, 255U};
    expect(map.set_voxel(20U, 20U, 40U, old_stone), "fixture overlap must be placed");

    PrefabPlacementTransaction transaction;
    const std::vector<PrefabPlacementVoxel> first{
        {{20, 20, 40}, team_block}, {{21, 20, 40}, team_block}};
    const std::vector<PrefabPlacementVoxel> second{
        {{22, 20, 40}, team_block}, {{23, 20, 40}, team_block}};
    expect(transaction.stage(first) && transaction.stage(second) && transaction.size() == 4U,
           "packet-30 slices must accumulate in one bounded transaction");
    expect(!map.solid(21U, 20U, 40U) && !map.solid(23U, 20U, 40U),
           "staged slices must remain invisible until PrefabComplete");

    const auto committed = transaction.commit(map);
    expect(committed.committed && committed.placed == 3U && committed.overlapped == 1U &&
               transaction.empty(),
           "PrefabComplete must reveal every missing voxel in one commit");
    expect(map.color(20U, 20U, 40U) == old_stone,
           "legal overlap must preserve existing world colour");
    expect(map.color(21U, 20U, 40U) == team_block &&
               map.color(22U, 20U, 40U) == team_block &&
               map.color(23U, 20U, 40U) == team_block,
           "all missing prefab voxels must become visible together");
}

void clipped_transaction_never_leaves_a_partial_prefab() {
    auto map = empty_world();
    PrefabPlacementTransaction transaction;
    constexpr VxlColor team_block{20U, 120U, 220U, 255U};
    const std::vector<PrefabPlacementVoxel> clipped{
        {{40, 40, 40}, team_block}, {{-1, 40, 40}, team_block}};
    expect(transaction.stage(clipped), "decode-valid slices stage before world validation");
    const auto committed = transaction.commit(map);
    expect(!committed.committed && committed.placed == 0U && transaction.empty() &&
               !map.solid(40U, 40U, 40U),
           "one invalid cell must reject the complete prefab before any VXL mutation");
}

void large_editor_constructs_stage_without_quadratic_scans() {
    auto map = empty_world();
    PrefabPlacementTransaction transaction;
    constexpr VxlColor stone{90U, 100U, 110U, 255U};
    std::vector<PrefabPlacementVoxel> slice;
    constexpr std::size_t count{216'850U}; // Largest bundled Lunar landscape.
    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t index{}; index < count; ++index) {
        slice.push_back({{static_cast<std::int32_t>(index % 512U),
                          static_cast<std::int32_t>(index / 512U), 100}, stone});
        if (slice.size() == 4096U || index + 1U == count) {
            expect(transaction.stage(slice), "large retail UGC construct must accept every slice");
            slice.clear();
        }
    }
    const auto staged_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
    const std::vector<PrefabPlacementVoxel> replay{{{0, 0, 100}, {1U, 2U, 3U, 255U}}};
    expect(transaction.stage(replay) && transaction.size() == count,
           "overlapping slices must update colour without duplicating cells");
    expect(!map.solid(0U, 0U, 100U), "large construct must stay invisible before completion");
    const auto result = transaction.commit(map);
    expect(result.committed && result.placed == count && transaction.empty(),
           "completion must reveal the entire large construct");
    expect(map.color(0U, 0U, 100U) == replay[0U].color,
           "latest replay colour must survive commit");
    expect(transaction.stage(replay) && transaction.size() == 1U,
           "completion must clear the duplicate index for the next build");
    std::cout << "216850-cell streamed UGC staging: " << staged_ms << " ms\n";
}

void placement_audio_edges_use_retail_stems() {
    expect(placement_sound_stem(PlacementFeedbackKind::block) == "build",
           "ordinary blocks must use retail build.ogg");
    expect(placement_sound_stem(PlacementFeedbackKind::prefab) == "prefabbuild",
           "competitive prefabs must use retail prefabbuild.ogg");
    expect(placement_sound_stem(PlacementFeedbackKind::ugc_prefab) == "ugc_place",
           "UGC prefabs must retain their distinct authored placement sound");
}

void preview_cache_tracks_pose_map_and_model_changes() {
    auto map = empty_world();
    const std::array cells{PrefabPlacementCell{0, 0, 0}, PrefabPlacementCell{1, 0, 0}};
    PrefabPlacementPreview preview;
    preview.reset(cells);
    const PrefabPlacementCell anchor{20, 20, 40};
    const auto first = preview.evaluate(map, anchor, 0U, 0U, 0U);
    expect(first.required_blocks == 2U && !first.touches_world, "initial floating footprint");
    for (int frame{}; frame < 1'000; ++frame) {
        static_cast<void>(preview.bounds(0U, 0U, 0U));
        static_cast<void>(preview.evaluate(map, anchor, 0U, 0U, 0U));
    }
    expect(preview.evaluations() == 1U, "unchanged simulation/render calls must reuse one voxel scan");
    expect(map.set_voxel(20U, 20U, 40U, {90U, 100U, 110U}), "mutate live map");
    const auto changed = preview.evaluate(map, anchor, 0U, 0U, 0U);
    expect(changed.touches_world && changed.required_blocks == 1U && preview.evaluations() == 2U,
           "server terrain mutations must invalidate cached placement");
    static_cast<void>(preview.evaluate(map, {21, 20, 40}, 0U, 0U, 0U));
    expect(preview.evaluations() == 3U, "anchor changes must re-evaluate placement");
    static_cast<void>(preview.evaluate(map, {21, 20, 40}, 1U, 0U, 0U));
    expect(preview.evaluations() == 4U, "rotation changes must re-evaluate placement");
    preview.reset(std::span{cells}.first(1U));
    expect(!preview.evaluate(map, anchor, 0U, 0U, 0U).has_effect(),
           "changing models must not reuse the previous footprint");
    preview.reset();
    expect(!preview.evaluate(map, anchor, 0U, 0U, 0U).has_effect(), "clearing a preview leaves no footprint");
}

void ghost_position_follows_retail_get_prefab_ghost_position() {
    auto map = empty_world();
    // prefab_ultrabarrier: KV6 header 6x14x6, the Soldier's first prefab.
    PrefabGhostRequest request;
    request.position = {256.3, 256.7, 100.2};
    request.orientation = {1.0, 0.0, 0.0};
    request.size = {6, 14, 6};
    request.player_direction = 0U;
    request.check_world_intersect = false;
    // Default override 1: rotated sizes (14, -6, 6), radius sqrt(67) -> band
    // 10, distance 10.82; floor(267.12, 256.7, 100.2) - int(ws / 2) then
    // int(-0.8 * 6 / 2.0) = -2 on z.
    request.yaw = 1U;
    auto ghost = prefab_ghost_position(map, {}, request);
    expect(ghost.anchor == PrefabPlacementCell{260, 259, 95},
           "signed rotated sizes truncate toward zero around the aim point");
    expect(ghost.center == std::array<std::int32_t, 3U>{267, 256, 98},
           "prefab_center adds int(world_size / 2) back");
    // Override 3 (WEST relative to the facing): both even sizes compensate.
    request.yaw = 3U;
    ghost = prefab_ghost_position(map, {}, request);
    expect(ghost.anchor == PrefabPlacementCell{273, 254, 95},
           "even-size facing compensation moves x -1 and y +1");
    request.use_player_orientation = false;
    ghost = prefab_ghost_position(map, {}, request);
    expect(ghost.anchor == PrefabPlacementCell{274, 253, 95},
           "UGC ghosts skip the facing compensation");

    // FACE_BOTTOM lift while the model intersects and still sinks below the
    // player's floor (floor(z + 2.25)); it stops at that floor regardless.
    const std::vector<PrefabPlacementCell> single{{0, 0, 0}};
    PrefabGhostRequest down;
    down.position = {100.5, 100.5, 30.0};
    down.orientation = {0.0, 0.0, 1.0};
    down.size = {1, 1, 1};
    for (std::uint32_t z{32U}; z <= 37U; ++z) {
        expect(!map.solid(100U, 100U, z), "fixture column starts as air");
    }
    expect(prefab_ghost_position(map, single, down).anchor == PrefabPlacementCell{100, 100, 37},
           "minimum distance 7 straight down with nothing to intersect");
    constexpr VxlColor stone{90U, 100U, 110U, 255U};
    for (std::uint32_t z{35U}; z <= 37U; ++z) {
        expect(map.set_voxel(100U, 100U, z, stone), "fixture column must be placed");
    }
    expect(prefab_ghost_position(map, single, down).anchor == PrefabPlacementCell{100, 100, 34},
           "the ghost lifts out of the terrain it intersects");
    for (std::uint32_t z{32U}; z <= 34U; ++z) {
        expect(map.set_voxel(100U, 100U, z, stone), "fixture column must be placed");
    }
    expect(prefab_ghost_position(map, single, down).anchor == PrefabPlacementCell{100, 100, 32},
           "the lift stops at the player's floor even while intersecting");
    down.check_world_intersect = false;
    expect(prefab_ghost_position(map, single, down).anchor == PrefabPlacementCell{100, 100, 37},
           "UGC ghosts never lift");
}

void ghost_transform_matches_voxels_at_every_rotation_and_pivot() {
    const std::array<float, 3U> pivot{2.5F, -3.0F, 4.25F};
    const std::array<double, 3U> voxel{7.0, 11.0, 13.0};
    const std::array render{voxel[0U] - pivot[0U], -(voxel[2U] - pivot[2U]),
                            voxel[1U] - pivot[1U]};
    for (std::uint8_t yaw{}; yaw < 4U; ++yaw) {
        for (std::uint8_t pitch{}; pitch < 4U; ++pitch) {
            for (std::uint8_t roll{}; roll < 4U; ++roll) {
                // Independent continuous-axis oracle in authored space: Y, X, Z.
                const auto angle = [](std::uint8_t turn) { return -turn * std::numbers::pi / 2.0; };
                auto expected = voxel;
                const auto rotate = [&](std::size_t a, std::size_t b, double theta) {
                    const auto old_a = expected[a], old_b = expected[b];
                    expected[a] = old_a * std::cos(theta) - old_b * std::sin(theta);
                    expected[b] = old_a * std::sin(theta) + old_b * std::cos(theta);
                };
                rotate(2U, 0U, angle(roll));
                rotate(1U, 2U, angle(pitch));
                rotate(0U, 1U, angle(yaw));
                const auto transform = prefab_preview_transform(pivot, {20, 30, 40}, yaw, pitch, roll);
                const auto cell = rotate_prefab_cell({7, 11, 13}, yaw, pitch, roll);
                const std::array rotated{cell.x, cell.y, cell.z};
                for (std::size_t axis{}; axis < 3U; ++axis) {
                    const auto actual = render[0U] * transform[axis] + render[1U] * transform[4U + axis] +
                                        render[2U] * transform[8U + axis] + transform[12U + axis];
                    expect(std::abs(rotated[axis] - expected[axis]) < 1e-6,
                           "protocol cell rotation must match the independent axis oracle");
                    expect(std::abs(actual - (expected[axis] + 20.5 + 10.0 * static_cast<double>(axis))) < 1e-6,
                           "preview voxel centres must match placement after every axis/pivot rotation");
                }
            }
        }
    }
}

} // namespace

int main() {
    try {
        overlap_is_legal_and_only_air_costs_blocks();
        competitive_prefab_colour_and_smoke_rings_follow_build_prefab();
        block_drag_cost_and_support_match_the_authoritative_build();
        no_op_and_out_of_bounds_fail_closed();
        streamed_prefab_is_revealed_atomically_on_completion();
        clipped_transaction_never_leaves_a_partial_prefab();
        large_editor_constructs_stage_without_quadratic_scans();
        placement_audio_edges_use_retail_stems();
        preview_cache_tracks_pose_map_and_model_changes();
        ghost_transform_matches_voxels_at_every_rotation_and_pivot();
        ghost_position_follows_retail_get_prefab_ghost_position();
        std::cout << "prefab placement tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
