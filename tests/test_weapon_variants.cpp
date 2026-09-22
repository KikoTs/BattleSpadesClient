#include "battlespades/world/scripted_weapon.hpp"
#include "battlespades/world/weapon_zoom.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>

using namespace battlespades::world;
void expect(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
bool draws_model(const ScriptedWeapon& skin,const std::string& name){
    return std::ranges::any_of(skin.frame().models,[&](const auto& draw){return skin.resources()[draw.resource].name==name;});
}
void settle(ScriptedWeapon& skin,SkinInput input){
    std::string error;input.dt=1.F/60.F;
    for(int n=0;n<120;++n)expect(skin.update(input,error),error);
}
void check_runtime_lifetimes(const std::filesystem::path& packs){
    const auto directory=std::filesystem::temp_directory_path()/
        ("aos-skin-runtime-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    struct Cleanup {std::filesystem::path directory;~Cleanup(){std::error_code ec;std::filesystem::remove_all(directory,ec);}} cleanup{directory};
    std::filesystem::copy_file(packs.parent_path()/"runtime/openspades.as",directory/"runtime.as");
    {std::ofstream script{directory/"fixture.as"};script<<R"(
namespace spades {
class FixtureSkin {
    Renderer@ renderer;
    float SprintState,RaiseState,AimDownSightState,ReadyState,ReloadProgress;
    bool IsMuted,IsReloading;int Ammo,ClipSize,model,sound;
    Vector3 Swing,TeamColor,LeftHandPosition,RightHandPosition;Matrix4 EyeMatrix;
    FixtureSkin(Renderer@ r,AudioDevice@ a){@renderer=r;model=NativeResource("model",0);sound=NativeResource("sound",2);}
    void Update(float dt){} void WeaponFired(){} void ReloadingWeapon(){} void ReloadedWeapon(){}
    void AddToScene(){
        array<float> matrix(16);matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;
        NativeModel(model,matrix);NativeSound(sound,1,1);
        NativeSound(sound,sqrt(-1.0f),1);NativeSound(-1,1,1);
    }
    void Draw2D(){Image@ image=renderer.RegisterImage("image");renderer.DrawImage(image,Vector2(0,0),Vector2(image.Width,image.Height));}
}
})";}
    const auto manifest=directory/"skin.json";
    {std::ofstream output{manifest};output<<R"({"class":"FixtureSkin","category":"SMG","scripts":["runtime.as","fixture.as"],"resources":{"model":"","sound":"","image":"image.png"}})";}
    const auto write_header=[&](unsigned char width,unsigned char height,bool valid=true){
        std::array<unsigned char,24> header{137,'P','N','G',13,10,26,10,0,0,0,13,'I','H','D','R'};
        header[19]=width;header[23]=height;if(!valid)header[12]='B';
        std::ofstream output{directory/"image.png",std::ios::binary};
        output.write(reinterpret_cast<const char*>(header.data()),static_cast<std::streamsize>(header.size()));
    };
    write_header(64,32);
    ScriptedWeapon skin;std::string error;SkinInput input;input.dt=1.F/60.F;
    expect(skin.load(manifest,error),error);expect(skin.update(input,error),error);
    const auto* models=skin.frame().models.data();const auto* sprites=skin.frame().sprites.data();const auto* sounds=skin.frame().sounds.data();
    const auto dimensions=[&](float width,float height){
        expect(skin.frame().sprites.size()==1U&&skin.frame().sprites.front().radius==width&&
            skin.frame().sprites.front().position[2]==height,"Skin image header cache returned incorrect dimensions");
    };
    dimensions(64,32);write_header(128,96);
    for(int frame=0;frame<600;++frame){
        expect(skin.update(input,error),error);dimensions(64,32);
        expect(skin.frame().models.data()==models&&skin.frame().sprites.data()==sprites&&skin.frame().sounds.data()==sounds,
               "Steady skin animation discarded draw-buffer capacity");
        expect(skin.frame().sounds.size()==1U&&std::isfinite(skin.frame().sounds.front().gain),
               "Invalid script sound resource or non-finite gain escaped the skin host");
    }
    auto invalid=input;invalid.dt=std::numeric_limits<float>::quiet_NaN();
    expect(!skin.update(invalid,error)&&skin.frame().models.empty(),"Non-finite engine input entered the skin VM");
    expect(skin.update(input,error),error);dimensions(64,32);
    expect(skin.load(manifest,error),error);expect(skin.update(input,error),error);dimensions(128,96);
    write_header(128,96,false);
    expect(skin.load(manifest,error),error);expect(skin.update(input,error),error);dimensions(32,32);
    ScriptedWeaponMotion motion;
    invalid.aim=invalid.sprint=std::numeric_limits<float>::quiet_NaN();invalid.swing.fill(std::numeric_limits<float>::infinity());
    motion.apply(invalid,true);
    for(int frame=0;frame<30;++frame){auto next=input;next.sprint=1.F;next.swing={.01F,.02F,.03F};motion.apply(next,true);
        expect(std::isfinite(next.sprint)&&std::ranges::all_of(next.swing,[](float value){return std::isfinite(value);}),
               "A malformed motion frame permanently poisoned the spring state");}
    std::cout<<"600 runtime frames retained draw buffers and cached PNG dimensions; reload and malformed input recovered\n";
}
int main(int argc,char** argv){
    try{
        expect(argc==2,"Pass the client asset root");
        const auto packs=std::filesystem::path{argv[1]}.parent_path()/"client/cosmetics/packs";
        check_runtime_lifetimes(packs);
        const auto kar=packs/"pack-legacy-kar98-kar98-pak-unpacked-rifle/skin.json";
        const auto mp5=packs/"pack-legacy-mp5k-smg/skin.json";
        const auto kar_options=load_skin_variants(kar),mp5_options=load_skin_variants(mp5);
        expect(kar_options.options.size()==1&&mp5_options.options.size()==2,"Only authored sights and attachments must load");
        std::string error;
        std::size_t combinations{};
        for(const auto& choice:kar_options.options.front().choices){
            ScriptedWeapon skin;expect(skin.load(kar,error,{{"sight",choice.id}}),error);
            SkinInput input;settle(skin,input);
            expect(draws_model(skin,"Models/Weapons/Rifle/ScopeModel.kv6")==choice.scope,"Kar98 attachment must match selected sight");
            input.aim=1.F;settle(skin,input);
            expect((skin.frame().scope_opacity==1.F)==choice.scope,"Kar98 scope aperture must match selected sight");
            if(choice.scope){
                expect(skin.frame().models.empty(),"Scoped ADS must look through the lens rather than the iron sight");
                const std::string image=choice.id=="scope-dot"?"Gfx/scope1.png":choice.id=="scope-cross"?"Gfx/scope2.png":"Gfx/scope3.png";
                expect(std::ranges::any_of(skin.frame().sprites,[&](const auto& sprite){return skin.resources()[sprite.resource].name==image;}),"Wrong Kar98 reticle");
            }else {
                expect(!skin.frame().models.empty(),"Iron sights must remain a visible 3D weapon");
                const auto gun=std::ranges::find_if(skin.frame().models,[&](const auto& draw){return skin.resources()[draw.resource].name=="Models/Weapons/Rifle/1.kv6";});
                expect(gun!=skin.frame().models.end(),"Kar98 receiver must be drawn");
                const std::array local{1.F/.14F+.5F,6.F/.14F-20.F,1.F/.14F};
                const auto& m=gun->transform;const auto& hand=skin.frame().left_hand;
                const std::array expected{-hand[0],-hand[2],-hand[1]};
                for(std::size_t k=0;k<3;++k)expect(std::abs(m[k]*local[0]+m[4+k]*local[1]+m[8+k]*local[2]+m[12+k]-expected[k])<1e-4F,
                    "Kar98 iron-sight alignment must move the grip with the receiver");
            }
            input.reloading=true;input.reload_progress=.35F;expect(skin.update(input,error),error);
            expect(!skin.frame().models.empty()&&skin.frame().scope_opacity==0.F,"Kar98 reload must reveal the weapon on the first frame even with aim held");
            ++combinations;
        }
        for(const auto& choice:mp5_options.options.front().choices)for(const bool suppressed:{false,true}){
            ScriptedWeapon skin;expect(skin.load(mp5,error,{{"sight",choice.id},{"muzzle",suppressed?"suppressed":"standard"}}),error);
            SkinInput input;settle(skin,input);
            expect(draws_model(skin,"Models/Weapons/SMG/Suppressor.kv6")==suppressed,"Muzzle geometry must follow variant");
            expect(draws_model(skin,"Models/Weapons/SMG/DotSight.kv6")== (choice.id=="red-dot"),"Red-dot model must follow selected sight");
            expect(draws_model(skin,"Models/Weapons/SMG/ScopeModel.kv6")==choice.scope,"Every MP5K scope reticle must have a mounted scope in hip view and inventory preview");
            expect(draws_model(skin,"Models/Weapons/SMG/ironSight.kv6")== (choice.id=="iron"),"Rear iron sight must follow selected attachment");
            input.fired=true;expect(skin.update(input,error),error);input.fired=false;
            expect(!skin.frame().sounds.empty(),"Firing variant must emit its authored sample");
            const auto& sound=skin.resources()[skin.frame().sounds.front().resource].name;
            expect((sound.find("spV2Local")!=std::string::npos)==suppressed,"Suppressor must select suppressed firing audio");
            input.aim=1;settle(skin,input);
            expect((skin.frame().scope_opacity==1)==choice.scope,"MP5K scope mode must follow variant");
            expect(skin.frame().models.empty()==choice.scope,"MP5K scope must show the reticle view");
            input.reloading=true;input.reload_progress=.35F;expect(skin.update(input,error),error);
            expect(!skin.frame().models.empty()&&skin.frame().scope_opacity==0.F,"MP5K reload must reveal the weapon on the first frame even with aim held");
            ++combinations;
        }
        const auto temporary=std::filesystem::path{"tmp/weapon-variant-preferences-test.json"};
        // A unique item namespace keeps this independent from the player's settings.
        SkinVariantPreferences preferences{temporary};
        expect(preferences.set("kar98-test",kar_options,{{"sight","iron"},{"zoom","sniper"}},error),error);
        expect(preferences.set("mp5-test",mp5_options,{{"sight","scope-2"},{"muzzle","standard"}},error),error);
        SkinVariantPreferences restored{temporary};expect(restored.load(error),error);
        expect(restored.selection("kar98-test").at("sight")=="iron"&&restored.selection("mp5-test").at("muzzle")=="standard","Per-skin preferences must survive restart independently");
        const auto selected=resolve_skin_variant(kar_options,restored.selection("kar98-test"));
        expect(selected.magnification==kar_options.options.front().choices.front().magnification&&!selected.scope,
               "Old manual zoom must not override the iron sight's automatic magnification");
        expect(!restored.selection("kar98-test").contains("zoom"),"Saving variants must discard retired zoom preferences");
        expect(resolve_skin_variant(kar_options,{{"zoom","sniper"}}).magnification==
                   resolve_skin_variant(kar_options,{}).magnification,"Previously saved zoom must be ignored");
        const auto fallback=resolve_skin_variant(kar_options,{{"sight","not-an-option"},{"zoom","not-an-option"}});
        expect(fallback.scope&&fallback.config.at("png2")=="1","Unknown saved IDs must safely use authored defaults");
        expect(preferences.set("kar98-test",kar_options,{},error),error);
        const auto fixed_sight=std::filesystem::path{"tmp/fixed-sight-test.json"};
        {std::ofstream output{fixed_sight};output<<R"({"category":"Rifle","optic":{"magnification":2.5,"scope":true}})";}
        const auto fixed=load_skin_variants(fixed_sight);
        const auto fixed_choice=resolve_skin_variant(fixed,{{"zoom","sniper"}});
        expect(fixed.options.empty()&&fixed_choice.scope&&fixed_choice.magnification==2.5F,
               "A fixed authored scope must magnify automatically without exposing a zoom selector");
        {std::ofstream output{fixed_sight};output<<R"({"category":"Rifle","optic":{"magnification":500,"scope":true}})";}
        expect(resolve_skin_variant(load_skin_variants(fixed_sight),{}).magnification==0.F,
               "Out-of-range fixed magnification must fall back safely");
        expect(resolve_skin_variant(kar_options,preferences.selection("kar98-test")).scope,"Reset must restore the default optic");
        expect(!preferences.set("../escape",kar_options,{},error),"Invalid item IDs must be rejected");
        const auto preserved=preferences.selection("mp5-test");
        {std::ofstream output{temporary};output<<"broken";}
        expect(!preferences.load(error)&&preferences.selection("mp5-test")==preserved,"A malformed file must not partially replace live choices");
        expect(preferences.set("kar98-test",kar_options,{},error),error);
        for(const float magnification:{1.35F,1.5F,2.F,2.5F,4.F}){
            const auto target=skin_variant_zoom_target(magnification);
            const auto fov=zoom_fov_y_degrees(target);
            const auto ratio=std::tan(75.*std::numbers::pi/360.)/std::tan(fov*std::numbers::pi/360.);
            expect(std::abs(ratio-magnification)<1e-5,"Zoom must magnify the world by its labelled amount");
            double level{};for(int tick=0;tick<180;++tick)level=advance_zoom_level(level,target,18,1./60.);
            expect(std::abs(level-target)<.001,"Skin zoom must use the smooth sniper transition");
        }
        std::ifstream index_file{packs/"index.json"};const auto index=nlohmann::json::parse(index_file);std::size_t guns{};
        for(const auto& item:index){
            // The importer records host paths; keep only the pack folder and file name.
            auto recorded=item.at("manifest").get<std::string>();std::ranges::replace(recorded,'\\','/');
            const auto indexed=std::filesystem::path{recorded};
            const auto manifest=packs/indexed.parent_path().filename()/indexed.filename();
            std::ifstream file{manifest};const auto data=nlohmann::json::parse(file);
            if(data.at("category")=="Spade")continue;
            const auto definition=load_skin_variants(manifest);
            expect(std::ranges::none_of(definition.options,[](const auto& option){return option.id=="zoom";}),
                   "Imported guns must not expose manual aim zoom");
            expect(resolve_skin_variant(definition,{{"zoom","sniper"}}).magnification==
                       resolve_skin_variant(definition,{}).magnification,"Legacy zoom must not affect automatic aiming");++guns;
        }
        std::cout<<combinations<<" authored combinations, "<<guns<<" automatic-zoom gun packs, persistence and magnification passed\n";
        return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}
