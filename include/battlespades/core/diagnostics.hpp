#pragma once

#include <string_view>

namespace battlespades::core {

/**
 * Appends one UTC-timestamped line to stderr, which graphical launches redirect
 * to BattleSpadesClient.log beside the executable.
 *
 * Categories are short tags such as "social" or "lobby". Callers must not pass
 * credentials, tokens or other secrets. Thread-safe; never throws.
 */
void diagnostic(std::string_view category, std::string_view message) noexcept;

} // namespace battlespades::core
