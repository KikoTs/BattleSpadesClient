#include "battlespades/core/resource_paths.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using battlespades::core::ResourceDiscoveryOptions;
using battlespades::core::ResourceOrigin;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

class TemporaryTree final {
public:
    TemporaryTree() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("battlespades-resource-paths-" + std::to_string(stamp));
        std::filesystem::create_directories(root_);
    }

    ~TemporaryTree() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    TemporaryTree(const TemporaryTree&) = delete;
    TemporaryTree& operator=(const TemporaryTree&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const noexcept {
        return root_;
    }

    [[nodiscard]] std::filesystem::path directory(std::string_view relative) const {
        const auto path = root_ / std::filesystem::path{relative};
        std::filesystem::create_directories(path);
        return std::filesystem::weakly_canonical(path);
    }

private:
    std::filesystem::path root_{};
};

void packaged_roots_override_developer_roots() {
    TemporaryTree tree;
    const auto executable_directory = tree.directory("package/bin");
    const auto packaged_assets = tree.directory("package/bin/assets/original");
    const auto packaged_shaders = tree.directory("package/bin/shaders");
    const auto developer_assets = tree.directory("source/assets/original");
    const auto developer_shaders = tree.directory("source/shaders");

    const auto result = battlespades::core::discover_resource_paths(ResourceDiscoveryOptions{
        executable_directory / "BattleSpadesClient",
        developer_assets,
        developer_shaders,
    });

    expect(static_cast<bool>(result), "complete package roots should resolve");
    expect(result.paths->assets.root == packaged_assets,
           "packaged assets must override source assets");
    expect(result.paths->shaders.root == packaged_shaders,
           "packaged shaders must override source shaders");
    expect(result.paths->assets.origin == ResourceOrigin::executable_adjacent,
           "packaged asset origin must be reported");
    expect(result.paths->shaders.origin == ResourceOrigin::executable_adjacent,
           "packaged shader origin must be reported");
}

void developer_roots_are_a_fallback() {
    TemporaryTree tree;
    const auto executable_directory = tree.directory("build/bin");
    const auto developer_assets = tree.directory("source/assets/original");
    const auto developer_shaders = tree.directory("source/shaders");

    const auto result = battlespades::core::discover_resource_paths(ResourceDiscoveryOptions{
        executable_directory / "BattleSpadesClient",
        developer_assets,
        developer_shaders,
    });

    expect(static_cast<bool>(result), "developer roots should support an uninstalled build");
    expect(result.paths->assets.root == developer_assets, "developer asset root should resolve");
    expect(result.paths->shaders.root == developer_shaders, "developer shader root should resolve");
    expect(result.paths->assets.origin == ResourceOrigin::developer_fallback,
           "developer asset origin must be reported");
    expect(result.paths->shaders.origin == ResourceOrigin::developer_fallback,
           "developer shader origin must be reported");
}

void asset_and_shader_precedence_is_independent() {
    TemporaryTree tree;
    const auto executable_directory = tree.directory("mixed/bin");
    const auto packaged_assets = tree.directory("mixed/bin/assets/original");
    const auto developer_shaders = tree.directory("source/shaders");

    const auto result = battlespades::core::discover_resource_paths(ResourceDiscoveryOptions{
        executable_directory / "BattleSpadesClient",
        tree.root() / "missing-assets",
        developer_shaders,
    });

    expect(static_cast<bool>(result), "mixed package/developer roots should resolve");
    expect(result.paths->assets.root == packaged_assets, "packaged assets should remain selected");
    expect(result.paths->shaders.root == developer_shaders,
           "missing packaged shaders should use only their developer fallback");
    expect(result.paths->assets.origin == ResourceOrigin::executable_adjacent,
           "mixed asset origin should be packaged");
    expect(result.paths->shaders.origin == ResourceOrigin::developer_fallback,
           "mixed shader origin should be developer fallback");
}

void missing_roots_fail_with_attempted_paths() {
    TemporaryTree tree;
    const auto executable_directory = tree.directory("empty/bin");
    const auto developer_assets = tree.root() / "source/assets/original";
    const auto developer_shaders = tree.root() / "source/shaders";

    const auto result = battlespades::core::discover_resource_paths(ResourceDiscoveryOptions{
        executable_directory / "BattleSpadesClient",
        developer_assets,
        developer_shaders,
    });

    expect(!static_cast<bool>(result), "missing immutable roots must fail closed");
    const auto packaged_asset_attempt =
        std::filesystem::weakly_canonical(executable_directory / "assets/original");
    expect(result.error.find(packaged_asset_attempt.string()) != std::string::npos,
           "failure should identify the packaged asset attempt");
    expect(result.error.find(developer_shaders.string()) != std::string::npos,
           "failure should identify the developer shader attempt");
}

void process_image_path_is_absolute() {
    std::string error;
    const auto path = battlespades::core::current_executable_path(error);
    expect(path.has_value(), "supported desktop platforms should expose the process image path");
    expect(path->is_absolute(), "process image path should be absolute");
    expect(error.empty(), "successful process image discovery should clear its error");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"packaged_roots_override_developer_roots", packaged_roots_override_developer_roots},
        {"developer_roots_are_a_fallback", developer_roots_are_a_fallback},
        {"asset_and_shader_precedence_is_independent", asset_and_shader_precedence_is_independent},
        {"missing_roots_fail_with_attempted_paths", missing_roots_fail_with_attempted_paths},
        {"process_image_path_is_absolute", process_image_path_is_absolute},
    };

    std::size_t failures{};
    for (const auto& test : tests) {
        try {
            test.body();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        }
    }

    std::cout << tests.size() - failures << '/' << tests.size() << " tests passed\n";
    return failures == 0U ? 0 : 1;
}
