#include "battlespades/network/protocol168_ugc.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <variant>

namespace {

using namespace battlespades::network;

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

template <typename Packet>
void expect_round_trip(const Packet& packet, const char* message) {
    const auto wire = encode_packet(packet);
    const auto decoded = decode_ugc_control_packet(wire);
    expect(decoded && std::holds_alternative<Packet>(*decoded.packet) &&
               std::get<Packet>(*decoded.packet) == packet,
           message);
}

} // namespace

int main() {
    try {
        expect_round_trip(SetUgcEditModePacket{12U},
                          "SetUGCEditMode must round-trip its target mode");
        expect_round_trip(RequestUgcEntitiesPacket{7U, true},
                          "ReqestUGCEntities must preserve mode and UGC flag");
        for (std::uint8_t raw{}; raw <= 4U; ++raw) {
            expect_round_trip(UgcMessagePacket{static_cast<UgcMessageCode>(raw)},
                              "every retail UGCMessage command must round-trip");
        }
        expect_round_trip(UgcMapLoadingFromHostPacket{87U},
                          "host loading progress must round-trip");
        expect(!decode_ugc_control_packet({}), "empty UGC packets must fail closed");
        const std::array invalid_bool{std::byte{99U}, std::byte{1U}, std::byte{2U}};
        expect(!decode_ugc_control_packet(invalid_bool),
               "ReqestUGCEntities must reject noncanonical booleans");
        const std::array invalid_message{std::byte{100U}, std::byte{5U}};
        expect(!decode_ugc_control_packet(invalid_message),
               "UGCMessage must reject unknown lifecycle commands");
        expect(encode_packet(UgcMapLoadingFromHostPacket{101U}).empty(),
               "loading progress above 100 must not be serialized");
        std::cout << "Protocol 168 UGC control packet tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
