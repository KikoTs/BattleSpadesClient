#include "battlespades/world/prefab_placement.hpp"

#include <cstddef>
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
           "an occupied prefab voxel must attach the shape and cost zero blocks");
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

void placement_audio_edges_use_retail_stems() {
    expect(placement_sound_stem(PlacementFeedbackKind::block) == "build",
           "ordinary blocks must use retail build.ogg");
    expect(placement_sound_stem(PlacementFeedbackKind::prefab) == "prefabbuild",
           "competitive prefabs must use retail prefabbuild.ogg");
    expect(placement_sound_stem(PlacementFeedbackKind::ugc_prefab) == "ugc_place",
           "UGC prefabs must retain their distinct authored placement sound");
}

} // namespace

int main() {
    try {
        overlap_is_legal_and_only_air_costs_blocks();
        no_op_and_out_of_bounds_fail_closed();
        streamed_prefab_is_revealed_atomically_on_completion();
        clipped_transaction_never_leaves_a_partial_prefab();
        placement_audio_edges_use_retail_stems();
        std::cout << "prefab placement tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
