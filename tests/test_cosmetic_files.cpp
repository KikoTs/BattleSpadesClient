#include "battlespades/world/cosmetic_files.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary};
    output << text;
}

} // namespace

int main(int argc, char** argv) {
    using battlespades::world::resolve_cosmetic_file;
    namespace fs = std::filesystem;

    const auto assets = fs::temp_directory_path() / "aos_cosmetic_files_test" / "assets";
    std::error_code ignored;
    fs::remove_all(assets.parent_path(), ignored);
    write(assets / "client/cosmetics/retail-substitutes.json",
          R"({"schema_version":1,"files":{)"
          R"("client/cosmetics/packs/p/Models/Tracer.kv6":"kv6/tracer.kv6",)"
          R"("client/cosmetics/packs/p/escape.kv6":"../outside.kv6",)"
          R"("client/cosmetics/packs/p/absent.kv6":"kv6/absent.kv6"}})");
    write(assets / "original/kv6/tracer.kv6", "retail");
    write(assets / "client/cosmetics/packs/p/Models/Weapon.kv6", "pack");
    write(assets / "outside.kv6", "outside");

    const auto missing = assets / "client/cosmetics/packs/p/Models/Tracer.kv6";
    expect(fs::equivalent(resolve_cosmetic_file(missing), assets / "original/kv6/tracer.kv6"),
           "a recorded retail-identical file resolves to the imported retail copy");

    const auto present = assets / "client/cosmetics/packs/p/Models/Weapon.kv6";
    expect(resolve_cosmetic_file(present) == present, "an existing pack file is used as-is");

    const auto unknown = assets / "client/cosmetics/packs/p/Models/Other.kv6";
    expect(resolve_cosmetic_file(unknown) == unknown, "an unrecorded missing file is unchanged");

    const auto escape = assets / "client/cosmetics/packs/p/escape.kv6";
    expect(resolve_cosmetic_file(escape) == escape, "a substitute may not leave original/");

    const auto absent = assets / "client/cosmetics/packs/p/absent.kv6";
    expect(resolve_cosmetic_file(absent) == absent,
           "a substitute that was not imported leaves the original path");
    fs::remove_all(assets.parent_path(), ignored);

    // Repository data: every recorded substitute must exist in an imported tree.
    if (argc > 1) {
        const fs::path original{argv[1]};
        const auto root = original.parent_path();
        std::ifstream input{root / "client/cosmetics/retail-substitutes.json"};
        expect(static_cast<bool>(input), "retail-substitutes.json ships with the client assets");
        if (input && fs::is_directory(original)) {
            const auto table = nlohmann::json::parse(input);
            for (const auto& [key, value] : table.at("files").items()) {
                const auto resolved = resolve_cosmetic_file(root / key);
                expect(fs::exists(resolved), "substitute resolves: " + key);
                expect(!fs::exists(root / key) ||
                           resolved == root / key,
                       "substitute is not shadowed: " + key);
                expect(value.get<std::string>().find("..") == std::string::npos,
                       "substitute stays inside original/: " + key);
            }
        }
    }

    if (failures == 0)
        std::cout << "cosmetic file resolution passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
