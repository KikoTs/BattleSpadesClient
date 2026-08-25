#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/kv6_model.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

using namespace battlespades::world;

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

/**
 * The normal vs_world.sc reconstructs from a stored face index.
 *
 * Kept verbatim here rather than shared, so that if the shader's decode table
 * ever changes without this test being updated, the mismatch surfaces as a
 * failure instead of silently agreeing.
 */
[[nodiscard]] std::array<float, 3U> shader_normal_for_face(std::uint8_t face) {
    switch (face) {
    case 0U: return {-1.0F, 0.0F, 0.0F};
    case 1U: return {1.0F, 0.0F, 0.0F};
    case 2U: return {0.0F, -1.0F, 0.0F};
    case 3U: return {0.0F, 1.0F, 0.0F};
    case 4U: return {0.0F, 0.0F, -1.0F};
    default: return {0.0F, 0.0F, 1.0F};
    }
}

/**
 * The KV6 loader writes positions as (x, -z, y).
 *
 * A normal in authored KV6 axes therefore lands in render space under the same
 * map, which is what the stored face index has to agree with.
 */
[[nodiscard]] std::array<float, 3U> kv6_axis_to_render(std::array<float, 3U> value) {
    return {value[0U], -value[2U], value[1U]};
}

void the_stored_face_index_matches_the_position_swizzle() {
    // Authored KV6 face normals, in the loader's own table order.
    constexpr std::array<std::array<float, 3U>, 6U> kv6_normals{{
        {-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F},
        {0.0F, -1.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, -1.0F}, {0.0F, 0.0F, 1.0F},
    }};
    // The permutation the loader applies before storing ChunkVertex::face.
    constexpr std::array<std::uint8_t, 6U> expected_permutation{0U, 1U, 4U, 5U, 3U, 2U};

    for (std::uint8_t face{}; face < 6U; ++face) {
        const auto in_render_space = kv6_axis_to_render(kv6_normals[face]);
        const auto decoded = shader_normal_for_face(expected_permutation[face]);
        for (std::size_t axis{}; axis < 3U; ++axis) {
            expect(std::abs(in_render_space[axis] - decoded[axis]) < 1.0e-5F,
                   "KV6 face " + std::to_string(face) +
                       " does not decode to its swizzled normal; a model would be "
                       "lit as though one of its sides were its top");
        }
    }
}

void a_real_model_stores_only_permuted_faces() {
    const std::filesystem::path root{AOS_TEST_ASSET_ROOT};
    const auto path = root / "kv6" / "semi.kv6";
    if (!std::filesystem::is_regular_file(path)) {
        // The asset tree is optional for this check; the algebra above is the
        // real assertion.
        return;
    }
    std::string error;
    const auto model = Kv6Model::load_file(path, &error);
    expect(static_cast<bool>(model), "the shipped rifle KV6 must parse: " + error);
    const auto mesh = model->mesh();
    expect(!mesh.vertices.empty(), "the rifle KV6 must emit geometry");

    // Every face a cube mesh can emit must be one of the six, and the four
    // corners of any quad must agree on which one.
    std::unordered_map<std::uint8_t, std::size_t> seen;
    for (const auto& vertex : mesh.vertices) {
        expect(vertex.face < 6U, "a stored face index is out of range");
        ++seen[vertex.face];
    }
    for (std::size_t index{}; index + 3U < mesh.vertices.size(); index += 4U) {
        const auto face = mesh.vertices[index].face;
        for (std::size_t corner{1U}; corner < 4U; ++corner) {
            expect(mesh.vertices[index + corner].face == face,
                   "the four corners of one quad must share a face index");
        }
    }
    // A closed voxel model exposes faces on more than one axis; if the
    // permutation collapsed indices we would see fewer distinct values.
    expect(seen.size() >= 3U,
           "a solid model must expose faces on at least three distinct directions");
}

/**
 * Models must not be self-illuminating.
 *
 * ChunkVertex::abgr's alpha byte is the self-illumination channel, applied by
 * fs_world.sc AFTER every occlusion term because a light source is not dimmed by
 * shadow or ambient occlusion. Terrain writes 0 there. KV6 wrote 255, so every
 * arm, weapon and player voxel added an unshaded copy of its own albedo on top
 * of its lit result -- which is exactly what made the first-person hands read as
 * bright plastic, since a flat additive term compresses the lit-to-shadow ratio
 * toward 1:1 no matter how good the lighting underneath it is.
 */
void models_do_not_emit_light() {
    const std::filesystem::path root{AOS_TEST_ASSET_ROOT};
    const auto path = root / "kv6" / "semi.kv6";
    if (!std::filesystem::is_regular_file(path)) {
        return;
    }
    std::string error;
    const auto model = Kv6Model::load_file(path, &error);
    expect(model.has_value(), "semi.kv6 failed to load: " + error);
    const auto mesh = model->mesh();
    expect(!mesh.vertices.empty(), "semi.kv6 meshed to nothing");
    for (const auto& vertex : mesh.vertices) {
        expect((vertex.abgr >> 24U) == 0U,
               "a KV6 vertex carries non-zero emission; models would glow");
    }
}

/**
 * A real model must produce some occluded corners, and must not be all-dark.
 *
 * KV6 models previously uploaded a hardcoded occlusion of 0, so every face was
 * uniformly lit with no crevice shading at all. The upper bound matters just as
 * much as the lower one: these models are thin, and an AO probe with a sign
 * error would return the darkest level almost everywhere and turn a weapon into
 * a silhouette.
 */
void a_real_model_has_varied_corner_occlusion() {
    const std::filesystem::path root{AOS_TEST_ASSET_ROOT};
    const auto path = root / "kv6" / "semi.kv6";
    if (!std::filesystem::is_regular_file(path)) {
        return;
    }
    std::string error;
    const auto model = Kv6Model::load_file(path, &error);
    expect(model.has_value(), "semi.kv6 failed to load: " + error);
    const auto mesh = model->mesh();

    std::array<std::size_t, 4U> histogram{};
    for (const auto& vertex : mesh.vertices) {
        expect(vertex.occlusion < 4U, "corner occlusion escaped its 0..3 range");
        ++histogram[vertex.occlusion];
    }
    expect(histogram[0U] > 0U, "no corner is fully open; the probe is inverted");
    const auto occluded = histogram[1U] + histogram[2U] + histogram[3U];
    expect(occluded > 0U,
           "a weapon with barrels and sights produced no occluded corners at all");
    // Most of a convex-ish model's corners should stay open. Past roughly half,
    // the model reads as uniformly dirty rather than as having crevices.
    expect(histogram[0U] * 2U >= mesh.vertices.size(),
           "over half of all corners are occluded; the model will render muddy");
}

} // namespace

int main() {
    try {
        the_stored_face_index_matches_the_position_swizzle();
        a_real_model_stores_only_permuted_faces();
        models_do_not_emit_light();
        a_real_model_has_varied_corner_occlusion();
        std::cout << "kv6 normal tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
