#include "battlespades/world/classic_combat.hpp"
#include "battlespades/world/classic_movement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

using namespace battlespades::world;
namespace {
void check(bool passed, const std::string& message) {
    if (!passed) throw std::runtime_error(message);
}
Vec3 add(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Vec3 sub(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vec3 mul(Vec3 v, double n) { return {v.x*n, v.y*n, v.z*n}; }
double length(Vec3 v) { return std::hypot(v.x, v.y, v.z); }
Vec3 unit(Vec3 v) { return mul(v, 1.0/length(v)); }
constexpr std::array<std::uint64_t, 2> seed{12345, 67890};
constexpr WeaponAction rifle{WeaponActionKind::hitscan, 6, 7, 1};

VxlMap flat_map() {
    std::vector<std::byte> raw;
    raw.reserve(512U * 512U * 8U);
    for (std::size_t n = 0; n < 512U * 512U; ++n)
        for (const auto value : {0, 239, 239, 0, 90, 100, 110, 128})
            raw.push_back(static_cast<std::byte>(value));
    auto loaded = VxlMap::load(raw, VxlDecodeProfile::canonical240);
    check(loaded.map.has_value(), "flat combat fixture must decode");
    return std::move(*loaded.map);
}

// Independent forward-transform/plane-intersection oracle adapted from
// ZeroSpades Player::GetHitBoxes and Core/Math.cpp OBB3::RayCast (yvt,
// GPL-3.0-or-later). Production instead inverse-transforms a ray and uses slabs.
// Keeping float32 here also checks the port's boundary/rotation precision.
using F3 = std::array<float, 3>;
F3 fadd(F3 a, F3 b) { return {a[0]+b[0], a[1]+b[1], a[2]+b[2]}; }
F3 fsub(F3 a, F3 b) { return {a[0]-b[0], a[1]-b[1], a[2]-b[2]}; }
F3 fmul(F3 a, float n) { return {a[0]*n, a[1]*n, a[2]*n}; }
float fdot(F3 a, F3 b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
F3 packed(Vec3 a) { return {static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z)}; }
struct ReferenceBox {
    F3 origin;
    std::array<F3, 3> axes;
};
ReferenceBox reference_box(F3 anchor, float yaw, float pitch, F3 minimum, F3 size) {
    const float c = std::cos(yaw), s = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const std::array<F3, 3> basis{{{c,s,0}, {-s*cp,c*cp,sp}, {s*sp,-c*sp,cp}}};
    ReferenceBox result{anchor, {}};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.origin = fadd(result.origin, fmul(basis[axis], minimum[axis]));
        result.axes[axis] = fmul(basis[axis], size[axis]);
    }
    return result;
}
struct ReferenceContact {
    float distance{};
    F3 normal{};
    float plane_offset{};
};
std::optional<ReferenceContact> reference_intersection(const ReferenceBox& box, F3 start, F3 direction) {
    start = fsub(start, box.origin);
    bool inside = true;
    for (const auto& axis : box.axes) {
        const auto p = fdot(start, axis);
        inside &= p >= 0 && p < fdot(axis, axis);
    }
    if (inside) return ReferenceContact{};
    const auto end = fadd(start, direction);
    for (std::size_t plane = 0; plane < 3; ++plane) {
        const auto& normal = box.axes[plane];
        if (fdot(direction, normal) == 0) continue;
        const float begin = fdot(start, normal), finish = fdot(end, normal);
        const float hit = begin < finish ? begin / (begin-finish)
            : (fdot(normal, normal)-begin) / (finish-begin);
        if (hit < 0) continue;
        const auto point = fadd(start, fmul(direction, hit));
        bool on_face = true;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (axis == plane) continue;
            const float distance = fdot(point, box.axes[axis]);
            on_face &= distance >= 0 && distance <= fdot(box.axes[axis], box.axes[axis]);
        }
        if (on_face) {
            const auto unit_normal = fmul(normal, 1/std::sqrt(fdot(normal,normal)));
            const auto plane_point = begin < finish ? box.origin : fadd(box.origin,normal);
            return ReferenceContact{hit,unit_normal,fdot(unit_normal,plane_point)};
        }
    }
    return {};
}
bool coplanar(const ReferenceContact& a, const ReferenceContact& b) {
    // Float32 world coordinates near256 have a ~0.00003 ULP. Only permit
    // alternative tie ordering for the same oriented surface and ray contact,
    // never just because unrelated surfaces happen to be close together.
    const float parallel = fdot(a.normal,b.normal);
    return std::abs(std::abs(parallel)-1) < 0.000001F &&
           std::abs(a.plane_offset-(parallel<0?-b.plane_offset:b.plane_offset)) < 0.0001F &&
           std::abs(a.distance-b.distance) < 0.0001F;
}
struct ReferenceHit {
    std::optional<ClassicHit> hit;
    bool coplanar_tie{};
};
ReferenceHit reference_hit(const ClassicHitTarget& target, Vec3 start, Vec3 ray, int tie_order = 0) {
    const auto aim = packed(target.orientation);
    const float yaw = std::atan2(aim[1], aim[0]) + std::numbers::pi_v<float>/2;
    const float pitch = -std::atan2(aim[2], std::hypot(aim[0], aim[1]));
    float arm_pitch = pitch - (target.sprinting ? 0.9F : 0.0F);
    if (arm_pitch < 0) arm_pitch = std::max(arm_pitch, -std::numbers::pi_v<float>/2)*0.9F;
    auto lower = packed(target.position);
    lower[2] += target.crouched ? 0.75F : 1.2F;
    auto torso = lower;
    torso[2] -= target.crouched ? 0.5F : 1.0F;
    auto head = torso, arms = torso;
    head[2] -= target.crouched ? 0.05F : 0.0F;
    arms[2] += target.crouched ? 0.0F : 0.1F;
    const std::array boxes{
        reference_box(head,yaw,pitch,{-0.3F,-0.3F,-0.6F},{0.6F,0.6F,0.6F}),
        reference_box(torso,yaw,0,target.crouched?F3{-0.4F,-0.1F,-0.1F}:F3{-0.4F,-0.2F,0},
                      target.crouched?F3{0.8F,0.8F,0.7F}:F3{0.8F,0.4F,0.9F}),
        reference_box(lower,yaw,0,{-0.4F,target.crouched?-0.1F:-0.2F,target.crouched?-0.2F:-0.15F},
                      {0.3F,0.4F,target.crouched?0.8F:1.2F}),
        reference_box(lower,yaw,0,{0.1F,target.crouched?-0.1F:-0.2F,target.crouched?-0.2F:-0.15F},
                      {0.3F,0.4F,target.crouched?0.8F:1.2F}),
        reference_box(arms,yaw,arm_pitch,{-0.6F,-1,-0.1F},{1.2F,1,0.6F})};
    constexpr std::array<unsigned, 5> parts{1,0,3,3,2};
    float distance = std::numeric_limits<float>::infinity();
    unsigned flags{}, closest_part{99};
    ReferenceHit result;
    std::optional<ReferenceContact> previous;
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        if (i == 4 && (closest_part == 0 || closest_part == 1)) continue;
        const auto hit = reference_intersection(boxes[i], packed(start), packed(ray));
        if (!hit) continue;
        const bool tied = previous && coplanar(*hit,*previous);
        result.coplanar_tie |= tied;
        if (tied && tie_order < 0) continue;
        if (!(tied && tie_order > 0) && hit->distance >= distance && !(i < 2 && closest_part == 2)) continue;
        distance = hit->distance;
        previous = hit;
        closest_part = parts[i];
        flags |= 1U << parts[i];
    }
    if (!flags) return result;
    const auto part = static_cast<std::uint8_t>((flags & 1U) ? 0 : (flags & 2U) ? 1 : (flags & 4U) ? 2 : 3);
    result.hit = ClassicHit{target.id, part, add(start, mul(ray, distance))};
    return result;
}

void hitbox_reference_tests(const VxlMap& map) {
    const std::array targets{
        ClassicHitTarget{1,{256,256,200},{1,0,0},false,false},
        ClassicHitTarget{1,{256,256,200},{1,0,0},true,false},
        ClassicHitTarget{1,{256,256,200},unit({1,1,0}),false,false},
        ClassicHitTarget{1,{256,256,200},unit({-1,2,2}),false,false},
        ClassicHitTarget{1,{256,256,200},unit({1,-1,-2}),true,false},
        ClassicHitTarget{1,{256,256,200},unit({1,1,0.4}),false,true}};
    std::size_t cases{}, hits{}, misses{}, coplanar_cases{}, alternative_contacts{};
    for (std::size_t pose = 0; pose < targets.size(); ++pose) {
        const auto& target = targets[pose];
        for (int view = 0; view < 4; ++view) {
            const double yaw = view * std::numbers::pi/2 + 0.31;
            const Vec3 across{-std::sin(yaw),std::cos(yaw),0};
            PlayerMovementState player;
            player.position = add(target.position, {std::cos(yaw)*20,std::sin(yaw)*20,view==2?8.0:0.8});
            for (int x = -5; x <= 5; ++x) for (int z = -4; z <= 12; ++z) {
                auto point = add(target.position, mul(across, x*0.17));
                point.z += z*0.23;
                player.orientation = unit(sub(point, player.position));
                ClassicCombat combat{seed};
                const auto actual = combat.attack(map, player, std::span{&target,1}, rifle, 3, true, 0);
                check(actual.tracers.size() == 1, "reference shot must have one ray");
                const auto start = add(player.position, mul(player.orientation,0.01));
                const auto expected = reference_hit(target,start,actual.tracers.front().direction);
                coplanar_cases += expected.coplanar_tie ? 1U : 0U;
                const auto label = "reference pose="+std::to_string(pose)+" view="+std::to_string(view)+
                    " x="+std::to_string(x)+" z="+std::to_string(z);
                check(!actual.hits.empty() == expected.hit.has_value(), label+" hit/miss");
                if (expected.hit) {
                    ++hits;
                    check(actual.hits.front().part == expected.hit->part, label+" body part");
                    bool same_contact = length(sub(actual.hits.front().position,expected.hit->position)) < 0.003;
                    // A crouched leg and torso share a side plane. Float32
                    // tie ordering can expose or suppress the arms-only test,
                    // changing the selected contact without changing damage.
                    // Test both orders only for verified coplanar ties, retaining
                    // the same strict contact tolerance and body-part result.
                    if (!same_contact && expected.coplanar_tie) {
                        for (const int order : {-1,1}) {
                            const auto alternate = reference_hit(target,start,actual.tracers.front().direction,order);
                            same_contact |= alternate.hit && alternate.hit->part == expected.hit->part &&
                                length(sub(actual.hits.front().position,alternate.hit->position)) < 0.003;
                        }
                        alternative_contacts += same_contact ? 1U : 0U;
                    }
                    check(same_contact,
                          label+" first contact: actual="+std::to_string(actual.hits.front().position.x)+","+
                              std::to_string(actual.hits.front().position.y)+","+
                              std::to_string(actual.hits.front().position.z)+" reference="+
                              std::to_string(expected.hit->position.x)+","+std::to_string(expected.hit->position.y)+","+
                              std::to_string(expected.hit->position.z));
                } else ++misses;
                ++cases;
            }
        }
    }
    check(hits > 200 && misses > 200, "reference coverage must include hits and near misses");
    std::cout << "ZeroSpades reference: " << cases << " rays, " << hits << " hits, " << misses
              << " misses, " << coplanar_cases << " coplanar ties (" << alternative_contacts
              << " alternative contacts)\n";

    // Shoot from below through a leg into the torso: the nearest contact is a
    // leg, but reference hit flags report torso damage for the same player.
    const ClassicHitTarget target{7,{256,256,200},{1,0,0},false,false};
    PlayerMovementState low;
    low.position = {256,255.75,205};
    low.orientation = unit(sub({256,255.75,200.7},low.position));
    ClassicCombat combat{seed};
    const auto hit = combat.attack(map,low,std::span{&target,1},rifle,3,true,0);
    check(hit.hits.size() == 1 && hit.hits.front().part == 0 && hit.hits.front().position.z > 202,
          "leg contact through torso retains reference torso damage");
}

void range_and_eye_tests(VxlMap& map) {
    for (unsigned y = 247; y <= 253; ++y) for (unsigned z = 176; z < 238; ++z)
        check(map.set_voxel(200,y,z,{80,90,100,255}), "range wall");
    PlayerMovementState player;
    player.position = {70.5,250.5,210.5};
    player.orientation = {1,0,0};
    ClassicCombat outside{seed};
    const auto miss = outside.attack(map,player,{},rifle,3,true,0);
    check(miss.damage.empty() && miss.impacts.empty() && !miss.destroy,
          "blocks beyond horizontal 128 cannot be damaged or destroyed");
    player.position.x = 72.5;
    ClassicCombat inside{seed};
    check(!inside.attack(map,player,{},rifle,3,true,0).damage.empty(), "block within horizontal fog range");
    player.position.z = 180;
    player.orientation = unit({1,0,0.3});
    ClassicCombat steep{seed};
    const auto long_ray = steep.attack(map,player,{},rifle,4,true,0);
    check(!long_ray.damage.empty(), "steep ray longer than 128 still hits inside horizontal range");
    for (unsigned y = 247; y <= 253; ++y) for (unsigned z = 176; z < 238; ++z)
        check(map.clear_voxel(200,y,z), "remove range wall");

    ClassicHitTarget target{4,{200,250.5,210.5},{-1,0,0},false,false};
    // Keep this a range test across standard-library RNG implementations:
    // place the distant head on the actual sampled ray, not an ideal no-spread ray.
    player.orientation = {1,0,0}; player.position = {72.5,250.5,210.5};
    ClassicCombat range_sample{seed};
    const auto ray = range_sample.attack(map,player,{},rifle,3,true,0).tracers.front().direction;
    const auto muzzle = add(player.position,mul(player.orientation,0.01));
    target.position = add(muzzle,mul(ray,(200-muzzle.x)/ray.x));
    player.orientation = {1,0,0}; player.position = {70.5,250.5,210.5};
    ClassicCombat far_player{seed};
    check(far_player.attack(map,player,std::span{&target,1},rifle,3,true,0).hits.empty(),
          "players outside horizontal fog range are rejected");
    player.position.x = 72.5;
    ClassicCombat near_player{seed};
    check(!near_player.attack(map,player,std::span{&target,1},rifle,3,true,0).hits.empty(),
          "player within horizontal fog range remains hittable");

    target.position = {200,250.5,210.5};
    player.position = {190,250.5,209.5};
    const Vec3 eye{190,250.5,210.5};
    ClassicCombat physical{seed}, visible{seed};
    check(physical.attack(map,player,std::span{&target,1},rifle,3,true,0).hits.empty(),
          "physical climb anchor is above visible target");
    const auto visible_hit = visible.attack(map,player,std::span{&target,1},rifle,3,true,0,eye);
    check(visible_hit.hits.size()==1 && visible_hit.hits.front().part==1,
          "climb-smoothed eye ray meets visible head");

    check(map.set_voxel(252,250,233,{80,90,100,255}), "climb build wall");
    player.position = {250.5,250.5,232.5};
    const Vec3 build_eye{250.5,250.5,233.5};
    check(!classic_build_target(map,player,{}), "physical anchor misses the aimed wall");
    const auto cell = classic_build_target(map,player,{},false,build_eye);
    check(cell && cell->x==251 && cell->z==233, "build cursor uses visible eye");
    check(map.clear_voxel(252,250,233), "remove climb build wall");
}

void pillar_tests(VxlMap& map) {
    for (unsigned x = 248; x <= 252; ++x) for (unsigned y = 248; y <= 252; ++y)
        check(map.set_voxel(x,y,236,{80,90,100,255}), "pillar floor");
    PlayerMovementState player;
    player.position = {250.5,250.5,233.75}; player.orientation = {0,0,1};
    bool jump_held{};
    ClassicBlockPlacement pending;
    PlayerInputState input;
    input.jump = true;
    unsigned builds{};
    for (unsigned frame = 0; frame < 120; ++frame) {
        static_cast<void>(step_classic_player(player,input,&map,1.0/60,true,jump_held));
        if (frame == 0) check(!pending.request(map,player,{}), "first airborne click waits for feet");
        if (const auto cell = pending.update(map,player,{},true)) {
            ++builds;
            check(cell->x==250 && cell->y==250 && cell->z==235 && player.position.z+2.25<=235,
                  "pending pillar clears physical feet before one placement");
            check(map.set_voxel(cell->x,cell->y,cell->z,{80,90,100,255}), "server accepts pillar");
        }
        check(std::abs(player.velocity.z) < 0.6, "pillar does not create vertical launch velocity");
    }
    check(builds==1 && !player.airborne && std::abs(player.position.z-232.75)<0.1,
          "one early click lands on the new pillar while held jump cannot repeat");
    check(map.clear_voxel(250,250,235), "reset pillar");

    player.position.z = 232.7; player.airborne = true;
    const Vec3 climb_eye{250.5,250.5,233.6};
    check(classic_build_target(map,player,{},false,climb_eye).has_value(),
          "smoothed camera must not become the physical feet-overlap anchor");
    player.position.z = 233.6;
    check(!pending.request(map,player,{}), "queue supported pillar");
    check(map.clear_voxel(250,250,236), "remove pillar support");
    player.position.z = 232.7;
    check(!pending.update(map,player,{},true), "lost support cancels deferred placement");
    check(map.set_voxel(250,250,236,{80,90,100,255}), "restore pillar support");
    check(!pending.update(map,player,{},true), "restoring support cannot revive canceled click");

    player.position.z = 233.6;
    check(!pending.request(map,player,{}), "queue second pillar");
    player.position.z = 232.7;
    const ClassicHitTarget other{9,{250.5,250.5,233.7},{1,0,0},false,false};
    check(!pending.update(map,player,std::span{&other,1},true), "another player blocks pending placement");
    check(pending.update(map,player,{},true).has_value(), "queued click resumes when other player leaves");
    check(!pending.update(map,player,{},true), "resumed click emits only once");
}
} // namespace

int main() {
    try {
        auto map = flat_map();
        hitbox_reference_tests(map);
        range_and_eye_tests(map);
        pillar_tests(map);
        std::cout << "Classic combat and pillar regressions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
