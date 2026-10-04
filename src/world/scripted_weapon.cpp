#include "battlespades/world/scripted_weapon.hpp"
#include "battlespades/world/cosmetic_files.hpp"
#include "battlespades/world/kv6_model.hpp"
#include <angelscript.h>
#include <scriptarray.h>
#include <scriptmath.h>
#include <scriptstdstring.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace battlespades::world {
namespace {
std::string read(const std::filesystem::path& p) {
    if (std::filesystem::file_size(p)>4U*1024U*1024U) throw std::runtime_error("Skin source too large");
    std::ifstream f(p,std::ios::binary); if(!f) throw std::runtime_error("Missing skin source: "+p.string());
    return {std::istreambuf_iterator<char>(f),{}};
}
std::filesystem::path contained(const std::filesystem::path& root,const std::string& name) {
    auto p=std::filesystem::path(name);if(p.is_absolute()||p.has_root_name())throw std::runtime_error("Invalid skin resource path");
    for(const auto& s:p)if(s=="..")throw std::runtime_error("Invalid skin resource path");
    const auto resolved=std::filesystem::weakly_canonical(root/p);
    const auto relative=resolved.lexically_relative(std::filesystem::weakly_canonical(root));
    if(relative.empty()||*relative.begin()=="..")throw std::runtime_error("Skin resource escapes pack");
    // Retail-identical pack files resolve to the player's imported copy.
    return resolve_cosmetic_file(resolved);
}
}
void ScriptedWeaponMotion::apply(SkinInput& input, bool enabled) {
    const float dt=std::isfinite(input.dt)?std::clamp(input.dt,0.F,.1F):0.F;
    const float aim=std::isfinite(input.aim)?std::clamp(input.aim,0.F,1.F):0.F;
    const float settled=aim*aim*(3.F-2.F*aim);
    const float target=enabled&&!input.reloading&&std::isfinite(input.sprint) ? std::clamp(input.sprint,0.F,1.F) : 0.F;
    sprint_+=std::clamp(target-sprint_,-dt*3.F,dt*4.F);
    input.sprint=enabled?sprint_*(1.F-settled):0.F;
    const float follow=1.F-std::exp(-dt*14.F);
    // A fall can produce offsets several times the arm's reach in the classic
    // coordinate system. Preserve the lift with bounded, continuous easing.
    constexpr std::array limits{.06F,.06F,.06F};
    for(std::size_t axis=0;axis<3;++axis){
        const float raw=std::isfinite(input.swing[axis])?input.swing[axis]:0.F;
        const float bounded=limits[axis]*std::tanh(raw/limits[axis]);
        swing_[axis]+=(bounded-swing_[axis])*follow;
        // Eliminate lateral/vertical drift at full ADS, retaining depth motion.
        const float gain=axis==1 ? 1.F-settled*.75F : 1.F-settled;
        input.swing[axis]=enabled?swing_[axis]*gain:0.F;
    }
    if(!enabled){sprint_=0.F;swing_={};}
}

struct ScriptedWeapon::Impl {
    asIScriptEngine* engine{};
    asIScriptContext* context{};
    asIScriptModule* module{};
    std::vector<SkinResource> resources;
    std::map<asDWORD,std::array<float,2>> image_sizes;
    nlohmann::json manifest;
    ResolvedSkinVariant variant;
    std::filesystem::path root;
    SkinFrame frame;
    std::string diagnostics;
    std::uint32_t random{0x6a09e667U};
    std::optional<std::size_t> lower_arm, upper_arm;
    std::optional<std::size_t> aim_overlay;
    std::array<std::array<float,3>,4> arm_side_axes{};
    float frame_dt{};
    std::chrono::steady_clock::time_point deadline;
    Impl(){frame.models.reserve(64U);frame.sprites.reserve(64U);frame.sounds.reserve(32U);}
    ~Impl(){if(context)context->Release();if(engine)engine->ShutDownAndRelease();}
    static Impl& self(asIScriptGeneric* g){return *static_cast<Impl*>(g->GetEngine()->GetUserData());}
    static void config(asIScriptGeneric* g){
        const auto& name=*static_cast<std::string*>(g->GetArgObject(0));
        const auto& fallback=*static_cast<std::string*>(g->GetArgObject(1));
        // The native flash pass depth-tests billboards; it does not implement
        // OpenSpades soft-particle intersections or their compensating offsets.
        if(name=="r_softParticles"){new(g->GetAddressOfReturnLocation())std::string{"0"};return;}
        const auto& settings=self(g).variant.config;const auto found=settings.find(name);
        new(g->GetAddressOfReturnLocation())std::string(found==settings.end()?fallback:found->second);
    }
    static void message(const asSMessageInfo* m,void* user){
        auto& p=*static_cast<Impl*>(user);p.diagnostics+=std::string(m->section)+":"+std::to_string(m->row)+": "+m->message+"\n";
    }
    static void budget(asIScriptContext* c,void* user){
        if(std::chrono::steady_clock::now()>static_cast<Impl*>(user)->deadline)c->Abort();
    }
    static void resource(asIScriptGeneric* g){
        auto& p=self(g);const auto& name=*static_cast<std::string*>(g->GetArgObject(0));const int kind=static_cast<int>(g->GetArgDWord(1));
        for(std::size_t i=0;i<p.resources.size();++i)if(p.resources[i].name==name&&p.resources[i].kind==kind){g->SetReturnDWord(static_cast<asDWORD>(i));return;}
        try {
            if(p.resources.size()>=256U)throw std::runtime_error("Skin resource limit exceeded");
            auto target=p.manifest.at("resources").value(name,std::string{});
            p.resources.push_back({name,target.empty()?std::filesystem::path{}:contained(p.root,target),kind});
            g->SetReturnDWord(static_cast<asDWORD>(p.resources.size()-1U));
        }catch(const std::exception& e){asGetActiveContext()->SetException(e.what());}
    }
    static void model(asIScriptGeneric* g){
        auto& p=self(g);const auto id=g->GetArgDWord(0);auto* a=static_cast<CScriptArray*>(g->GetArgObject(1));
        if(id>=p.resources.size()||a->GetSize()!=16U||p.frame.models.size()>=60U)return;
        SkinModelDraw d;d.resource=id;
        // OpenSpades GetEyeMatrix uses (-right, front, -up): +X is LEFT.
        // Convert (left,forward,down) -> native eye (right,up,back).
        for(asUINT col=0;col<4;++col){
            d.transform[col*4]=-*static_cast<float*>(a->At(col*4));
            d.transform[col*4+1]=-*static_cast<float*>(a->At(col*4+2));
            d.transform[col*4+2]=-*static_cast<float*>(a->At(col*4+1));
            d.transform[col*4+3]=*static_cast<float*>(a->At(col*4+3));
        }
        if(std::ranges::all_of(d.transform,[](float v){return std::isfinite(v)&&std::abs(v)<10000.F;}))p.frame.models.push_back(d);
    }
    static void sprite(asIScriptGeneric* g){
        auto& p=self(g);if(p.frame.sprites.size()>=64U)return;
        SkinSprite s;s.resource=g->GetArgDWord(0);
        for(asUINT i=0;i<3;++i)s.position[i]=g->GetArgFloat(i+1);
        s.radius=g->GetArgFloat(4);s.rotation=g->GetArgFloat(5);
        for(asUINT i=0;i<4;++i)s.color[i]=g->GetArgFloat(i+6);
        s.screen=g->GetArgByte(10)!=0;
        if(s.resource<p.resources.size()&&std::isfinite(s.radius)&&s.radius>0&&s.radius<10000&&std::isfinite(s.rotation)&&
           std::ranges::all_of(s.position,[](float v){return std::isfinite(v);})&&std::ranges::all_of(s.color,[](float v){return std::isfinite(v);}))p.frame.sprites.push_back(s);
    }
    static void image_size(asIScriptGeneric* g){
        auto& p=self(g);const auto id=g->GetArgDWord(0);
        if(id>=p.resources.size()||p.resources[id].kind!=1){g->SetReturnFloat(32.F);return;}
        auto [entry,inserted]=p.image_sizes.try_emplace(id,std::array{32.F,32.F});
        // RegisterImage may be called every Draw2D, and asks for both axes.
        // Resources are immutable for this VM: read each header only once,
        // including failures, and discard the cache when a skin is reloaded.
        if(inserted&&!p.resources[id].path.empty()){
            std::array<unsigned char,24> header{};std::ifstream f(p.resources[id].path,std::ios::binary);f.read(reinterpret_cast<char*>(header.data()),24);
            constexpr std::array<unsigned char,8> png_signature{137,'P','N','G',13,10,26,10};
            if(f&&std::equal(png_signature.begin(),png_signature.end(),header.begin())&&
                header[8]==0&&header[9]==0&&header[10]==0&&header[11]==13&&
                header[12]=='I'&&header[13]=='H'&&header[14]=='D'&&header[15]=='R'){
                for(std::size_t axis=0;axis<2;++axis){
                    const auto offset=16U+axis*4U;
                    const auto n=(static_cast<unsigned>(header[offset])<<24U)|(static_cast<unsigned>(header[offset+1])<<16U)|(static_cast<unsigned>(header[offset+2])<<8U)|header[offset+3];
                    if(n>0&&n<=8192U)entry->second[axis]=static_cast<float>(n);
                }
            }
        }g->SetReturnFloat(entry->second[g->GetArgByte(1)?1U:0U]);
    }
    static void sound(asIScriptGeneric* g){
        auto& p=self(g);const auto id=g->GetArgDWord(0);const auto gain=g->GetArgFloat(1),pitch=g->GetArgFloat(2);
        if(id<p.resources.size()&&p.resources[id].kind==2&&std::isfinite(gain)&&std::isfinite(pitch)&&p.frame.sounds.size()<32U)
            p.frame.sounds.push_back({id,std::clamp(gain,0.F,8.F),std::clamp(pitch,.25F,4.F)});
    }
    static void rng(asIScriptGeneric* g){auto& r=self(g).random;r^=r<<13;r^=r>>17;r^=r<<5;g->SetReturnDWord(r);}
    static void hands(asIScriptGeneric* g){auto& p=self(g);for(asUINT i=0;i<3;++i){p.frame.left_hand[i]=g->GetArgFloat(i);p.frame.right_hand[i]=g->GetArgFloat(i+3);}}
    bool execute(asIScriptFunction* f,std::string& error) {
        if(!f){error="Missing skin entry point";return false;}
        // Constructors register many cold disk resources. Their bounded setup
        // allowance must not share the much tighter per-frame animation budget.
        context->Prepare(f);return run(error,1000);
    }
    bool run(std::string& error,unsigned budget_ms=40){
        deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(budget_ms);
        const int state=context->Execute();
        if(state!=asEXECUTION_FINISHED){error=state==asEXECUTION_EXCEPTION?context->GetExceptionString():"Skin exceeded its execution budget";return false;}
        return true;
    }
    void arms(){
        if(!lower_arm||!upper_arm)return;
        using V=std::array<float,3>;
        const auto sub=[](V a,V b){return V{a[0]-b[0],a[1]-b[1],a[2]-b[2]};};
        const auto dot=[](V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
        const auto cross=[](V a,V b){return V{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
        const auto normal=[&](V a){const auto n=std::sqrt(dot(a,a));return n>1e-6F?V{a[0]/n,a[1]/n,a[2]/n}:V{0,0,1};};
        std::size_t segment_index{};
        const auto segment=[&](V origin,V target,std::size_t id){
            const V distance=sub(target,origin),z=normal(distance);
            auto& previous=arm_side_axes[segment_index++];
            const float along=dot(previous,z);
            V projected{previous[0]-z[0]*along,previous[1]-z[1]*along,previous[2]-z[2]*along};
            V y=cross(z,{0,0,1});
            if(dot(y,y)<1e-5F)y=dot(projected,projected)>1e-5F?projected:cross(z,{1,0,0});
            y=normal(y);
            if(dot(projected,projected)>1e-5F){
                projected=normal(projected);
                // The vertical reference becomes singular as an arm points up.
                // Keep its roll continuous instead of flipping the wrist 180°.
                if(dot(projected,y)<0.F)for(float& value:y)value=-value;
                const float blend=1.F-std::exp(-std::clamp(frame_dt,0.F,.1F)*12.F);
                for(std::size_t k=0;k<3;++k)y[k]=projected[k]+(y[k]-projected[k])*blend;
                y=normal(y);
            }
            previous=y;const V x=normal(cross(y,z));
            SkinModelDraw d;d.resource=id;const std::array<V,4> axes{x,y,z,origin};
            // The authored wrist/elbow attachment is ten voxels from its root.
            // Fit that span to the solved joint distance, including long reaches.
            const float length=std::max(std::sqrt(dot(distance,distance)),.001F);
            for(std::size_t c=0;c<4;++c){const float s=c==3?1.F:c==2?length/10.F:.05F;d.transform[c*4]=-axes[c][0]*s;d.transform[c*4+1]=-axes[c][2]*s;d.transform[c*4+2]=-axes[c][1]*s;}d.transform[15]=1;frame.models.push_back(d);
        };
        for(std::size_t side=0;side<2;++side){
            segment_index=side*2;
            const float sign=side==0?1.F:-1.F;const V shoulder{.4F*sign,0,.25F},hand=side==0?frame.left_hand:frame.right_hand;
            if(!std::ranges::all_of(hand,[](float v){return std::isfinite(v);})||dot(hand,hand)<1e-8F||dot(hand,hand)>4.F)continue;
            const V distance=sub(hand,shoulder);V bend=normal(cross({.5F*sign,.2F,0},distance));if(bend[2]<0)bend[2]=-bend[2];
            const float bend_length=std::sqrt(std::max(.25F-dot(distance,distance)*.25F,0.F));V elbow{};
            for(std::size_t k=0;k<3;++k)elbow[k]=(hand[k]+shoulder[k])*.5F+bend[k]*bend_length;
            segment(elbow,hand,*lower_arm);segment(shoulder,elbow,*upper_arm);
        }
    }
};
ScriptedWeapon::ScriptedWeapon():impl_(std::make_unique<Impl>()){}
ScriptedWeapon::~ScriptedWeapon()=default;
bool ScriptedWeapon::load(const std::filesystem::path& file,std::string& error,const SkinVariantSelection& selection){
    // Skins compile on worker threads (loadout preparation, inventory
    // previews) while the game thread runs others; AngelScript needs its
    // shared thread manager set up once before engines exist on several threads.
    static std::once_flag threads_prepared;
    std::call_once(threads_prepared,[]{asPrepareMultithread();});
    impl_=std::make_unique<Impl>();auto& p=*impl_;
    try {
        p.root=file.parent_path();p.manifest=nlohmann::json::parse(read(file));
        p.variant=resolve_skin_variant(load_skin_variants(file),selection);
        p.engine=asCreateScriptEngine();if(!p.engine)throw std::runtime_error("AngelScript initialization failed");
        p.engine->SetUserData(&p);p.engine->SetMessageCallback(asFUNCTION(Impl::message),&p,asCALL_CDECL);
        p.engine->SetEngineProperty(asEP_MAX_STACK_SIZE,1024U*1024U);
        p.engine->SetEngineProperty(asEP_PROPERTY_ACCESSOR_MODE,2);
        p.engine->SetEngineProperty(asEP_INIT_GLOBAL_VARS_AFTER_BUILD,0);
        RegisterStdString(p.engine);RegisterScriptArray(p.engine,true);RegisterScriptMath(p.engine);
        const auto bind=[&](const char* decl,asSFuncPtr fn){if(p.engine->RegisterGlobalFunction(decl,fn,asCALL_GENERIC)<0)throw std::runtime_error(std::string("Skin API registration: ")+decl);};
        bind("int NativeResource(const string &in,int)",asFUNCTION(Impl::resource));
        bind("string NativeConfig(const string &in,const string &in)",asFUNCTION(Impl::config));
        bind("void NativeModel(int,const array<float> &in)",asFUNCTION(Impl::model));
        bind("void NativeSprite(int,float,float,float,float,float,float,float,float,float,bool)",asFUNCTION(Impl::sprite));
        bind("void NativeSound(int,float,float)",asFUNCTION(Impl::sound));
        bind("uint NativeRandom()",asFUNCTION(Impl::rng));
        bind("float NativeImageSize(int,bool)",asFUNCTION(Impl::image_size));
        bind("void NativeHands(float,float,float,float,float,float)",asFUNCTION(Impl::hands));
        p.module=p.engine->GetModule("skin",asGM_ALWAYS_CREATE);
        for(const auto& entry:p.manifest.at("scripts")){
            const auto name=entry.get<std::string>();const auto source=read(contained(p.root,name));p.module->AddScriptSection(name.c_str(),source.c_str(),source.size());
        }
        const std::string type=p.manifest.at("class");
        if(type.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)throw std::runtime_error("Invalid skin class");
        const bool spade=p.manifest.value("category","")=="Spade";
        const std::string parameters=spade
            ? "nativeSkin.ActionType=ready>=1?SpadeActionType::Idle:aim>.5?SpadeActionType::Dig:SpadeActionType::Bash;nativeSkin.ActionProgress=ready;"
            : "nativeSkin.AimDownSightState=aim;nativeSkin.ReadyState=ready;nativeSkin.ReloadProgress=reload;nativeSkin.IsReloading=reloading;nativeSkin.Ammo=ammo;nativeSkin.ClipSize=clip;";
        const std::string events=spade?"":"if(fired)nativeSkin.WeaponFired();if(started)nativeSkin.ReloadingWeapon();if(finished)nativeSkin.ReloadedWeapon();";
        const std::string wrapper="namespace spades { Renderer nativeRenderer; AudioDevice nativeAudio; "+type+
            "@ nativeSkin; void NativeInit(){@nativeSkin="+type+"(nativeRenderer,nativeAudio);} "
            "void NativeTick(float dt,float aim,float sprint,float raise,float ready,float reload,bool reloading,bool fired,bool started,bool finished,bool muted,int ammo,int clip,float sx,float sy,float sz,float tx,float ty,float tz,float width,float height){"
            "nativeRenderer.ScreenWidth=width;nativeRenderer.ScreenHeight=height;nativeSkin.SprintState=sprint;nativeSkin.RaiseState=raise;nativeSkin.IsMuted=muted;"+parameters+
            "nativeSkin.Swing=Vector3(sx,sy,sz);nativeSkin.TeamColor=Vector3(tx,ty,tz);nativeSkin.EyeMatrix=Matrix4();nativeSkin.Update(dt);"+events+
            "nativeSkin.AddToScene();nativeSkin.Draw2D();Vector3 l=nativeSkin.LeftHandPosition,r=nativeSkin.RightHandPosition;NativeHands(l.x,l.y,l.z,r.x,r.y,r.z);} }";
        p.module->AddScriptSection("adapter",wrapper.c_str(),wrapper.size());
        if(p.module->Build()<0)throw std::runtime_error(p.diagnostics);
        p.context=p.engine->CreateContext();p.context->SetLineCallback(asFUNCTION(Impl::budget),&p,asCALL_CDECL);
        p.deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
        if(p.module->ResetGlobalVars(p.context)<0)throw std::runtime_error("Skin global initialization failed");
        p.module->SetDefaultNamespace("spades");
        if(!p.execute(p.module->GetFunctionByName("NativeInit"),error))return false;
        // Some skins first register their scope or crosshair from Draw2D.
        // Discover every mapped PNG now, so the frontend can upload it before
        // opening a frame. NativeResource reuses these IDs during animation.
        for(const auto& [name,value]:p.manifest.at("resources").items()){
            const auto target=value.get<std::string>();
            auto extension=std::filesystem::path(target).extension().string();
            std::ranges::transform(extension,extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            if(extension!=".png"||std::ranges::any_of(p.resources,[&](const auto& r){return r.kind==1&&r.name==name;}))continue;
            if(p.resources.size()>=256U)throw std::runtime_error("Skin resource limit exceeded");
            p.resources.push_back({name,contained(p.root,target),1});
        }
        if(p.manifest.contains("hands")){
            p.lower_arm=p.resources.size();p.resources.push_back({"Hands/Arm.kv6",contained(p.root,p.manifest["hands"]["lower"].get<std::string>()),0,SkinArmPart::lower});
            p.upper_arm=p.resources.size();p.resources.push_back({"Hands/UpperArm.kv6",contained(p.root,p.manifest["hands"]["upper"].get<std::string>()),0,SkinArmPart::upper});
        }
        if(p.manifest.contains("aim_overlay")){
            p.aim_overlay=p.resources.size();p.resources.push_back({"Tuning/AimOverlay",contained(p.root,p.manifest["aim_overlay"].get<std::string>()),1});
        }
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ScriptedWeapon::update(const SkinInput& requested,std::string& error){
    auto i=requested;
    if(i.reloading)i.aim=0.F;
    auto& p=*impl_;if(!p.context){error="Skin is not loaded";return false;}
    p.frame.models.clear();p.frame.sprites.clear();p.frame.sounds.clear();
    p.frame.left_hand={};p.frame.right_hand={};p.frame.scope_opacity=0.F;
    // Invalid engine input must not poison persistent script animation springs.
    const auto finite=[](float value){return std::isfinite(value);};
    if(!std::ranges::all_of(std::array{i.dt,i.aim,i.sprint,i.raise,i.ready,i.reload_progress,i.screen_width,i.screen_height},finite)||
       !std::ranges::all_of(i.swing,finite)||!std::ranges::all_of(i.team_color,finite)){
        error="Invalid skin animation input";return false;
    }
    p.context->Prepare(p.module->GetFunctionByName("NativeTick"));asUINT a=0;
    for(float f:{i.dt,i.aim,i.sprint,i.raise,i.ready,i.reload_progress})p.context->SetArgFloat(a++,f);
    for(bool b:{i.reloading,i.fired,i.reload_started,i.reload_finished,i.muted})p.context->SetArgByte(a++,b?1:0);
    p.context->SetArgDWord(a++,static_cast<asDWORD>(i.ammo));p.context->SetArgDWord(a++,static_cast<asDWORD>(i.clip_size));
    for(float f:i.swing){p.context->SetArgFloat(a++,f);}for(float f:i.team_color){p.context->SetArgFloat(a++,f);}
    p.context->SetArgFloat(a++,i.screen_width);p.context->SetArgFloat(a,i.screen_height);
    if(!p.run(error)){return false;}p.frame_dt=i.dt;p.arms();
    if(p.variant.scope&&!i.reloading)p.frame.scope_opacity=std::clamp((i.aim-.6F)*5.F,0.F,1.F);
    if(p.aim_overlay&&i.aim>.8F&&!i.reloading){
        if(p.manifest.value("hide_aimed_models",false))p.frame.models.clear();
        p.frame.sprites.clear();const float width=i.screen_height*(4.F/3.F);
        p.frame.sprites.push_back({*p.aim_overlay,{(i.screen_width-width)*.5F,0.F,i.screen_height},{1,1,1,1},width,0,true});
    }
    return true;
}
const std::vector<SkinResource>& ScriptedWeapon::resources()const{return impl_->resources;}
const ResolvedSkinVariant& ScriptedWeapon::variant()const{return impl_->variant;}
const SkinFrame& ScriptedWeapon::frame()const{return impl_->frame;}
ChunkMesh ScriptedWeapon::source_mesh(const std::filesystem::path& path,std::optional<VxlColor> team){
    auto model=Kv6Model::load_file(path);return model?source_mesh(std::move(*model),team):ChunkMesh{};
}
ChunkMesh ScriptedWeapon::source_mesh(Kv6Model model,std::optional<VxlColor> team){
    if(team){model.apply_default_color(*team);}auto mesh=model.mesh();
    constexpr std::array<std::uint8_t,6> faces{0,1,5,4,2,3};
    for(auto& v:mesh.vertices){const float y=v.y;v.y=v.z;v.z=-y;v.face=faces[v.face];}
    // Both the asset rotation and the eye conversion preserve handedness.
    const auto low=mesh.minimum,high=mesh.maximum;
    mesh.minimum={low[0],low[2],-high[1]};mesh.maximum={high[0],high[2],-low[1]};
    return mesh;
}
ChunkMesh ScriptedWeapon::model_mesh(std::size_t id,VxlColor team,const ClassModelOverrides& character)const{
    if(id>=impl_->resources.size())return {};
    const auto& resource=impl_->resources[id];
    if(resource.kind!=0||resource.path.empty())return {};
    if(resource.arm_part==SkinArmPart::none)return source_mesh(resource.path);
    const auto found=character.find(resource.arm_part==SkinArmPart::upper?"arm_upper":"arm_lower");
    if(found==character.end())return source_mesh(resource.path,team);
    auto mesh=source_mesh(found->second,team);
    if(!character.open_spades){
        // Retail arm assets run along Y about a centred pivot; OpenSpades
        // attaches Z-long segments at the elbow/shoulder. Keep the selected
        // skin's geometry, fitting its length into that attachment frame.
        const auto& model=found->second;const auto pivot=model.pivot();
        const float scale=12.F/static_cast<float>(model.size_y());
        const std::array center{static_cast<float>(model.size_x()-1U)*.5F-pivot[0],static_cast<float>(model.size_y()-1U)*.5F-pivot[1],static_cast<float>(model.size_z()-1U)*.5F-pivot[2]};
        mesh.minimum.fill(std::numeric_limits<float>::max());mesh.maximum.fill(std::numeric_limits<float>::lowest());
        constexpr std::array<std::uint8_t,6> faces{0,1,4,5,3,2};
        for(auto& v:mesh.vertices){
            const auto y=v.y;v.x=(v.x-center[0])*scale;v.y=-(v.z-center[2])*scale-.5F;v.z=(y-center[1])*scale+5.5F;v.face=faces[v.face];
            const std::array xyz{v.x,v.y,v.z};for(std::size_t k=0;k<3;++k){mesh.minimum[k]=std::min(mesh.minimum[k],xyz[k]);mesh.maximum[k]=std::max(mesh.maximum[k],xyz[k]);}
        }
    }
    return mesh;
}
} // namespace battlespades::world
