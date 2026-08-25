#include "battlespades/frontend/ugc_project_repository.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace battlespades::frontend;

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        root = std::filesystem::temp_directory_path() /
               ("aos-ugc-repository-" + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(root); }
    std::filesystem::path root;
};

void write(const std::filesystem::path& path, std::string_view value) {
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream.write(value.data(), static_cast<std::streamsize>(value.size()));
}

void scan_recovers_triplets_and_isolates_bad_sidecars() {
    TemporaryDirectory temporary;
    write(temporary.root / "Good.ugc",
          R"({"title":"Good Map","aos_ugc_handle":18446744073709551615,"modified_since_publish":true,"tags":["map","tdm"]})");
    write(temporary.root / "Good.vxl", "vxl");
    write(temporary.root / "Good.txt", "meta");
    write(temporary.root / "Good.png", "png");
    write(temporary.root / "Broken.ugc", "{");

    const auto scan = scan_ugc_projects(temporary.root);
    expect(scan.maps.size() == 1U && scan.maps.front().uid == "Good.ugc" &&
               scan.maps.front().title == "Good Map",
           "valid sidecar/VXL triplet must become one publish row");
    expect(scan.maps.front().state == UgcLocalMapState::unpublished &&
               scan.maps.front().has_publishable_mode() &&
               scan.maps.front().modes.front().mode_id == "tdm",
           "unpublished handle and TDM tag must retain their exact semantics");
    expect(scan.maps.front().preview_asset == (temporary.root / "Good.png").string(),
           "local PNG preview must retain an absolute renderer path");
    expect(scan.warnings.size() == 1U,
           "one malformed project must be reported without hiding valid rows");
}

void missing_vxl_is_visible_but_not_publishable() {
    TemporaryDirectory temporary;
    write(temporary.root / "NeedsData.ugc",
          R"({"title":"Needs Data","tags":["map","ctf"]})");
    const auto scan = scan_ugc_projects(temporary.root);
    expect(scan.maps.size() == 1U &&
               scan.maps.front().state == UgcLocalMapState::data_required &&
               !scan.maps.front().has_publishable_mode(),
           "sidecar without its VXL must remain visible as data-required");
}

void delete_uses_opaque_filename_and_rejects_traversal() {
    TemporaryDirectory temporary;
    for (const auto extension : {".ugc", ".vxl", ".txt", ".png"}) {
        write(temporary.root / (std::string{"DeleteMe"} + extension), "data");
    }
    std::string error;
    expect(!delete_ugc_project(temporary.root, "../DeleteMe.ugc", error) && !error.empty(),
           "repository deletion must reject parent traversal");
    expect(delete_ugc_project(temporary.root, "DeleteMe.ugc", error),
           "opaque UID returned by scan must delete its project triplet");
    expect(!std::filesystem::exists(temporary.root / "DeleteMe.ugc") &&
               !std::filesystem::exists(temporary.root / "DeleteMe.vxl") &&
               !std::filesystem::exists(temporary.root / "DeleteMe.txt") &&
               !std::filesystem::exists(temporary.root / "DeleteMe.png"),
           "delete must remove sidecar, VXL, metadata and preview siblings");
}

} // namespace

int main() {
    try {
        scan_recovers_triplets_and_isolates_bad_sidecars();
        missing_vxl_is_visible_but_not_publishable();
        delete_uses_opaque_filename_and_rejects_traversal();
        std::cout << "3/3 tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
