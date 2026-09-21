#include "battlespades/frontend/inventory_session.hpp"
#include "battlespades/network/cosmetic_appearance.hpp"
#include "battlespades/world/class_models.hpp"
#include "battlespades/world/kv6_model.hpp"
#include "battlespades/world/weapon_models.hpp"
#include "battlespades/world/local_entity.hpp"
#include "battlespades/world/scripted_weapon.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "battlespades/world/weapon_catalog.hpp"
#include "battlespades/world/retail_view_model.hpp"
using namespace battlespades;
void expect(bool value, const char* message) {
    if (!value)
        throw std::runtime_error{message};
}
void expect_renderable(const frontend::InventoryMenuModel& menu) {
    const auto list = menu.build();
    expect(!list.empty(), "Inventory presentation is empty");
    for (const auto& command : list.commands()) {
        if (const auto* sprite = std::get_if<ui::SpriteDrawCommand>(&command)) {
            const auto rect = sprite->destination;
            if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) ||
                !std::isfinite(rect.height) || rect.width <= 0.0 || rect.height <= 0.0)
                throw std::runtime_error{
                    "Inventory submitted a non-renderable sprite: " + sprite->asset_id + " (" +
                    std::to_string(rect.width) + " x " + std::to_string(rect.height) + ")"};
        }
    }
}
void check_zero_xp_presentation() {
    for (const auto section : {frontend::InventorySection::collection,
                               frontend::InventorySection::crates,
                               frontend::InventorySection::history,
                               frontend::InventorySection::packs}) {
        frontend::InventoryMenuModel menu;
        menu.section = section;
        expect_renderable(menu);
        menu.busy = true;
        expect_renderable(menu);
        menu.fail("Collection unavailable. Try Refresh.");
        expect_renderable(menu);
        menu.complete({});
        expect_renderable(menu);
        menu.data.guest = true;
        expect_renderable(menu);
        menu.data.next_xp = "0";
        expect_renderable(menu);
        menu.complete(frontend::inventory_preview_fixture());
        menu.data.level_xp = "0";
        expect_renderable(menu);
        menu.data.level_xp = menu.data.next_xp;
        expect_renderable(menu);
    }
}
void same_geometry(const world::ChunkMesh& a, const world::ChunkMesh& b) {
    expect(a.indices == b.indices && a.vertices.size() == b.vertices.size(),
           "Cosmetics changed mesh topology");
    expect(a.minimum == b.minimum && a.maximum == b.maximum, "Cosmetics changed bounds");
    for (std::size_t i = 0U; i < a.vertices.size(); ++i)
        expect(a.vertices[i].x == b.vertices[i].x && a.vertices[i].y == b.vertices[i].y &&
                   a.vertices[i].z == b.vertices[i].z,
               "Cosmetics moved a vertex");
}
void check_verified_model_cache(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    const auto* item = frontend::find_inventory_cosmetic("community-stg44-v2");
    const auto first = frontend::inventory_verified_model(*item, root);
    expect(first && first == frontend::inventory_verified_model(*item, root),
           "Repeated skin selection re-decodes the same verified model");
    const auto directory = fs::temp_directory_path() / ("aos-skin-cache-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto copy = directory / item->asset;
    fs::create_directories(copy.parent_path());
    try {
        fs::copy_file(root.parent_path() / item->asset, copy);
        const auto isolated = frontend::inventory_verified_model(*item, directory / "original");
        expect(isolated && isolated != first, "Skin cache leaked an asset between installs");
        std::ofstream{copy, std::ios::binary | std::ios::trunc} << "invalid";
        expect(!frontend::inventory_verified_model(*item, directory / "original"),
               "Modified skin bypassed its hash check through the cache");
        fs::remove(copy);
        expect(!frontend::inventory_verified_model(*item, directory / "original"),
               "Removed skin retained stale cached ownership of its resource");
    } catch (...) { fs::remove_all(directory); throw; }
    fs::remove_all(directory);
}
void check_snapshot_compatibility(const std::filesystem::path& root) {
    // Header returned by the published service before collection-v3 shipped.
    nlohmann::json snapshot = {
        {"schema_version",1},
        {"catalog",{{"version","collection-v2"},{"digest","aa008150d68e0df9925ffa354b944a06010698bbac62d16c497f29581ae7ac3e"}}},
        {"progression",{{"level","2"},{"lifetime_xp","1100"},{"level_xp","100"},{"xp_to_next","1250"},
            {"inventory_revision","4"},{"unopened_crates","1"},{"pity",{0,0,0}},
            {"features",{{"opening",true},{"equip",true},{"awards",true}}}}},
        {"inventory",{{"items", {{{"cosmetic_id","community-stg44-v2"}}}}}},
        {"equipped", {{{"slot","weapon:60:view"},{"cosmetic_id","community-stg44-v2"}}}},
        {"crates",{{"items", {{{"id","legacy-crate"},{"level","2"},{"commitment",std::string(64,'a')},{"catalog_version","weapons-v2"}}}}, {"next_cursor",nullptr}}},
        {"history",{{"items",nlohmann::json::array()},{"next_cursor",nullptr}}}
    };
    frontend::InventoryMenuModel model;
    model.complete(frontend::parse_inventory_snapshot(snapshot));
    expect(model.online && model.filtered_items().size()>80U,"Published collection-v2 blanked the installed catalogue");
    const auto visible=model.filtered_items().size();
    expect(model.data.crates.size()==1U && model.equipped_item("weapon:60:view"),"Legacy snapshot lost ownership or crates");
    model.owned_only=true;
    expect(model.filtered_items().size()==1U,"Installed catalogue granted unearned skins");
    snapshot["history"]["items"]={{{"receipt",{{"crate_id","old-opening"},
        {"item",{{"id","field-character-skin-uniform-v2"},{"name","Field Uniform"},{"rarity","common"}}},
        {"opened_at","2026-09-07T00:00:00Z"},{"commitment",std::string(64,'a')},{"seed",std::string(64,'b')}}}}};
    const auto history=frontend::parse_inventory_snapshot(snapshot).history;
    expect(history.size()==1U && history[0].item_id=="community-170869-bf4-assault-class-us-assault-v3",
        "Opening history still presents the retired generated model");
    for(const auto& [version,count]:std::array<std::pair<std::string_view,std::size_t>,3U>{{
        {"weapons-v4",45U},{"characters-v4",38U},{"cosmetics-v4",10U}}}) {
        const auto pool=frontend::inventory_crate_pool(model.data,version);
        expect(pool.items.size()==count,"Current crate contents are missing imported models");
        for(const auto& item:pool.items)expect(item.enabled && item.kind!="weapon_skin","Crate contains retired generated art");
    }
    for (const bool change_version : {false,true}) {
        auto invalid=snapshot;
        invalid["catalog"][change_version?"version":"digest"]=change_version?"collection-v999":std::string(64,'0');
        bool rejected{};
        try { static_cast<void>(frontend::parse_inventory_snapshot(invalid)); }
        catch(const std::exception&) { rejected=true; }
        expect(rejected,"Unknown catalogue/version pairing was accepted");
    }
    network::RevivalIdentityConfig config;
    config.state_path=root/"missing-inventory-test-identity.json";
    frontend::InventorySession session{std::make_shared<network::RevivalIdentityService>(config)};
    frontend::InventoryMenuModel signed_out;
    session.start(signed_out,frontend::InventoryAction::refresh);
    expect(!signed_out.online && !signed_out.loaded && !signed_out.error.empty(),"Signed-out browsing became an authenticated collection");
    expect(signed_out.filtered_items().size()==visible,"Signed-out skin browser is empty");
    signed_out.owned_only=true;
    expect(signed_out.filtered_items().empty() && signed_out.data.crates.empty(),"Signed-out browsing fabricated ownership");
    snapshot["inventory"]["items"].push_back({{"cosmetic_id","community-bren-v2"}});
    snapshot["equipped"].push_back({{"slot","weapon:8:view"},{"cosmetic_id","community-bren-v2"}});
    snapshot["equipped"].push_back({{"slot","weapon:8:world"},{"cosmetic_id","community-bren-v2"}});
    model.complete(frontend::parse_inventory_snapshot(snapshot));
    expect(model.equipped_item("weapon:61:view")&&model.equipped_item("weapon:61:world")&&
               !model.equipped_item("weapon:8:view"),"Previously awarded/equipped Bren must move to the Medic LMG");
}

void check_corrected_skin_bindings(const std::filesystem::path& root) {
    const auto* bren=frontend::find_inventory_cosmetic("community-bren-v2");
    expect(bren&&bren->parents.size()==1U&&bren->parents[0].tool==61U,"Bren is not the Medic LMG");
    const auto finish=frontend::inventory_weapon_finish(bren,root,61U);
    expect(finish.has_value()&&!frontend::inventory_weapon_finish(bren,root,8U),"Bren still replaces the minigun");
    expect(static_cast<bool>(world::load_weapon_models(root,61U,{1,1,1},std::nullopt,1U,&*finish)),"LMG Bren world fit failed");
    for(const auto slot:{"weapon:61:view","weapon:61:world"}) {
        const auto stored=network::cosmetic_storage_slot(bren->id,slot);
        expect(network::cosmetic_display_slot(bren->id,stored)==slot,"Bren equipment compatibility does not round trip");
    }
    network::CosmeticAppearances appearances;
    const std::string payload=std::string(1,static_cast<char>(240))+R"(BSC1{"player_id":7,"items":{"weapon:8:world":"community-bren-v2"}})";
    expect(appearances.ingest(std::span{reinterpret_cast<const std::byte*>(payload.data()),payload.size()})&&
               appearances.item(7U,"weapon:61:world")==bren->id&&appearances.item(7U,"weapon:8:world").empty(),
           "Remote legacy Bren appearance must follow the LMG");
    world::ScriptedWeapon weapon;
    std::string error;
    expect(weapon.load(root.parent_path()/bren->scripted_skin,error),"Bren animation failed");
    for(const auto id:{"community-170858-astronaut-v3","community-170858-armageddon-astronaut-v3",
                       "community-170858-armageddon-astronaut-without-helmet-v3"}) {
        const auto parts=frontend::inventory_character_parts(frontend::find_inventory_cosmetic(id),root);
        expect(parts.contains("arms")&&parts.contains("arm_upper")&&parts.contains("arm_lower"),
               "Astronaut combined arms were not adapted for animated hands");
        for(const auto role:{"arm_upper","arm_lower"}) {
            const auto& source=parts.at("arms").voxels();
            for(const auto& voxel:parts.at(role).voxels())
                expect(std::ranges::any_of(source,[&](const auto& original){return voxel.color==original.color;}),
                       "Adapted astronaut arm introduced materials absent from the authored model");
        }
        for(const auto klass:std::array<std::uint8_t,2>{2U,17U}) {
            const auto models=world::load_class_models(root,klass,{72,111,181,255},1U,std::nullopt,nullptr,&parts);
            expect(models&&models.models->first_person_arms.size()==2U&&!models.models->combined_arms,
                   "Classic weapons or third-person model still use default astronaut arms");
        }
        for(std::size_t resource=0U;resource<weapon.resources().size();++resource) {
            if(weapon.resources()[resource].arm_part==world::SkinArmPart::none)continue;
            const auto actual=weapon.model_mesh(resource,{72,111,181,255},parts);
            const auto defaults=weapon.model_mesh(resource,{72,111,181,255});
            expect(!actual.empty()&&!std::ranges::equal(actual.vertices,defaults.vertices,[](const auto& a,const auto& b){
                return a.x==b.x&&a.y==b.y&&a.z==b.z&&a.abgr==b.abgr;}),"Scripted weapon fell back to standard hands");
        }
    }
    for(const auto id:{"community-169631-tf2-level-1-sentry-v3","community-169599-dead-v3","community-169613-dead-v3"}) {
        const auto* item=frontend::find_inventory_cosmetic(id);
        expect(item!=nullptr,"Ground-contact cosmetic fixture missing");
        const auto parts=frontend::inventory_character_parts(item,root);
        const std::array<std::uint8_t,2> types=item->kind=="tombstone"?std::array<std::uint8_t,2>{11U,12U}:
                                                                                std::array<std::uint8_t,2>{8U,8U};
        for(const auto type:types) {
            const auto* definition=world::find_entity_definition(type);
            std::vector<float> bottoms;
            for(const auto& part:definition->parts) {
                auto model=item->kind=="tombstone"?*frontend::inventory_verified_model(*item,root):parts.at(std::string{part.kv6});
                if(item->kind!="tombstone")model.offset_pivots(part.pivot_offset);
                bottoms.push_back(model.mesh().minimum[1]);
            }
            for(const bool grounded:{false,true})for(const double height:{64.0,64.75}) {
                world::LocalEntity entity;entity.type=type;entity.position={50,50,height};entity.face=4U;entity.grounded=grounded;
                const auto offset=world::entity_rig_vertical_contact_adjustment(entity,*definition,definition->parts,bottoms);
                double bottom=-10000;
                for(std::size_t part=0U;part<definition->parts.size();++part) {
                    const auto& piece=definition->parts[part];if(piece.rotation_mode>=2U)continue;
                    bottom=std::max(bottom,world::entity_presentation_position(entity,piece).z+offset-
                                          bottoms[part]*definition->model_size*piece.scale);
                }
                expect(std::abs(bottom-(grounded?std::floor(height):height))<0.00001,
                       "Imported tombstone/sentry foot lies inside or above the support block");
                entity.aim_pitch=70;
                expect(offset==world::entity_rig_vertical_contact_adjustment(entity,*definition,definition->parts,bottoms),
                       "Aiming the turret moves its feet");
                entity.face=0U;
                expect(world::entity_rig_vertical_contact_adjustment(entity,*definition,definition->parts,bottoms)==0,
                       "Wall attachment received a floor offset");
            }
        }
    }
}
int main(int argc, char** argv) {
    try {
        if(argc==2 && std::string_view{argv[1]}=="--live-equip-check") {
            network::RevivalIdentityService identity{network::RevivalIdentityConfig{}};
            const auto snapshot=identity.inventory_request({});
            expect(static_cast<bool>(snapshot),snapshot.error.c_str());
            auto before=frontend::parse_inventory_snapshot(snapshot.payload);
            const auto selected=std::ranges::find_if(before.equipped,[](const auto& pair){return pair.first.starts_with("class:")&&pair.first.ends_with(":body");});
            expect(selected!=before.equipped.end(),"No current class appearance to confirm");
            network::InventoryRequest request;request.kind=network::InventoryRequestKind::equip;
            request.target=selected->first;request.cosmetic_id=selected->second;request.revision=before.revision;
            const auto started=std::chrono::steady_clock::now();
            const auto response=identity.inventory_request(request);
            expect(static_cast<bool>(response),response.error.c_str());
            expect(response.payload.contains("collection"),"Live equipment confirmation lacks its committed collection");
            auto after=frontend::parse_inventory_snapshot(response.payload.at("collection"));
            std::ranges::sort(before.equipped);std::ranges::sort(after.equipped);
            expect(before.equipped==after.equipped,"Confirming an existing class assignment changed another loadout slot");
            expect(before.crate_count==after.crate_count,"Equipment confirmation changed unopened crates");
            std::cout<<"Existing class assignment confirmed in "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()
                <<" ms with one response; all "<<after.equipped.size()<<" equipped slots and unopened crates preserved\n";
            return 0;
        }
        if (argc==3 && std::string_view{argv[1]}=="--live-collection") {
            // Read-only diagnostic using the normal protected launcher session.
            network::RevivalIdentityService identity{network::RevivalIdentityConfig{}};
            network::InventoryResult response;
            for(int i=0;i<3;++i) {
                const auto started=std::chrono::steady_clock::now();response=identity.inventory_request({});
                std::cout<<"Collection read "<<i+1<<": "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()
                    <<" ms, "<<response.payload.dump().size()<<" response bytes\n";
                if(!response)break;
            }
            if (!response) throw std::runtime_error{response.error};
            frontend::InventoryMenuModel model;
            model.complete(frontend::parse_inventory_snapshot(response.payload));
            expect(model.online && !model.filtered_items().empty(),"Live collection loaded no skins");
            const auto visible=model.filtered_items().size();
            const auto* item=frontend::find_inventory_cosmetic("community-stg44-v2");
            expect(item && frontend::inventory_verified_model(*item,argv[2]),"Staged weapon model failed verification");
            model.owned_only=true;
            std::cout<<"Live collection: "<<response.payload.at("catalog").at("version")
                <<", "<<visible<<" browsable skins, "<<model.filtered_items().size()
                <<" active owned skins, "<<model.data.crates.size()<<" unopened crates, "
                <<model.data.equipped.size()<<" equipped slots. Staged model verified.\n";
            return 0;
        }
        if (argc==4 && std::string_view{argv[1]}=="--account-switch-http") {
            network::RevivalIdentityConfig config;
            config.api_base=argv[2];const auto directory=std::filesystem::path{argv[3]};
            config.state_path=directory/"identity.json";
            auto identity=std::make_shared<network::RevivalIdentityService>(config);
            expect(static_cast<bool>(identity->login("OwnerFixture","test-password")),"Owner fixture login failed");
            frontend::InventorySession session{identity};frontend::InventoryMenuModel model;
            const auto finish=[&]{
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
                while(model.busy&&std::chrono::steady_clock::now()<deadline){session.pump(model);std::this_thread::sleep_for(std::chrono::milliseconds{5});}
                expect(!model.busy,"Account switching request did not finish");
            };
            session.start(model,frontend::InventoryAction::refresh);finish();
            expect(model.loaded&&model.online,"Owner fixture collection did not load");
            model.kind_filter=1;model.owned_only=true;model.page=model.selected=model.slot_index=0;
            session.start(model,frontend::InventoryAction::equip_weapon);
            expect(model.busy,"Account switching mutation did not start");
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{5};
            while(!std::filesystem::exists(directory/"mutation-started")&&std::chrono::steady_clock::now()<deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
            expect(std::filesystem::exists(directory/"mutation-started"),"Mutation did not reach the HTTP fixture");
            expect(static_cast<bool>(identity->login("OtherFixture","test-password")),"Other fixture login failed");
            {std::ofstream release{directory/"release-mutation"};release<<"ready";}
            finish();
            expect(!model.loaded&&model.data.items.empty()&&model.error.empty(),"Old account mutation populated the new inventory");
            network::InventoryRequest request;request.expected_account="inventory-owner-fixture";
            auto rejected=identity->inventory_request(request);
            expect(!rejected&&rejected.error_code=="account_changed","Pinned read used the new account's token");
            request.kind=network::InventoryRequestKind::equip;request.target="weapon:60:view";
            request.cosmetic_id="community-stg44-v2";request.revision="2";
            rejected=identity->inventory_request(request);
            expect(!rejected&&rejected.error_code=="account_changed","Pinned mutation used the new account's token");
            request={};request.expected_account="inventory-other-fixture";
            expect(static_cast<bool>(identity->inventory_request(request)),"A fresh new-account request was incorrectly rejected");
            std::cout<<"Account switch during lost equip response preserved account-bound requests and cache\n";
            return 0;
        }
        if (argc==4 && std::string_view{argv[1]}=="--session-http") {
            network::RevivalIdentityConfig config;
            config.api_base=argv[2]; config.state_path=std::filesystem::path{argv[3]}/"identity.json";
            auto identity=std::make_shared<network::RevivalIdentityService>(config);
            expect(static_cast<bool>(identity->login("Fixture","test-password")),"Session login failed");
            frontend::InventorySession session{identity}; frontend::InventoryMenuModel model;
            const auto run=[&](frontend::InventoryAction action) {
                const auto started=std::chrono::steady_clock::now();
                session.start(model,action);
                expect(model.busy,"Async inventory request did not start");
                expect(std::chrono::steady_clock::now()-started<std::chrono::milliseconds{200},"Inventory request blocked the UI thread");
                while(model.busy && std::chrono::steady_clock::now()-started<std::chrono::seconds{15}) {
                    session.start(model,action); // Repeated clicks must not submit additional writes.
                    session.pump(model); std::this_thread::sleep_for(std::chrono::milliseconds{5});
                }
                expect(!model.busy,"Delayed inventory request never completed");
                std::cout<<"Inventory action "<<static_cast<int>(action)<<": "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<" ms\n";
            };
            run(frontend::InventoryAction::refresh);
            expect(!model.online && !model.error.empty() && model.filtered_items().size()>80U,
                "Failed initial request blanked the installed skin browser");
            model.owned_only=true;
            expect(model.filtered_items().empty(),"Failed initial request fabricated ownership");
            model.owned_only=false;
            run(frontend::InventoryAction::refresh);
            expect(model.online && model.error.empty() && model.filtered_items().size()>80U,
                "Compatible account snapshot did not restore the skin browser");
            const auto select=[&] {
                model.kind_filter=1; model.owned_only=true; model.page=model.selected=model.slot_index=0;
            };
            select(); run(frontend::InventoryAction::equip_weapon);
            expect(model.data.revision=="3" && model.data.equipped.size()==2U,"Atomic equip lost a view or used a stale revision");
            select(); run(frontend::InventoryAction::unequip_weapon);
            expect(model.data.revision=="5" && model.data.equipped.empty() && model.error.empty(),"Atomic unequip failed to recover from a stale revision");
            select(); run(frontend::InventoryAction::equip_weapon);
            expect(model.data.revision=="6" && model.data.equipped.size()==2U && model.error.empty(),"Atomic equip could not recover after a conflict");
            model.section=frontend::InventorySection::crates;model.page=model.selected=0;
            run(frontend::InventoryAction::open);
            expect(model.error.empty() && model.reveal && model.data.crates.empty() && model.data.revision=="7",
                "Lost opening response did not recover the saved reward in one response");
            expect(std::ranges::any_of(model.data.items,[](const auto& item){return item.id=="community-aeon-awp-v2"&&item.owned;}),
                "Opening confirmation did not apply authoritative ownership");
            session.start(model, frontend::InventoryAction::refresh);
            expect(!model.busy, "Reward overlay allowed a background action");
            model.reveal.reset();
            session.start(model, frontend::InventoryAction::refresh);
            expect(model.busy, "Cancellation fixture did not start");
            std::this_thread::sleep_for(std::chrono::milliseconds{500});
            session.cancel();
            model = frontend::InventoryMenuModel{};
            // A cancelled same-account response must not restore the old
            // profile or errors over a freshly reset inventory.
            const auto cancel_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            while (std::chrono::steady_clock::now() < cancel_deadline) {
                session.pump(model);
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
            }
            expect(!model.loaded && model.error.empty() && model.data.items.empty(),
                   "Cancelled request restored inventory into the new session");
            return 0;
        }
        if (argc == 4 && std::string_view{argv[1]} == "--http") {
            network::RevivalIdentityConfig config;
            config.api_base = argv[2];
            config.state_path = std::filesystem::path{argv[3]} / "identity.json";
            network::RevivalIdentityService identity{config};
            expect(static_cast<bool>(identity.login("FakeBuilder", "test-password")),
                   "Mock login failed");
            expect(static_cast<bool>(identity.inventory_request({})), "Snapshot transport failed");
            const auto directory = identity.hosted_results_directory();
            std::filesystem::create_directories(directory);
            const auto report = directory / "retry-test.json";
            { std::ofstream file{report}; file << R"({"event_id":"retry-test","relay_lobby_id":"test-relay"})"; }
            expect(identity.flush_hosted_results().uploaded == 0U && std::filesystem::exists(report),
                   "Failed result upload was not retained");
            std::stop_source cancelled; cancelled.request_stop();
            expect(identity.flush_hosted_results(cancelled.get_token()).uploaded == 0U,
                   "Cancelled uploader made progress");
            expect(identity.flush_hosted_results().uploaded == 1U && !std::filesystem::exists(report),
                   "Acknowledged result was not removed");
            network::InventoryRequest action;
            action.kind = network::InventoryRequestKind::equip;
            action.target = "weapon:60:view";
            action.cosmetic_id = "field-weapon-skin-v1";
            action.revision = "2";
            expect(static_cast<bool>(identity.inventory_request(action)), "Equip PUT failed");
            action.kind = network::InventoryRequestKind::unequip;
            expect(static_cast<bool>(identity.inventory_request(action)), "Unequip DELETE failed");
            action.kind = network::InventoryRequestKind::open;
            action.target = "11111111-1111-4111-8111-111111111111";
            action.idempotency_key = action.target;
            action.nonce = std::string(64U, 'a');
            expect(static_cast<bool>(identity.inventory_request(action)), "Open POST failed");
            return 0;
        }
        expect(argc == 3 || argc == 4, "Pass the asset root, receipt vector and optional preview directory");
        const std::filesystem::path root{argv[1]};
        check_verified_model_cache(root);
        check_zero_xp_presentation();
        check_snapshot_compatibility(root);
        check_corrected_skin_bindings(root);
        frontend::InventoryMenuModel menu;
        menu.complete(frontend::inventory_preview_fixture());
        expect(menu.filtered_items().size() > 80U, "Expanded collection missing imports");
        for(const auto i:menu.filtered_items())expect(menu.data.items[i].kind!="weapon_skin"&&menu.data.items[i].kind!="hat"&&menu.data.items[i].kind!="profile_badge","Retired generated art is still active");
        menu.online = false;
        expect(menu.click({625, 524}) == frontend::InventoryAction::none,
               "Offline equip must be read-only");
        menu.kind_filter = 1U;
        expect(menu.filtered_items().size()>=35U,
               "Community weapon filter is missing expanded packs");
        const auto empty = menu.build();
        expect(!empty.empty(), "Empty collection must render");
        menu.kind_filter = 0U;
        menu.move_selection(19);
        menu.rarity_filter = 5U;
        menu.selected = 0U;
        expect(menu.selected_item() && menu.selected_item()->rarity == "legendary",
               "Rarity filter lost selected item");
        auto before = frontend::inventory_preview_fixture();
        for (auto& item : before.items)
            item.owned = false;
        before.pity = {9U, 39U, 99U};
        const auto odds = frontend::inventory_effective_odds(before);
        expect(odds[4] == 1.0 && odds[0] == 0.0, "Strongest guarantee must win");
        nlohmann::json receipt;
        {
            std::ifstream input{argv[2]};
            input >> receipt;
        }
        before.crates = {{receipt.at("crate_id"), "2", receipt.at("commitment")}};
        expect(frontend::verify_inventory_receipt(receipt, before),
               "Native RNG disagrees with backend receipt vector");
        auto tampered = receipt;
        tampered["seed"] = std::string(64U, '0');
        expect(!frontend::verify_inventory_receipt(tampered, before), "Tampered seed verified");
        tampered = receipt;
        tampered["pity_after"] = {1, 1, 1};
        expect(!frontend::verify_inventory_receipt(tampered, before), "Tampered pity verified");
        before.crates[0].commitment = std::string(64U, '0');
        expect(!frontend::verify_inventory_receipt(receipt, before),
               "Changed pre-grant commitment verified");
        const auto base = world::load_weapon_models(root, 60U);
        const world::WeaponCosmeticFinish finish{"kv6/assaultRifle.kv6", {172U, 144U, 90U}};
        const auto skin =
            world::load_weapon_models(root, 60U, {1.0F, 1.0F, 1.0F}, std::nullopt, 1U, &finish);
        expect(base && skin, "Weapon models did not load");
        bool recolored{};
        for (std::size_t i = 0U; i < base.models->first_person_parts.size(); ++i) {
            same_geometry(base.models->first_person_parts[i], skin.models->first_person_parts[i]);
            for (std::size_t vertex = 0U;
                 vertex < base.models->first_person_parts[i].vertices.size();
                 ++vertex)
                recolored |= base.models->first_person_parts[i].vertices[vertex].abgr !=
                             skin.models->first_person_parts[i].vertices[vertex].abgr;
        }
        expect(recolored, "Equipped weapon finish did not change its appearance");
        const auto body = world::load_class_models(root, 12U);
        const auto uniform =
            world::load_class_models(root, 12U, {44U, 117U, 179U, 255U}, 1U, finish.palette);
        expect(body && uniform, "Class models did not load");
        same_geometry(body.models->standing_preview, uniform.models->standing_preview);
        auto raw = world::Kv6Model::load_file(root / "kv6/Character_Engineer_Body.kv6");
        expect(raw.has_value(), "KV6 missing");
        const auto voxels = raw->voxels();
        raw->apply_cosmetic_palette(finish.palette);
        for (std::size_t i = 0U; i < voxels.size(); ++i) {
            const auto color = voxels[i].color;
            if (color.green == 0U && color.red == color.blue &&
                (color.red == 0U || color.red == 64U || color.red == 128U || color.red == 192U))
                expect(color == raw->voxels()[i].color, "Team material marker changed");
        }
        auto data = frontend::inventory_preview_fixture();
        const auto pixels =
            frontend::build_inventory_preview(*frontend::find_inventory_cosmetic("community-stg44-v2"), root, false, 0.65, 1.0);
        expect(pixels.size() == 540U * 264U * 4U, "Preview raster size invalid");
        std::size_t visible{};
        for (std::size_t i = 3U; i < pixels.size(); i += 4U)
            visible += pixels[i] != 0U ? 1U : 0U;
        expect(visible > 1000U, "Preview is blank");
        network::CosmeticAppearances appearances;
        const auto envelope=[](const std::string& json) {
            const auto value=std::string{static_cast<char>(240)}+"BSC1"+json;
            return std::vector<std::byte>{reinterpret_cast<const std::byte*>(value.data()),
                                         reinterpret_cast<const std::byte*>(value.data()+value.size())};
        };
        expect(appearances.ingest(envelope(R"({"player_id":7,"items":{"weapon:6:world":"community-lee-enfield-v2"}})")),"Appearance packet rejected");
        expect(appearances.item(7U,"weapon:6:world")=="community-lee-enfield-v2","Appearance slot lost");
        expect(!appearances.ingest(envelope(R"({"player_id":999,"items":{}})")),"Invalid player accepted");
        expect(!appearances.ingest(envelope(R"({"player_id":4294967297,"items":{}})")),"Overflowed player accepted");
        expect(!appearances.ingest(envelope(R"({"player_id":7,"items":{"weapon:6:world":"../../models"}})")),"Asset path accepted as cosmetic ID");
        expect(appearances.item(7U,"weapon:6:world")=="community-lee-enfield-v2","Malformed packet destroyed last valid outfit");
        expect(appearances.ingest(envelope(R"({"player_id":7,"items":{}})")),"Unequip update rejected");
        expect(appearances.item(7U,"weapon:6:world").empty(),"Unequip retained stale appearance");
        appearances.clear();
        for (const auto& item : data.items) if (item.kind=="weapon_model") {
            expect(!item.author.empty() && !item.source.empty(),"Original author credit is missing");
            for (const auto& parent : item.parents) {
                const auto replacement=frontend::inventory_weapon_finish(&item,root,parent.tool);
                expect(replacement && replacement->replacement,"Bundled weapon failed verification");
                const auto models=world::load_weapon_models(root,parent.tool,{1,1,1},std::nullopt,1U,&*replacement);
                if (replacement->sight_tags) {
                    const auto& tags=*replacement->sight_tags;
                    const auto pose=world::evaluate_weapon_sight(parent.tool);
                    for (const auto& tag : {tags.rear,tags.front}) {
                        const auto point=world::sight_tag_position(tags,tag,replacement->scale*static_cast<float>(pose.model_scale));
                        expect(std::abs(point[0])<0.00001F && std::abs(point[1])<0.00001F && point[2]>0.0F,
                               "Tagged sight misses the firing axis");
                    }
                    expect(models.models->sight.has_value() && !models.models->pin.has_value(),
                           "Tagged sight retained a parent pin");
                    auto parent_optic=*replacement;parent_optic.sight_tags.reset();parent_optic.sight_pivot.reset();
                    const auto fallback=world::load_weapon_models(root,parent.tool,{1,1,1},std::nullopt,1U,&parent_optic);
                    const auto retail=world::load_weapon_models(root,parent.tool);
                    expect(fallback.models->sight->vertices.size()==retail.models->sight->vertices.size(),
                           "Parent optic fallback kept the replacement body");
                }
                expect(models && models.models->first_person_parts.size()==1U && models.models->third_person_parts.size()==1U,
                       "Whole weapon retained an unrelated parent model part");
                const auto& mesh=models.models->first_person_parts.front();
                const auto* definition=world::find_weapon_definition(parent.tool);
                expect(models.models->sight.has_value()==!definition->sight_model_asset.empty(),"Replacement removed the working aimed view");
                for (const auto quality:std::array<std::uint8_t,2>{2U,3U}) {
                    const auto lod=world::load_weapon_models(root,parent.tool,{1,1,1},std::nullopt,quality,&*replacement);
                    expect(static_cast<bool>(lod),"Replacement failed at reduced model quality");
                    if (models.models->sight) same_geometry(*models.models->sight,*lod.models->sight);
                    const auto& lower=lod.models->first_person_parts.front();
                    for (std::size_t axis{};axis<3U;++axis) {
                        expect(std::abs(lower.minimum[axis]-mesh.minimum[axis])<=3.1F*replacement->scale &&
                               std::abs(lower.maximum[axis]-mesh.maximum[axis])<=3.1F*replacement->scale,
                               "Model quality moved the replacement away from its grip");
                    }
                }
                if (item.id=="community-stg44-v2") {
                    expect(mesh.maximum[2]-mesh.minimum[2]<43.0F,"STG44 still uses the oversized original fit");
                    expect(std::abs((40.0F-replacement->pivot[1])*replacement->scale+11.0F)<0.01F &&
                           std::abs((replacement->pivot[2]-25.0F)*replacement->scale+3.5F)<0.01F,
                           "STG44 grip does not meet its parent hand anchor");
                    expect(replacement->sight_pivot.has_value(),"STG44 custom iron sight is missing");
                    const auto sight=world::evaluate_weapon_sight(parent.tool);
                    const auto& pivot=*replacement->sight_pivot;
                    expect(std::abs((5.0F-pivot[0])*replacement->scale*sight.model_scale+sight.position.x)<0.001 &&
                           std::abs((pivot[2]-4.0F)*replacement->scale*sight.model_scale+sight.position.y)<0.001,
                           "STG44 iron sight is not aligned with the firing axis");
                }
                expect(!mesh.empty(),"Community model is empty");
                for(const auto& v : mesh.vertices) expect(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z),"Nonfinite weapon mesh");
            }
            expect(!frontend::inventory_weapon_finish(&item,root,255U),"Weapon cosmetic changed an incompatible tool");
            expect(!frontend::inventory_weapon_finish(&item,root/"missing-root",item.parents.front().tool),"Missing pack did not fall back to parent");
        }
        for (const auto& item : data.items) if(item.enabled && item.kind=="hat") {
            const auto head=frontend::inventory_verified_model(item,root);
            expect(static_cast<bool>(head),"Helmet asset failed verification");
            const auto dressed=world::load_class_models(root,12U,{44U,117U,179U,255U},1U,std::nullopt,head.get());
            expect(static_cast<bool>(dressed),"Helmet class assembly failed");
            same_geometry(body.models->standing_preview,dressed.models->standing_preview);
        }
        const auto weapon_pool=frontend::inventory_crate_pool(data,"weapons-v2");
        expect(weapon_pool.items.size()==11U,"Weapon crate pool mixed categories");
        expect(frontend::inventory_crate_pool(data,"supply-v1").items.size()==20U,"Legacy crate pool changed");
        expect(frontend::inventory_crate_pool(data,"unknown-v99").items.empty(),"Unknown crate did not fail closed");
        {
            nlohmann::json vectors;
            std::ifstream input{std::filesystem::path{argv[2]}.parent_path()/"inventory-receipts-v2.json"};
            input>>vectors;
            for(const auto& vector : vectors) {
                auto snapshot=frontend::inventory_preview_fixture();
                for(auto& item : snapshot.items) item.owned=false;
                snapshot.pity={9U,39U,99U};
                for(const auto* family : {"weapons","characters","cosmetics"}) snapshot.pity_by_family[family]=snapshot.pity;
                snapshot.crates={{vector.at("crate_id"),"2",vector.at("commitment"),vector.at("catalog_version")}};
                expect(frontend::verify_inventory_receipt(vector,snapshot),"Crate family RNG differs between C++ and server");
                snapshot.crates[0].catalog_version="unknown-v99";
                expect(!frontend::verify_inventory_receipt(vector,snapshot),"Receipt accepted the wrong crate family");
            }
        }
        if(argc == 4) {
            const std::filesystem::path directory{argv[3]};
            std::filesystem::create_directories(directory);
            for(const auto& item : data.items) if(item.enabled && item.kind!="profile_badge" && item.kind!="weapon_skin") {
                const auto rgba=frontend::build_inventory_preview(item,root,false,.65,1.0);
                std::ofstream output{directory/(item.id+".rgba"),std::ios::binary};
                output.write(reinterpret_cast<const char*>(rgba.data()),static_cast<std::streamsize>(rgba.size()));
            }
        }
        std::cout << "Inventory ownership, RNG, geometry and preview checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
