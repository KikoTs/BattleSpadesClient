#include "battlespades/updater/file_util.hpp"

#include <fstream>
#include <iterator>

namespace battlespades::updater {

std::filesystem::path path_from_utf8(std::string_view text) {
    const std::u8string encoded{reinterpret_cast<const char8_t*>(text.data()), text.size()};
    return std::filesystem::path{encoded};
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::optional<std::string> read_text_file(const std::filesystem::path& file, std::string& error) {
    std::ifstream input{file, std::ios::binary};
    if (!input) {
        error = "cannot open " + path_to_utf8(file);
        return std::nullopt;
    }
    std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        error = "cannot read " + path_to_utf8(file);
        return std::nullopt;
    }
    return text;
}

std::optional<std::vector<std::uint8_t>> read_binary_file(const std::filesystem::path& file,
                                                          std::string& error) {
    auto text = read_text_file(file, error);
    if (!text.has_value()) return std::nullopt;
    return std::vector<std::uint8_t>{text->begin(), text->end()};
}

bool write_file_atomic(const std::filesystem::path& file, std::string_view bytes, std::string& error) {
    auto temporary = file;
    temporary += ".battlespades-tmp";
    if (file.has_parent_path()) {
        std::error_code ignored;
        std::filesystem::create_directories(file.parent_path(), ignored);
    }
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        if (!output) {
            error = "cannot create " + path_to_utf8(temporary);
            return false;
        }
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            error = "cannot write " + path_to_utf8(temporary);
            std::error_code ignored;
            output.close();
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
    std::error_code code;
    std::filesystem::rename(temporary, file, code);
    if (code) {
        error = "cannot replace " + path_to_utf8(file) + ": " + code.message();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

std::optional<std::filesystem::path> backup_file(const std::filesystem::path& file,
                                                 const std::filesystem::path& backup_dir,
                                                 std::string_view label, std::string& error) {
    std::error_code code;
    std::filesystem::create_directories(backup_dir, code);
    if (code) {
        error = "cannot create backup folder " + path_to_utf8(backup_dir) + ": " + code.message();
        return std::nullopt;
    }
    const auto stem = path_to_utf8(file.stem());
    const auto extension = path_to_utf8(file.extension());
    for (int attempt = 0; attempt < 1000; ++attempt) {
        std::string name = stem + "-" + std::string{label};
        if (attempt != 0) name += "-" + std::to_string(attempt);
        const auto target = backup_dir / path_from_utf8(name + extension);
        if (std::filesystem::exists(target, code)) continue;
        std::filesystem::copy_file(file, target, std::filesystem::copy_options::none, code);
        if (code) {
            error = "cannot back up " + path_to_utf8(file) + ": " + code.message();
            return std::nullopt;
        }
        return target;
    }
    error = "too many backups of " + path_to_utf8(file);
    return std::nullopt;
}

std::string generic_utf8(const std::filesystem::path& path) {
    const auto encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

} // namespace battlespades::updater
