#include "battlespades/world/body_damage.hpp"
#include "battlespades/world/particle_system.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace battlespades::world {
namespace {
double squared(Vec3 a, Vec3 b) { return (a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z); }
Vec3 center(const ChunkMesh& mesh, std::size_t i) {
    Vec3 result{};
    for (std::size_t j=0;j<4;++j) { const auto& v=mesh.vertices[i+j]; result.x+=v.x*.25; result.y+=v.y*.25; result.z+=v.z*.25; }
    return result;
}
Vec3 transform(Vec3 p, const ClassicCorpse::Matrix& m) {
    return {p.x*m[0]+p.y*m[4]+p.z*m[8]+m[12],p.x*m[1]+p.y*m[5]+p.z*m[9]+m[13],p.x*m[2]+p.y*m[6]+p.z*m[10]+m[14]};
}
std::uint8_t geometric_face(const ChunkMesh& mesh, std::size_t i) {
    // Composite native bodies have already changed coordinate basis. Their
    // original face/material tags must not be treated as geometric normals.
    const auto& a=mesh.vertices[i];const auto& b=mesh.vertices[i+1];const auto& c=mesh.vertices[i+2];
    const Vec3 u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};
    const Vec3 n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
    if(std::abs(n.x)>=std::abs(n.y) && std::abs(n.x)>=std::abs(n.z)) return n.x<0?0:1;
    if(std::abs(n.y)>=std::abs(n.z)) return n.y<0?2:3;
    return n.z<0?4:5;
}
using Cell=std::array<int,3>;
constexpr std::array<Cell,6> normals{{{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}}};
constexpr std::array<std::array<Cell,4>,6> corners{{
    {{{0,0,0},{0,0,1},{0,1,1},{0,1,0}}}, {{{1,0,0},{1,1,0},{1,1,1},{1,0,1}}},
    {{{0,0,0},{1,0,0},{1,0,1},{0,0,1}}}, {{{0,1,0},{0,1,1},{1,1,1},{1,1,0}}},
    {{{0,0,0},{0,1,0},{1,1,0},{1,0,0}}}, {{{0,0,1},{1,0,1},{1,1,1},{0,1,1}}}
}};
struct VoxelVolume {
    double cell{};
    std::array<float,3> origin{};
    Cell dimensions{};
    std::vector<Cell> owners;
    std::vector<std::uint8_t> occupancy;

    Cell grid(Vec3 at) const {
        return {static_cast<int>(std::floor((at.x-origin[0])/cell+1e-4)),
                static_cast<int>(std::floor((at.y-origin[1])/cell+1e-4)),
                static_cast<int>(std::floor((at.z-origin[2])/cell+1e-4))};
    }
    Vec3 cell_center(Cell c) const {
        return {origin[0]+(c[0]+.5)*cell,origin[1]+(c[1]+.5)*cell,origin[2]+(c[2]+.5)*cell};
    }
    bool inside(Cell c) const {
        return c[0]>=-1 && c[1]>=-1 && c[2]>=-1 &&
               c[0]<dimensions[0]-1 && c[1]<dimensions[1]-1 && c[2]<dimensions[2]-1;
    }
    std::size_t offset(Cell c) const {
        return static_cast<std::size_t>((c[2]+1)*dimensions[1]+c[1]+1) *
                   static_cast<std::size_t>(dimensions[0]) + static_cast<std::size_t>(c[0]+1);
    }
    bool solid(Cell c) const {return !occupancy.empty() && inside(c) && occupancy[offset(c)]!=2;}
    bool exposed(const ChunkMesh& mesh, std::size_t i) const {
        if(occupancy.empty()) return true;
        auto neighbor=owners[i/4];const auto n=normals[geometric_face(mesh,i)];
        for(std::size_t a=0;a<3;++a) neighbor[a]+=n[a];
        return !solid(neighbor);
    }
    explicit VoxelVolume(const ChunkMesh& source) {
        if(source.vertices.size()<4) return;
        const auto& first=source.vertices[0];const auto& second=source.vertices[1];
        cell=std::sqrt(squared({first.x,first.y,first.z},{second.x,second.y,second.z}));
        if(cell<1e-6 || !std::isfinite(cell)) return;
        origin=source.minimum;
        std::size_t volume=1;
        for(std::size_t axis=0;axis<3;++axis) {
            const double extent=(source.maximum[axis]-origin[axis])/cell;
            if(!std::isfinite(extent) || extent<0 || extent>126) return;
            dimensions[axis]=static_cast<int>(std::lround(extent))+2;
            volume*=static_cast<std::size_t>(dimensions[axis]);
        }
        constexpr std::size_t maximum_volume=262144;
        if(volume>maximum_volume) return;
        owners.reserve(source.vertices.size()/4);
        for(std::size_t i=0;i+3<source.vertices.size();i+=4) {
            for(std::size_t j=0;j<4;++j) {
                const auto& v=source.vertices[i+j];const std::array xyz{v.x,v.y,v.z};
                for(std::size_t axis=0;axis<3;++axis) {
                    const auto coordinate=(xyz[axis]-origin[axis])/cell;
                    if(!std::isfinite(coordinate) || std::abs(coordinate-std::round(coordinate))>=.002) return;
                }
            }
            const auto at=center(source,i);const auto n=normals[geometric_face(source,i)];
            owners.push_back(grid({at.x-n[0]*cell*.5,at.y-n[1]*cell*.5,at.z-n[2]*cell*.5}));
        }
        // KV6 stores surface voxels and inner-facing skin. Fill only enclosed
        // air: this preserves exterior concavities and separate model pieces.
        // The padded grid is temporary, bounded, and never built per idle frame.
        occupancy.resize(volume);
        for(const auto owner:owners) if(inside(owner)) occupancy[offset(owner)]=1;
        std::vector<Cell> exterior;exterior.reserve(volume);
        exterior.push_back({-1,-1,-1});occupancy[0]=2;
        for(std::size_t i=0;i<exterior.size();++i) for(const auto n:normals) {
            auto neighbor=exterior[i];for(std::size_t a=0;a<3;++a) neighbor[a]+=n[a];
            if(!inside(neighbor)) continue;
            auto& state=occupancy[offset(neighbor)];if(state) continue;
            state=2;exterior.push_back(neighbor);
        }
    }
};
std::uint32_t mix(std::uint32_t x) { x^=x>>16; x*=0x7feb352dU; x^=x>>15; x*=0x846ca68bU; return x^(x>>16); }
}
void BodyDamage::hit(Vec3 position, std::uint32_t seed, double radius, std::uint8_t group,
                     std::uint8_t intensity, bool cut) {
    if (!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z)) return;
    if (!std::isfinite(radius) || radius<=0) return;
    if (group!=255 && pending_count_==pending_.size()) {
        std::move(pending_.begin()+1,pending_.end(),pending_.begin());
        --pending_count_;
    }
    if (pending_count_<pending_.size()) pending_[pending_count_++]={position,seed,std::min(radius,.45),group,std::min<std::uint8_t>(intensity,3),cut};
}
BodyDamage::Chips BodyDamage::resolve(std::span<const Surface> surfaces) {
    Chips chips;
    if(!pending_count_) return chips;
    std::vector<VoxelVolume> volumes;
    volumes.reserve(surfaces.size());
    for(const auto& surface:surfaces) {
        if(surface.mesh) volumes.emplace_back(*surface.mesh);
        else volumes.emplace_back(ChunkMesh{});
    }
    for (std::size_t p=0;p<pending_count_;++p) {
        const auto& contact=pending_[p];
        double closest=.75*.75;
        std::optional<Wound> wound;
        Chip chip{};
        for (std::size_t surface_index=0;surface_index<surfaces.size();++surface_index) {
            const auto& surface=surfaces[surface_index];
            if (!surface.mesh || surface.group>=group_revisions_.size()) continue;
            if (contact.group!=255 && contact.group!=surface.group) continue;
            const auto& m=surface.transform;
            const double scale=std::sqrt(m[0]*m[0]+m[1]*m[1]+m[2]*m[2]);
            if (scale<1e-6) continue;
            for (std::size_t i=0;i+3<surface.mesh->vertices.size();i+=4) {
                // Contacts may originate inside the broad gameplay body box.
                // Never select an inward KV6 face: its reversed normal would
                // carve outward through the helmet instead of into the body.
                if(!volumes[surface_index].exposed(*surface.mesh,i)) continue;
                const auto local=center(*surface.mesh,i);
                const auto world=transform(local,m);
                const auto distance=squared(world,contact.point);
                if (distance>=closest) continue;
                closest=distance;
                wound=Wound{local,contact.radius/scale,surface.group,contact.seed,contact.intensity,
                            geometric_face(*surface.mesh,i),contact.cut};
                chip=Chip{world,surface.mesh->vertices[i].abgr,contact.seed,
                          surface.group!=0 && contact.intensity>=2};
            }
        }
        if (wound) {
            chips.values[chips.count++]=chip;
            if(count_==wounds_.size()) ++group_revisions_[wounds_[next_].group];
            ++group_revisions_[wound->group];
            wounds_[next_]=*wound; next_=(next_+1)%wounds_.size();
            count_=std::min(count_+1,wounds_.size()); ++revision_;
        }
    }
    pending_count_=0;
    return chips;
}
bool BodyDamage::marked(std::uint8_t group) const noexcept {
    return std::any_of(wounds_.begin(),wounds_.begin()+count_,[group](const auto& w){return w.group==group;});
}
ChunkMesh BodyDamage::mesh(const ChunkMesh& source, std::uint8_t group) const {
    if(source.vertices.size()<4 || !marked(group)) return source;
    const VoxelVolume volume(source);
    if(volume.occupancy.empty()) {
        // Non-voxel custom geometry still gets one surface tint, never a
        // second overlapping decal or an unsafe approximation of its volume.
        auto result=source;
        for(std::size_t i=0;i+3<result.vertices.size();i+=4) for(std::size_t w=0;w<count_;++w) {
            if(wounds_[w].group!=group || squared(center(source,i),wounds_[w].point)>wounds_[w].radius*wounds_[w].radius) continue;
            for(std::size_t j=0;j<4;++j) result.vertices[i+j].abgr=0x000c0c60U;
            break;
        }
        return result;
    }
    const auto cell=volume.cell;
    const auto origin=volume.origin;
    const auto& owners=volume.owners;
    const auto& first=source.vertices[0];
    const auto grid=[&](Vec3 at) {return volume.grid(at);};
    const auto cell_center=[&](Cell c) {return volume.cell_center(c);};
    const auto solid=[&](Cell c) {return volume.solid(c);};
    struct Hash {
        std::size_t operator()(const Cell& c) const noexcept {
            return static_cast<std::uint32_t>(c[0])*73856093U ^ static_cast<std::uint32_t>(c[1])*19349663U ^ static_cast<std::uint32_t>(c[2])*83492791U;
        }
    };
    // Limit both sparse storage and CPU work. Even pathological skins cannot
    // turn a bullet into whole-body destruction or unbounded geometry growth.
    constexpr std::size_t maximum_removed=1024, maximum_probes=32768;
    std::unordered_map<Cell,std::size_t,Hash> removed;removed.reserve(maximum_removed);
    std::size_t probes{};
    for(std::size_t w=0;w<count_ && probes<maximum_probes;++w) {
        const auto& wound=wounds_[w];if(wound.group!=group) continue;
        const auto n=normals[wound.face];const auto axis=static_cast<std::size_t>(wound.face/2U);
        const double thickness=source.maximum[axis]-origin[axis];
        const int layers=std::max(1,std::min(static_cast<int>(std::array{1U,2U,4U,5U}[wound.intensity]),static_cast<int>(thickness*.42/cell)));
        const double radius=std::min(wound.radius,cell*10);
        auto middle=grid(wound.point);
        const std::array point{wound.point.x,wound.point.y,wound.point.z};
        middle[axis]=static_cast<int>(std::lround((point[axis]-origin[axis])/cell));
        const int reach=static_cast<int>(std::ceil(radius/cell))+1;
        for(int u=-reach;u<=reach && probes<maximum_probes;++u)
        for(int v=-reach;v<=reach && probes<maximum_probes;++v)
        for(int depth=0;depth<layers && probes<maximum_probes;++depth) {
            ++probes;
            Cell c=middle;c[(axis+1)%3]+=u;c[(axis+2)%3]+=v;
            // On a positive-facing plane grid() points at the outside cell.
            c[axis]-=n[axis]*(depth+(n[axis]>0?1:0));
            const auto at=cell_center(c);
            const std::array d{at.x-wound.point.x,at.y-wound.point.y,at.z-wound.point.z};
            const double radial=std::sqrt(d[(axis+1)%3]*d[(axis+1)%3]+d[(axis+2)%3]*d[(axis+2)%3])/radius;
            const double rim=.87+(mix(wound.seed+static_cast<std::uint32_t>(Hash{}(c)))%14)/100.;
            const int permitted=wound.cut?1:std::max(1,static_cast<int>(std::ceil(layers*(1-radial*.6))));
            if(radial>rim || depth>=permitted || !solid(c)) continue;
            const auto existing=removed.find(c);
            if(existing!=removed.end()) existing->second=w;
            else if(removed.size()<maximum_removed) removed.emplace(c,w);
        }
    }
    if(removed.empty()) return source;
    ChunkMesh result;result.key=source.key;result.minimum=source.minimum;result.maximum=source.maximum;
    result.vertices.reserve(source.vertices.size());result.indices.reserve(source.indices.size());
    const auto quad=[&](std::array<ChunkVertex,4> vertices) {
        const auto base=static_cast<std::uint32_t>(result.vertices.size());
        result.vertices.insert(result.vertices.end(),vertices.begin(),vertices.end());
        for(const auto index:{0U,1U,2U,0U,2U,3U}) result.indices.push_back(base+index);
    };
    for(std::size_t face=0;face<owners.size();++face) {
        if(removed.contains(owners[face])) continue;
        const auto i=face*4;
        auto neighbor=owners[face];const auto normal=normals[geometric_face(source,i)];
        for(std::size_t axis=0;axis<3;++axis) neighbor[axis]+=normal[axis];
        if(solid(neighbor)) continue; // hidden faces between overlapping authored shells
        quad({source.vertices[i],source.vertices[i+1],source.vertices[i+2],source.vertices[i+3]});
    }
    // Only a removed/retained boundary needs an interior face. Adjacent holes
    // merge naturally, so no skin, tissue or bone surfaces share a plane.
    for(const auto& [cut,w]:removed) for(std::uint8_t direction=0;direction<6;++direction) {
        Cell neighbor=cut;for(std::size_t a=0;a<3;++a) neighbor[a]+=normals[direction][a];
        if(removed.contains(neighbor) || !solid(neighbor)) continue;
        const auto face=static_cast<std::uint8_t>(direction^1U);const auto n=normals[face];
        const auto& wound=wounds_[w];const auto axis=static_cast<std::size_t>(wound.face/2U);
        const auto at=cell_center(neighbor);const std::array d{at.x-wound.point.x,at.y-wound.point.y,at.z-wound.point.z};
        const double radial=std::sqrt(d[(axis+1)%3]*d[(axis+1)%3]+d[(axis+2)%3]*d[(axis+2)%3])/wound.radius;
        const double depth=std::abs(d[axis])/cell;
        const bool bone=wound.intensity>0 && (group==1 ? (wound.cut?radial<.5:depth<2.0 || radial>.65) : group>=2 && radial<.5);
        const auto shade=static_cast<std::uint32_t>(mix(wound.seed+static_cast<std::uint32_t>(Hash{}(neighbor)))%18U);
        const std::uint32_t color=bone?0x0091b7d5U:radial>.8?0x00122080U:(0x00090930U+shade);
        std::array<ChunkVertex,4> vertices;
        for(std::size_t j=0;j<4;++j) {
            auto vertex=first;vertex.x=static_cast<float>(origin[0]+(neighbor[0]+corners[face][j][0])*cell);
            vertex.y=static_cast<float>(origin[1]+(neighbor[1]+corners[face][j][1])*cell);
            vertex.z=static_cast<float>(origin[2]+(neighbor[2]+corners[face][j][2])*cell);
            vertex.face=face;vertex.ao_u=static_cast<float>(n[0]);vertex.ao_v=static_cast<float>(n[1]);vertex.edge_u=static_cast<float>(n[2]);
            vertex.abgr=color;vertices[j]=vertex;
        }
        quad(vertices);
    }
    return result;
}
GoreProfile gore_profile(std::uint8_t intensity) noexcept {
    constexpr std::array<GoreProfile,4> profiles{{{.095,3},{.16,6},{.24,10},{.32,16}}};
    return profiles[std::min<std::size_t>(intensity,profiles.size()-1)];
}
void emit_body_fragments(ParticleSystem& particles, const BodyDamage::Chips& chips, std::uint8_t intensity) {
        const auto profile=gore_profile(intensity);
        // Bound a burst even when every pellet lands in one frame. Existing
        // particle pooling, collisions and lifetime own the debris simulation.
        const auto fragments=static_cast<unsigned>(std::min<std::size_t>(profile.fragments,32/std::max<std::size_t>(1,chips.count)));
        for (const auto& chip:chips) {
            ParticleSpawn fragment;
            fragment.position={static_cast<float>(chip.position.x),static_cast<float>(chip.position.y),static_cast<float>(chip.position.z)};
            fragment.color={static_cast<std::uint8_t>(chip.color),static_cast<std::uint8_t>(chip.color>>8U),static_cast<std::uint8_t>(chip.color>>16U),255U};
            fragment.size_begin=.06F; fragment.size_end=.045F; fragment.lifetime=1.8F;
            fragment.explode_velocity=.07F; fragment.gravity_scale=1.F;
            fragment.alpha_begin=1.F; fragment.alpha_end=0.F;
            fragment.atlas=ParticleAtlas::tumbling_cube; fragment.collide=true;
            particles.emit_burst(fragment,fragments,chip.seed);
            if(chip.bone) {
                fragment.color={213U,183U,145U,255U};
                fragment.size_begin=.045F;fragment.size_end=.035F;fragment.lifetime=2.2F;
                particles.emit_burst(fragment,2U,chip.seed^0x52424f4eU);
            }
        }
}
bool sever_on_impact(GoreWeapon weapon, ClassicCorpse::Part part, double distance,
                     bool lethal, std::uint32_t seed, std::uint8_t intensity) noexcept {
    if (!intensity || part==ClassicCorpse::torso || part>=ClassicCorpse::part_count || !std::isfinite(distance) || distance<0) return false;
    intensity=std::min<std::uint8_t>(intensity,3);
    double chance{};
    if (weapon==GoreWeapon::explosive) chance=std::array{0.,.55,.85,1.}[intensity];
    else if (weapon==GoreWeapon::shotgun && distance<16) {
        const double close=std::array{0.,0.,4.,8.}[intensity];
        const double falloff=std::clamp((16-distance)/(16-close),0.,1.);
        chance=(part==ClassicCorpse::skull?std::array{0.,.12,.85,1.}:std::array{0.,.48,.95,1.})[intensity]*falloff;
    } else if (weapon==GoreWeapon::sniper && lethal && part!=ClassicCorpse::skull)
        chance=std::array{0.,.08,.22,.45}[intensity];
    else if(weapon==GoreWeapon::ordinary && intensity==3 && part!=ClassicCorpse::skull)
        chance=.08;
    return static_cast<double>(mix(seed)%10000U)<chance*10000;
}
} // namespace battlespades::world
