#include "battlespades/world/weapon_variants.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <numbers>
#include <set>
#include <stdexcept>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace battlespades::world {
namespace {
bool safe_id(std::string_view id) {
    return !id.empty()&&id.size()<=160U&&std::ranges::all_of(id,[](unsigned char c){
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';
    });
}
bool safe_value(const std::string& value) {
    if(value.empty()||value.size()>24U)return false;
    try{std::size_t end{};const auto number=std::stod(value,&end);return end==value.size()&&std::isfinite(number)&&std::abs(number)<=1000.;}
    catch(...){return false;}
}
nlohmann::json read_json(const std::filesystem::path& path) {
    if(std::filesystem::file_size(path)>1024U*1024U)throw std::runtime_error("Skin options file is too large");
    std::ifstream input{path};if(!input)throw std::runtime_error("Cannot read skin options");
    return nlohmann::json::parse(input);
}
}
const SkinVariantChoice& SkinVariantOption::selected(const SkinVariantSelection& values) const {
    const auto found=values.find(id);
    const auto requested=found==values.end()?default_choice:found->second;
    for(const auto& choice:choices)if(choice.id==requested)return choice;
    for(const auto& choice:choices)if(choice.id==default_choice)return choice;
    return choices.front();
}
SkinVariantDefinition load_skin_variants(const std::filesystem::path& manifest) {
    SkinVariantDefinition result;
    try {
        const auto data=read_json(manifest);
        if(data.value("category","")=="Spade")return result;
        if(data.contains("optic")){
            result.magnification=data.at("optic").value("magnification",1.F);
            result.scope=data.at("optic").value("scope",false);
            if(!std::isfinite(result.magnification)||result.magnification<1.F||result.magnification>4.F)
                throw std::runtime_error("Invalid fixed sight magnification");
        }
        std::set<std::string> ids;
        for(const auto& row:data.value("options",nlohmann::json::array())){
            SkinVariantOption option{row.at("id"),row.at("label"),row.at("default"),{}};
            if(!safe_id(option.id)||option.id=="zoom"||option.label.size()>64U||!ids.insert(option.id).second||ids.size()>8U)
                throw std::runtime_error("Invalid skin option");
            std::set<std::string> choices;
            for(const auto& value:row.at("choices")){
                SkinVariantChoice choice;
                choice.id=value.at("id");choice.label=value.at("label");
                choice.config=value.value("config",std::map<std::string,std::string>{});
                choice.magnification=value.value("magnification",0.F);choice.scope=value.value("scope",false);
                if(!safe_id(choice.id)||choice.label.size()>64U||!choices.insert(choice.id).second||choices.size()>16U||choice.config.size()>16U||
                   !std::isfinite(choice.magnification)||choice.magnification<0.F||choice.magnification>4.F)
                    throw std::runtime_error("Invalid skin variant");
                for(const auto& [key,setting]:choice.config)if(!safe_id(key)||!safe_value(setting))throw std::runtime_error("Invalid script option");
                option.choices.push_back(std::move(choice));
            }
            if(!choices.contains(option.default_choice))throw std::runtime_error("Missing default skin variant");
            result.options.push_back(std::move(option));
        }
        // Magnification belongs to the authored sight choice. Legacy saved
        // "zoom" preferences are deliberately ignored by the resolver.
    }catch(...){return {};}
    return result;
}
ResolvedSkinVariant resolve_skin_variant(const SkinVariantDefinition& definition,const SkinVariantSelection& selection) {
    ResolvedSkinVariant result;
    result.magnification=definition.magnification;
    result.scope=definition.scope;
    for(const auto& option:definition.options){
        const auto& choice=option.selected(selection);
        for(const auto& [key,value]:choice.config)result.config[key]=value;
        if(choice.magnification>0.F)result.magnification=choice.magnification;
        result.scope=result.scope||choice.scope;
    }
    return result;
}
double skin_variant_zoom_target(float magnification) noexcept {
    if(!std::isfinite(magnification)||magnification<1.F)return 0.;
    const double fov=2.*std::atan(std::tan(75.*std::numbers::pi/360.)/std::clamp(static_cast<double>(magnification),1.,4.))*180./std::numbers::pi;
    return (75.-fov)/37.5;
}
SkinVariantPreferences::SkinVariantPreferences(std::filesystem::path path):path_(std::move(path)){}
bool SkinVariantPreferences::load(std::string& error) {
    error.clear();
    try{
        if(!std::filesystem::exists(path_))return true;
        const auto data=read_json(path_);
        if(data.at("schema_version")!=1)throw std::runtime_error("Unsupported skin options version");
        const auto saved=data.at("skins").get<std::map<std::string,SkinVariantSelection>>();
        if(saved.size()>256U)throw std::runtime_error("Too many saved skin options");
        for(const auto& [item,values]:saved){
            if(!safe_id(item)||values.size()>9U)throw std::runtime_error("Invalid saved skin options");
            for(const auto& [key,value]:values)if(!safe_id(key)||!safe_id(value))throw std::runtime_error("Invalid saved skin choice");
        }
        choices_=saved;++revision_;return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
SkinVariantSelection SkinVariantPreferences::selection(std::string_view item) const {
    const auto found=choices_.find(std::string{item});return found==choices_.end()?SkinVariantSelection{}:found->second;
}
bool SkinVariantPreferences::set(std::string_view item,const SkinVariantDefinition& definition,const SkinVariantSelection& requested,std::string& error) {
    error.clear();std::filesystem::path temporary;
    try{
        if(!safe_id(item)||definition.options.empty())throw std::runtime_error("This skin has no configurable variants");
        SkinVariantSelection values;
        for(const auto& option:definition.options)values[option.id]=option.selected(requested).id;
        auto candidate=choices_;candidate[std::string{item}]=std::move(values);
        if(candidate.size()>256U)throw std::runtime_error("Too many saved skin options");
        if(!path_.parent_path().empty())std::filesystem::create_directories(path_.parent_path());
        static std::atomic<std::uint64_t> sequence{};
        temporary=path_;temporary+=".tmp."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"."+std::to_string(sequence++);
        {std::ofstream output{temporary,std::ios::binary|std::ios::trunc};
            output<<nlohmann::json{{"schema_version",1},{"skins",candidate}}.dump(2)<<'\n';output.flush();
            if(!output)throw std::runtime_error("Could not save skin choices");}
#if defined(_WIN32)
        if(!MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not replace saved skin choices");
#else
        std::filesystem::rename(temporary,path_);
#endif
        choices_=std::move(candidate);++revision_;return true;
    }catch(const std::exception& exception){
        if(!temporary.empty()){std::error_code ignored;std::filesystem::remove(temporary,ignored);}
        error=exception.what();return false;
    }
}
} // namespace battlespades::world
