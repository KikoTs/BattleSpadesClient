#pragma once

#include "battlespades/updater/file_util.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

namespace updater_test {

inline void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error{message};
}

/// A unique scratch folder under the system temp directory, removed on exit.
class TempDir {
public:
    explicit TempDir(std::string_view name) {
        std::random_device random;
        path_ = std::filesystem::temp_directory_path() /
                ("battlespades-" + std::string{name} + "-" + std::to_string(random()) + "-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

inline void write_file(const std::filesystem::path& file, std::string_view text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream output{file, std::ios::binary | std::ios::trunc};
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error{"cannot write " + battlespades::updater::path_to_utf8(file)};
}

inline std::string read_file(const std::filesystem::path& file) {
    std::string error;
    auto text = battlespades::updater::read_text_file(file, error);
    if (!text.has_value()) throw std::runtime_error{error};
    return *text;
}

inline void replace_all(std::string& text, std::string_view from, std::string_view to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
}

template <typename Body>
int run(const char* name, Body&& body) {
    try {
        body();
    } catch (const std::exception& exception) {
        std::cerr << name << ": FAILED: " << exception.what() << '\n';
        return 1;
    }
    std::cout << name << ": passed\n";
    return 0;
}

} // namespace updater_test
