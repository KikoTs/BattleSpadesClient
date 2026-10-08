#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

namespace battlespades::platform {
#if defined(_WIN32)
using SelectSocket = SOCKET;
#else
using SelectSocket = int;
#endif

// Haiku's fd_set macros mix signed descriptors/long literals with unsigned
// fd_mask storage. Keep their implementation warnings local to these wrappers;
// application socket lengths and packet arithmetic still use strict warnings.
#if defined(__HAIKU__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
inline void select_add_socket(SelectSocket socket, fd_set& set) noexcept {
    FD_SET(socket, &set);
}
[[nodiscard]] inline bool select_has_socket(SelectSocket socket, fd_set& set) noexcept {
    return FD_ISSET(socket, &set) != 0;
}
#if defined(__HAIKU__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
} // namespace battlespades::platform
