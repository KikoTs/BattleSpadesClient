#pragma once

#include "battlespades/frontend/create_match_menu.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace battlespades::frontend {

/** Last local host choices; never contains a room token, password or account. */
[[nodiscard]] std::optional<CreateMatchConfiguration> load_local_match_configuration(
    const std::filesystem::path& path, std::string& error);
[[nodiscard]] bool save_local_match_configuration(
    const std::filesystem::path& path, const CreateMatchConfiguration& configuration,
    std::string& error);

} // namespace battlespades::frontend
