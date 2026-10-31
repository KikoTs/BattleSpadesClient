#pragma once

#include <algorithm>
#include <array>
#include <chrono>

#include <enet/enet.h>

namespace battlespades::network::detail {

// Some game-host filters admit ENet only after seeing an A2S_INFO query from
// the same UDP source port. Browser queries use a separate socket, so they
// cannot prepare the actual connection. Send the standard query from ENet's
// socket before CONNECT and allow the reply to open that path first. Sending
// both back-to-back can cause the filter to reject the entire connection.
// This optional exchange consumes at most 500 ms of the existing deadline;
// servers without A2S still get a normal ENet attempt. No peer exists yet.
template <typename Canceled>
void prepare_connection_query(ENetHost& host, const ENetAddress& address,
                              std::chrono::steady_clock::time_point deadline,
                              Canceled canceled) {
    char query[] = "\xff\xff\xff\xff" "TSource Engine Query";
    ENetBuffer buffer{};
    buffer.data = query;
    buffer.dataLength = sizeof(query); // Includes the required terminating NUL.
    if (enet_socket_send(host.socket, &address, &buffer, 1U) < 0) return;
    const auto now = std::chrono::steady_clock::now();
    // Reserve most of even a deliberately short timeout for ENet itself.
    const auto maximum_wait = std::chrono::steady_clock::duration{std::chrono::milliseconds{500}};
    deadline = now + (std::min)((deadline - now) / 4, maximum_wait);
    std::array<enet_uint8, 4096U> reply{};
    buffer.data = reply.data();
    buffer.dataLength = reply.size();
    while (!canceled() && std::chrono::steady_clock::now() < deadline) {
        enet_uint32 condition = ENET_SOCKET_WAIT_RECEIVE;
        if (enet_socket_wait(host.socket, &condition, 10U) != 0) return;
        if ((condition & ENET_SOCKET_WAIT_RECEIVE) == 0U) continue;
        ENetAddress source{};
        const auto size = enet_socket_receive(host.socket, &source, &buffer, 1U);
        if (size < 0) return;
        if (source.host != address.host || source.port != address.port || size < 6 ||
            reply[0] != 0xffU || reply[1] != 0xffU ||
            reply[2] != 0xffU || reply[3] != 0xffU) continue;
        // Either a challenge or an INFO response confirms the query path.
        if ((reply[4] == 'A' && size >= 9) || reply[4] == 'I') return;
    }
}

} // namespace battlespades::network::detail
