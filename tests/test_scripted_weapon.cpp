#include "battlespades/world/scripted_weapon.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
int main(int argc,char** argv){
    if(argc<2){std::cerr<<"Pass a skin manifest\n";return 2;}
    battlespades::world::ScriptedWeapon skin;std::string error;
    if(!skin.load(argv[1],error)){std::cerr<<error;return 1;}
    battlespades::world::ScriptedWeaponMotion motion;
    std::vector<battlespades::world::SkinModelDraw> previous_models;
    for(int n=0;n<720;++n){battlespades::world::SkinInput input;
        input.dt=1.F/60.F;input.aim=n<60?static_cast<float>(n)/60.F:1.F;
        input.reloading=n>=90&&n<210;input.reload_progress=input.reloading?static_cast<float>(n-90)/120.F:1.F;
        input.fired=n==65;input.reload_started=n==90;input.reload_finished=n==210;
        if(n>=240){
            input.aim=n<400?0.F:std::clamp(static_cast<float>(n-400)/50.F,0.F,1.F);
            input.sprint=n<360||n>=480?1.F:0.F;
            input.swing={.03F*std::sin(n*.05F),.03F*std::cos(n*.05F),n>=360&&n<540?-3.F:0.F};
            motion.apply(input,n<600);
            if(std::abs(input.swing[2])>.061F||std::abs(input.swing[0])>.061F){std::cerr<<"Unbounded motion";return 1;}
            if(input.aim==1.F&&(input.sprint!=0.F||input.swing[0]!=0.F||input.swing[2]!=0.F)){
                std::cerr<<"Sprint/fall displaced the aimed sight";return 1;
            }
            if(n>=600&&(input.sprint!=0.F||input.swing!=std::array<float,3>{})){std::cerr<<"Motion disable failed";return 1;}
        }
        if(!skin.update(input,error)){std::cerr<<"Frame "<<n<<": "<<error;return 1;}
        for(const auto& d:skin.frame().models)for(const float v:d.transform)if(!std::isfinite(v))return 1;
        if(n>240&&n<600&&previous_models.size()==skin.frame().models.size()){
            for(std::size_t id=0;id<previous_models.size();++id){
                const auto& old=previous_models[id];const auto& next=skin.frame().models[id];
                if(old.resource!=next.resource||skin.resources()[next.resource].arm_part==battlespades::world::SkinArmPart::none)continue;
                float alignment{};for(std::size_t k=4;k<7;++k)alignment+=old.transform[k]*next.transform[k];
                if(alignment<-.0005F){std::cerr<<"Arm rolled abruptly during movement at frame "<<n;return 1;}
            }
        }
        previous_models=skin.frame().models;
        std::size_t shoulder=0;
        std::size_t wrist=0;
        // Scripts may deliberately hide either hand (zero or off-screen target).
        // Match the visible arms to the same authored-side filter as the rig.
        std::vector<std::size_t> visible_sides;
        const std::array authored_hands{skin.frame().left_hand,skin.frame().right_hand};
        for(std::size_t side=0;side<2;++side){
            const auto& h=authored_hands[side];const float length=h[0]*h[0]+h[1]*h[1]+h[2]*h[2];
            if(std::ranges::all_of(h,[](float v){return std::isfinite(v);})&&length>=1e-8F&&length<=4.F)visible_sides.push_back(side);
        }
        for(const auto& d:skin.frame().models){
            if(skin.resources()[d.resource].arm_part==battlespades::world::SkinArmPart::lower){
                if(wrist>=visible_sides.size()){std::cerr<<"Unexpected visible wrist";return 1;}
                const auto& hand=authored_hands[visible_sides[wrist++]];
                const std::array expected{-hand[0],-hand[2],-hand[1]};
                for(std::size_t k=0;k<3;++k)if(std::abs(d.transform[12+k]+d.transform[8+k]*10.F-expected[k])>1e-4F){
                    std::cerr<<"Wrist detached from authored grip at frame "<<n;return 1;
                }
            }
            if(skin.resources()[d.resource].arm_part!=battlespades::world::SkinArmPart::upper)continue;
            // OpenSpades' left shoulder (+.4 source X) must be on screen LEFT.
            const auto& m=d.transform;
            if(shoulder>=visible_sides.size()){std::cerr<<"Unexpected visible shoulder";return 1;}
            const float expected_x=visible_sides[shoulder++]==0?-.4F:.4F;
            const float determinant=m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]);
            if(std::abs(m[12]-expected_x)>1e-6F||std::abs(m[13]+.25F)>1e-6F||determinant<=0){
                std::cerr<<"Mirrored OpenSpades shoulder/attachment at frame "<<n;return 1;
            }
        }
        if(n%120==0)std::cout<<n<<" models="<<skin.frame().models.size()<<" sprites="<<skin.frame().sprites.size()<<" hand="<<skin.frame().left_hand[0]<<","<<skin.frame().left_hand[1]<<","<<skin.frame().left_hand[2]<<"\n";
    }
    // A tiny ADS change must not discard a whole accumulated movement spring.
    // Freeze script time so this measures the transition, not animation advance.
    battlespades::world::SkinInput transition;transition.dt=1.F/60.F;transition.swing={.015F,0.F,-.035F};
    for(int n=0;n<120;++n)if(!skin.update(transition,error)){std::cerr<<error;return 1;}
    transition.dt=0.F;
    if(!skin.update(transition,error))return 1;
    const auto hip_models=skin.frame().models;
    transition.aim=.00001F;
    if(!skin.update(transition,error))return 1;
    if(hip_models.size()==skin.frame().models.size())for(std::size_t id=0;id<hip_models.size();++id){
        const auto& before=hip_models[id];const auto& after=skin.frame().models[id];
        if(before.resource!=after.resource||skin.resources()[after.resource].arm_part!=battlespades::world::SkinArmPart::none)continue;
        for(std::size_t k=0;k<16;++k)if(std::abs(before.transform[k]-after.transform[k])>.005F){
            std::cerr<<"Movement snaps on first ADS input: "<<skin.resources()[after.resource].name;return 1;
        }
    }
    for(const auto& r:skin.resources()){
        if(r.kind==0&&!r.path.empty()){
            const auto model=battlespades::world::Kv6Model::load_file(r.path);
            if(!model){std::cerr<<"Missing model "<<r.name;return 1;}
            const auto raw=model->mesh(),source=battlespades::world::ScriptedWeapon::source_mesh(*model);
            if(raw.indices!=source.indices){std::cerr<<"Source conversion reversed winding";return 1;}
            const std::array<std::array<float,3>,6> normals{{{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}}};
            for(std::size_t v=0;v<raw.vertices.size();++v){const auto a=normals[raw.vertices[v].face],b=normals[source.vertices[v].face];
                if(b!=std::array<float,3>{a[0],a[2],-a[1]}){std::cerr<<"Source normals must follow the asset rotation";return 1;}}
            for(const auto& v:source.vertices){const std::array xyz{v.x,v.y,v.z};for(std::size_t k=0;k<3;++k)
                if(xyz[k]<source.minimum[k]-1e-5F||xyz[k]>source.maximum[k]+1e-5F){std::cerr<<"Invalid source mesh bounds";return 1;}}
        }
        std::cout<<r.kind<<" "<<r.name<<" -> "<<r.path.string()<<"\n";
    }
    return 0;
}
