#include "battlespades/world/cosmetic_preview.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace battlespades::world {
std::vector<std::uint8_t> cosmetic_preview(Kv6Model model,
        std::optional<std::array<std::uint8_t,3U>> palette,bool blue_team,double yaw,double zoom) {
    if(palette)model.apply_cosmetic_palette(*palette);
    model.apply_default_color(blue_team?VxlColor{72,111,181,255}:VxlColor{79,132,61,255});
    return cosmetic_preview(model.mesh(),yaw,zoom);
}

std::vector<std::uint8_t> cosmetic_preview(const ChunkMesh& mesh,double yaw,double zoom,
                                          const CosmeticPreviewStyle& style) {
    if(!style.width||!style.height||style.width>1024||style.height>1024)return {};
    std::vector<std::uint8_t> result(style.width*style.height*4U,0);
    // Multipart authored guns such as the AWP exceed a single KV6's budget.
    if(mesh.empty()||mesh.vertices.size()>4'000'000U||!std::isfinite(yaw)||!std::isfinite(zoom)||
       !std::isfinite(style.roll)||!std::isfinite(style.padding)||!std::isfinite(style.fit_width)||
       !std::isfinite(style.outline))return result;
    constexpr int samples=2;
    constexpr unsigned usamples=samples;
    const int width=static_cast<int>(style.width)*samples,height=static_cast<int>(style.height)*samples;
    // Pixel buffers are indexed with size_t; coordinates stay signed for the neighbour tests.
    const auto stride=static_cast<std::size_t>(width);
    const auto pixel_count=stride*static_cast<std::size_t>(height);
    const auto at=[stride](int px,int py){return static_cast<std::size_t>(py)*stride+static_cast<std::size_t>(px);};
    struct Vertex {double x,y,z;};
    using Normal=std::array<float,3>;
    std::vector<Vertex> vertices;vertices.reserve(mesh.vertices.size());
    double min_x=1e9,min_y=1e9,max_x=-1e9,max_y=-1e9;
    const auto cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(.25),sp=std::sin(.25);
    const auto cr=std::cos(style.roll),sr=std::sin(style.roll);
    for(const auto& v:mesh.vertices){
        const double rx=v.x*cy+v.z*sy,rz=-v.x*sy+v.z*cy;
        const double ry=-(v.y*cp-rz*sp),depth=v.y*sp+rz*cp;
        const double x=rx*cr-ry*sr,y=rx*sr+ry*cr;
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(depth))return result;
        vertices.push_back({x,y,depth});
        min_x=std::min(min_x,x);max_x=std::max(max_x,x);
        min_y=std::min(min_y,y);max_y=std::max(max_y,y);
    }
    const double fit_width=style.fit_width>0?std::min<double>(style.width,style.fit_width):style.width;
    const auto scale=std::min(std::max(1.,fit_width-2*style.padding)/std::max(.0001,max_x-min_x),
        std::max(1.,style.height-2*style.padding)/std::max(.0001,max_y-min_y))*samples*std::clamp(zoom,.6,1.5);
    for(auto& v:vertices){v.x=(v.x-(min_x+max_x)*.5)*scale+width*.5;
        v.y=(v.y-(min_y+max_y)*.5)*scale+height*.5;v.z*=scale;}
    std::vector<double> depths(pixel_count,-std::numeric_limits<double>::infinity());
    std::vector<Normal> normals(pixel_count);
    std::vector<std::uint8_t> pixels(pixel_count*4,0);
    const auto edge=[](const Vertex& a,const Vertex& b,double x,double y){return (x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x);};
    for(std::size_t i=0;i+2<mesh.indices.size();i+=3){
        if(mesh.indices[i]>=vertices.size()||mesh.indices[i+1]>=vertices.size()||mesh.indices[i+2]>=vertices.size())return result;
        const auto& a=vertices[mesh.indices[i]];const auto& b=vertices[mesh.indices[i+1]];const auto& c=vertices[mesh.indices[i+2]];
        const auto area=edge(a,b,c.x,c.y);if(std::abs(area)<.00001)continue;
        const double ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
        double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
        const auto length=std::sqrt(nx*nx+ny*ny+nz*nz);if(length<1e-12)continue;
        const auto sign=nz<0?-1.:1.;nx*=sign/length;ny*=sign/length;nz*=sign/length;
        const Normal normal{static_cast<float>(nx),static_cast<float>(ny),static_cast<float>(nz)};
        // A fixed top-left studio light separates voxel faces in tiny UI slots.
        const double light=.42+.78*std::max(0.,nx*-.45+ny*-.65+nz*.61);
        std::array<double,3> rgb{};const auto albedo=mesh.vertices[mesh.indices[i]].abgr;
        for(unsigned channel=0;channel<3;++channel)rgb[channel]=std::pow(((albedo>>(channel*8))&255)/255.*light,1./2.2);
        const double luma=rgb[0]*.2126+rgb[1]*.7152+rgb[2]*.0722;
        for(auto& channel:rgb)channel=std::clamp(((luma+(channel-luma)*1.38)*1.12-.45)*1.08+.45,0.,1.);
        const int x0=std::clamp(static_cast<int>(std::floor(std::min({a.x,b.x,c.x}))),0,width-1);
        const int x1=std::clamp(static_cast<int>(std::ceil(std::max({a.x,b.x,c.x}))),0,width-1);
        const int y0=std::clamp(static_cast<int>(std::floor(std::min({a.y,b.y,c.y}))),0,height-1);
        const int y1=std::clamp(static_cast<int>(std::ceil(std::max({a.y,b.y,c.y}))),0,height-1);
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
            const auto u=edge(b,c,x+.5,y+.5)/area,v=edge(c,a,x+.5,y+.5)/area,w=1.-u-v;
            if(u<0||v<0||w<0)continue;
            const auto z=u*a.z+v*b.z+w*c.z;const auto p=at(x,y);
            if(z<=depths[p])continue;
            depths[p]=z;normals[p]=normal;
            for(unsigned channel=0;channel<3;++channel)pixels[p*4+channel]=static_cast<std::uint8_t>(rgb[channel]*255);
            pixels[p*4+3]=255;
        }
    }
    const auto source=pixels;
    const int radius=static_cast<int>(std::ceil(std::clamp(style.outline,0.,6.)*samples));
    // A two-pass distance field keeps outline cost independent of stroke width.
    std::vector<float> distance(pixel_count,static_cast<float>(radius+2));
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const auto p=at(x,y);if(source[p*4+3]){distance[p]=0;continue;}
        if(x)distance[p]=std::min(distance[p],distance[p-1]+1);
        if(y){distance[p]=std::min(distance[p],distance[p-stride]+1);
            if(x)distance[p]=std::min(distance[p],distance[p-stride-1]+1.4142F);
            if(x+1<width)distance[p]=std::min(distance[p],distance[p-stride+1]+1.4142F);}
    }
    for(int y=height-1;y>=0;--y)for(int x=width-1;x>=0;--x){
        const auto p=at(x,y);
        if(x+1<width)distance[p]=std::min(distance[p],distance[p+1]+1);
        if(y+1<height){distance[p]=std::min(distance[p],distance[p+stride]+1);
            if(x)distance[p]=std::min(distance[p],distance[p+stride-1]+1.4142F);
            if(x+1<width)distance[p]=std::min(distance[p],distance[p+stride+1]+1.4142F);}
    }
    // Draw silhouettes into transparent space and crease/occlusion lines on visible
    // geometry. Depth and normals avoid outlining coplanar voxel or triangle seams.
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const auto p=at(x,y);
        if(!source[p*4+3]){
            if(distance[p]<=static_cast<float>(radius)){pixels[p*4]=pixels[p*4+1]=pixels[p*4+2]=8;pixels[p*4+3]=255;}
            continue;
        }
        bool crease=false;
        for(const auto delta:std::array<std::array<int,2>,4>{{{-samples,0},{samples,0},{0,-samples},{0,samples}}}){
            const int xx=x+delta[0],yy=y+delta[1];if(xx<0||xx>=width||yy<0||yy>=height)continue;
            const auto q=at(xx,yy);if(!source[q*4+3])continue;
            const auto& n=normals[p];const auto& m=normals[q];
            const auto dot=n[0]*m[0]+n[1]*m[1]+n[2]*m[2];
            const auto dz=depths[q]-depths[p];
            const auto plane_error=std::abs(n[0]*static_cast<float>(delta[0])+n[1]*static_cast<float>(delta[1])+n[2]*dz);
            if((dot<.65F&&n[2]>m[2]+.08F)||(dz<-2*samples&&plane_error>1.5*samples)){crease=true;break;}
        }
        if(crease)for(unsigned channel=0;channel<3;++channel)pixels[p*4+channel]=static_cast<std::uint8_t>(pixels[p*4+channel]*.18);
    }
    // Alpha-weighted downsampling retains a crisp dark outline without a light halo.
    for(unsigned y=0;y<style.height;++y)for(unsigned x=0;x<style.width;++x){
        unsigned alpha=0;std::array<unsigned,3> rgb{};
        for(unsigned j=0;j<usamples;++j)for(unsigned k=0;k<usamples;++k){
            const auto p=(static_cast<std::size_t>(y*usamples+j)*stride+x*usamples+k)*4;
            const auto a=pixels[p+3];alpha+=a;for(unsigned c=0;c<3;++c)rgb[c]+=pixels[p+c]*a;
        }
        const auto p=(y*style.width+x)*4;
        if(alpha){for(unsigned c=0;c<3;++c)result[p+c]=static_cast<std::uint8_t>(rgb[c]/alpha);result[p+3]=static_cast<std::uint8_t>(alpha/(usamples*usamples));}
    }
    return result;
}
} // namespace battlespades::world
