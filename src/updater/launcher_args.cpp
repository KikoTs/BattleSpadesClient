#include "battlespades/updater/launcher_args.hpp"

#include <cctype>

namespace battlespades::updater {

bool looks_like_steam_command(std::string_view argument) noexcept {
    if (argument.size() < 5U || argument.front() == '-' || argument.front() == '+') return false;
    const auto suffix = argument.substr(argument.size() - 4U);
    return suffix[0] == '.' && std::tolower(static_cast<unsigned char>(suffix[1])) == 'e' &&
           std::tolower(static_cast<unsigned char>(suffix[2])) == 'x' &&
           std::tolower(static_cast<unsigned char>(suffix[3])) == 'e';
}

LauncherArguments parse_launcher_arguments(const std::vector<std::string>& arguments) {
    LauncherArguments parsed;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        if (argument == "--no-update") {
            parsed.no_update = true;
        } else if (argument == "--rollback") {
            parsed.rollback = true;
        } else if (argument == "--update-only") {
            parsed.update_only = true;
        } else if (argument == "--install-component") {
            if (index + 1U >= arguments.size() || arguments[index + 1U].empty()) {
                parsed.error = "--install-component requires a component name";
                return parsed;
            }
            parsed.install_components.push_back(arguments[++index]);
        } else if (argument == "--update-api" || argument == "--update-manifest") {
            if (index + 1U >= arguments.size()) {
                parsed.error = argument + " requires a URL";
                return parsed;
            }
            (argument == "--update-api" ? parsed.api_url : parsed.manifest_url) = arguments[++index];
        } else if (parsed.dropped_command.empty() && looks_like_steam_command(argument)) {
            parsed.dropped_command = argument;
        } else {
            parsed.forwarded.push_back(argument);
        }
    }
    return parsed;
}

std::string quote_windows_argument(std::string_view argument) {
    if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string_view::npos) {
        return std::string{argument};
    }
    std::string quoted = "\"";
    for (std::size_t index = 0;; ++index) {
        std::size_t backslashes = 0U;
        while (index < argument.size() && argument[index] == '\\') {
            ++index;
            ++backslashes;
        }
        if (index == argument.size()) {
            // Double trailing backslashes so they do not escape the closing quote.
            quoted.append(backslashes * 2U, '\\');
            break;
        }
        if (argument[index] == '"') {
            quoted.append(backslashes * 2U + 1U, '\\');
            quoted.push_back('"');
        } else {
            quoted.append(backslashes, '\\');
            quoted.push_back(argument[index]);
        }
    }
    quoted.push_back('"');
    return quoted;
}

std::string build_windows_command_line(std::string_view executable,
                                       const std::vector<std::string>& arguments) {
    // argv[0] is parsed by different rules (no escapes), so always plain-quote it.
    std::string line = "\"" + std::string{executable} + "\"";
    for (const auto& argument : arguments) {
        line.push_back(' ');
        line += quote_windows_argument(argument);
    }
    return line;
}

} // namespace battlespades::updater
