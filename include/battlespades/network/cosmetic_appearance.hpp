#pragma once
#include "battlespades/network/cosmetic_slots.hpp"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <algorithm>

namespace battlespades::network {
/** Separate extension store. Never changes roster, loadout or gameplay state. */
class CosmeticAppearances final {
    std::map<std::uint8_t,std::map<std::string,std::string>> players_;
public:
    static constexpr std::uint8_t packet_id{240U};
    void clear() { players_.clear(); }
    void erase(std::uint8_t player) { players_.erase(player); }
    [[nodiscard]] std::string_view item(std::uint8_t player, std::string_view slot) const {
        const auto p=players_.find(player);
        if (p==players_.end()) return {};
        const auto found=p->second.find(std::string{slot});
        return found==p->second.end() ? std::string_view{} : std::string_view{found->second};
    }
    [[nodiscard]] bool ingest(std::span<const std::byte> packet) {
        if (packet.size()<7U || packet.size()>8192U || std::to_integer<std::uint8_t>(packet[0])!=packet_id)
            return false;
        const std::string_view bytes{reinterpret_cast<const char*>(packet.data()+1U),packet.size()-1U};
        if (!bytes.starts_with("BSC1")) return false;
        const auto body=bytes.substr(4U);
        // The envelope has exactly two object levels; bound parser recursion
        // before decoding even a malformed server response.
        unsigned depth{}; bool quoted{}, escaped{};
        for (const char ch : body) {
            if (quoted) { if (escaped) escaped=false; else if(ch=='\\') escaped=true; else if(ch=='"') quoted=false; }
            else if (ch=='"') quoted=true;
            else if (ch=='{' || ch=='[') { if(++depth>4U) return false; }
            else if (ch=='}' || ch==']') { if(depth==0U) return false; --depth; }
        }
        try {
            const auto value=nlohmann::json::parse(body);
            if (!value.is_object() || !value.contains("player_id") || !value.at("player_id").is_number_unsigned() ||
                value.at("player_id").get<std::uint64_t>()>127U || !value.contains("items") ||
                !value.at("items").is_object() || value.at("items").size()>64U) return false;
            std::map<std::string,std::string> items;
            for (const auto& [slot,id] : value.at("items").items()) {
                if(slot.empty() || slot.size()>32U || !id.is_string()) return false;
                const auto cosmetic=id.get<std::string>();
                if(cosmetic.empty() || cosmetic.size()>80U || !std::ranges::all_of(cosmetic,[](char c){
                    return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='-';})) return false;
                items.emplace(slot,cosmetic);
            }
            auto normalized=items;
            for(const auto& [slot,id]:items){
                const std::string display{cosmetic_display_slot(id,slot)};
                if(display!=slot){normalized.erase(slot);normalized.try_emplace(display,id);}
            }
            players_[value.at("player_id").get<std::uint8_t>()]=std::move(normalized);
            return true;
        } catch (const nlohmann::json::exception&) { return false; }
    }
};
} // namespace battlespades::network
