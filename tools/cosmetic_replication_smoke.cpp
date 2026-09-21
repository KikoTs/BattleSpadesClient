#include "battlespades/network/cosmetic_appearance.hpp"
#include "battlespades/network/live_protocol168_connection.hpp"
#include "battlespades/network/protocol168_weapons.hpp"
#include "battlespades/platform/local_server_process.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <array>
#include <fstream>

int main(int argc,char** argv) {
    using namespace battlespades;
    using namespace std::chrono_literals;
    if(argc!=3) {std::cerr<<"Pass portable server directory and loopback fixture URL\n";return 2;}
    platform::LocalServerLaunchConfig launch;
    launch.bundle_root=argv[1];launch.server_name="Cosmetic compatibility smoke";
    launch.map_name="AncientEgypt";launch.mode="tdm";launch.maximum_players=4U;
    std::string error;
    launch.preferred_port=platform::allocate_local_server_port(29380U,error);
    if(!launch.preferred_port) {std::cerr<<error;return 1;}
    const auto port=std::to_string(launch.preferred_port);
    launch.environment_overrides={{"AOS_MASTER_URL",argv[2]}, {"AOS_MASTER_WRITE_TOKEN","cosmetic-test-only"},
        {"AOS_PUBLIC_HOST","127.0.0.1"},{"AOS_PUBLIC_PORT",port},{"AOS_PUBLIC_QUERY_PORT",port},
        {"AOS_SERVER_ID","127.0.0.1:"+port}};
    platform::LocalServerProcess server;
    if(!server.start(launch,error)) {std::cerr<<error;return 1;}
    // Allow the portable server to bind before ENet starts its connect retry.
    std::this_thread::sleep_for(2s);
    std::array<network::LiveProtocol168Connection,2U> peers;
    std::array<std::unique_ptr<network::Protocol168WorldBootstrap>,2U> worlds;
    std::array<std::int32_t,2U> loops{};
    for(std::size_t index{};index<2U;++index) {
        network::Protocol168SessionConfig session;
        session.player_name="~"+std::string(14U,index==0U?'N':'L');
        session.team=index==0U?2U:3U;
        if(!peers[index].start({"127.0.0.1",server.port(),30000U},session)) return 1;
    }
    network::CosmeticAppearances appearances;
    bool own_equipped{}, other_equipped{}, unequipped{};
    std::array<std::size_t,2U> gameplay{};
    const auto deadline=std::chrono::steady_clock::now()+45s;
    while(std::chrono::steady_clock::now()<deadline) {
        for(std::size_t index{};index<2U;++index) {
            if(!worlds[index]) {
                worlds[index]=peers[index].take_bootstrap();
                if(worlds[index]) loops[index]=static_cast<std::int32_t>(worlds[index]->next_client_loop_count);
            }
            const auto state=peers[index].status();
            if(state.phase==network::LiveProtocol168Phase::failed || state.phase==network::LiveProtocol168Phase::disconnected) {
                std::cerr<<"Peer "<<index<<" failed: "<<state.error<<" server log="<<server.log_path()<<'\n';return 1;
            }
            for(const auto& packet : peers[index].take_inbound(256U)) {
                if(!packet.empty() && std::to_integer<std::uint8_t>(packet[0])==network::CosmeticAppearances::packet_id) {
                    if(index==1U) {std::cerr<<"Cosmetic packet leaked to legacy peer\n";return 1;}
                    if(!appearances.ingest(packet)) {std::cerr<<"Malformed cosmetic wire envelope\n";return 1;}
                    if(worlds[0] && worlds[1]) {
                        const auto own=appearances.item(worlds[0]->local_player_id,"weapon:6:world");
                        own_equipped |= own=="community-lee-enfield-v2";
                        other_equipped |= appearances.item(worlds[1]->local_player_id,"weapon:60:world")=="community-honey-badger-v2";
                        unequipped |= own_equipped && own.empty();
                    }
                } else ++gameplay[index];
            }
            if(worlds[index]) {
                network::ClientDataPacket input;
                input.loop_count=loops[index]++;input.player_id=worlds[index]->local_player_id;
                if(const auto* player=worlds[index]->roster.player(input.player_id)) input.tool_id=player->tool_id;
                input.opaque_state=network::protocol168_client_data_opaque_state(input.loop_count);
                input.orientation={0.0F,0.0F,-1.0F};
                if(!peers[index].send(network::encode_packet(input))) return 1;
            }
        }
        if(own_equipped && other_equipped && unequipped && gameplay[0]>60U && gameplay[1]>60U) {
            std::cout<<"Two live ENet peers: equipped models, remote outfit, unequip and delayed master lookup passed; zero extensions to legacy peer. Gameplay packets="<<gameplay[0]<<","<<gameplay[1]<<'\n';
            return 0;
        }
        std::this_thread::sleep_for(16ms);
    }
    std::cerr<<"Cosmetic replication timeout: own="<<own_equipped<<" other="<<other_equipped<<" clear="<<unequipped<<" gameplay="<<gameplay[0]<<","<<gameplay[1]<<" server log="<<server.log_path()<<'\n';
    std::ifstream log{server.log_path()};std::cerr<<log.rdbuf();
    return 1;
}
