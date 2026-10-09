#include "battlespades/platform/workshop_sync.hpp"

#include <algorithm>
#include <atomic>
#include <barrier>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace battlespades::platform;

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

[[nodiscard]] std::vector<unsigned char> bytes(std::string_view text) {
    return {text.begin(), text.end()};
}

/** A scratch directory removed again when the test ends. */
class ScratchDirectory final {
public:
    explicit ScratchDirectory(std::string_view name)
        : path_{std::filesystem::temp_directory_path() /
                ("aos_workshop_sync_" + std::string{name})} {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ~ScratchDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void write_text(const std::filesystem::path& file, std::string_view text) {
    std::ofstream output{file, std::ios::binary};
    output << text;
}

[[nodiscard]] std::string read_text(const std::filesystem::path& file) {
    std::ifstream input{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void stems_use_the_full_published_file_id() {
    expect(workshop_subscribed_stem(3U) == "Subscribed_3", "retail's %llu stem");
    expect(workshop_subscribed_stem(5'000'000'123ULL) == "Subscribed_5000000123",
           "ids beyond 32 bits keep every digit");
    expect(parse_workshop_subscribed_stem("Subscribed_119024623") == 119024623ULL,
           "a stem parses back to its id");
    expect(!parse_workshop_subscribed_stem("Subscribed_").has_value(), "no id");
    expect(!parse_workshop_subscribed_stem("Subscribed_012").has_value(), "leading zero");
    expect(!parse_workshop_subscribed_stem("Subscribed_12a").has_value(), "trailing junk");
    expect(!parse_workshop_subscribed_stem("Subscribed_0").has_value(), "zero is no id");
    expect(!parse_workshop_subscribed_stem("Custommap_1").has_value(), "not a subscription");
    expect(!parse_workshop_subscribed_stem("Subscribed_99999999999999999999999").has_value(),
           "overflow");
}

void containers_round_trip_in_retail_layout() {
    const auto vxl = bytes("voxels!");
    const auto ugc = bytes("{\"title\": \"Castle\"}");
    const auto container = build_aos_container(vxl, ugc);
    // Retail's publisher: "VXL\0", little-endian size, data, then "UGC\0".
    expect(container.size() == vxl.size() + ugc.size() + 16U, "two 8-byte headers");
    expect(container[0] == 'V' && container[1] == 'X' && container[2] == 'L' &&
               container[3] == 0U,
           "VXL tag first");
    expect(container[4] == vxl.size() && container[5] == 0U && container[6] == 0U &&
               container[7] == 0U,
           "length is little-endian");
    expect(container[8U + vxl.size()] == 'U', "UGC tag second");
    std::string error;
    const auto parsed = parse_aos_container(container, error);
    expect(parsed.has_value(), "round trip parses: " + error);
    expect(parsed->vxl == vxl && parsed->ugc == ugc, "payloads survive");
}

void containers_accept_either_chunk_order() {
    // UGC first, built by hand.
    std::vector<unsigned char> swapped{'U', 'G', 'C', 0U, 2U, 0U, 0U, 0U, '{', '}',
                                       'V', 'X', 'L', 0U, 1U, 0U, 0U, 0U, 'x'};
    std::string error;
    const auto parsed = parse_aos_container(swapped, error);
    expect(parsed.has_value(), "UGC before VXL is accepted: " + error);
    expect(parsed->vxl == bytes("x") && parsed->ugc == bytes("{}"), "both payloads read");
}

void broken_containers_are_refused() {
    std::string error;
    expect(!parse_aos_container({}, error).has_value(), "empty");
    const auto good = build_aos_container(bytes("vox"), bytes("{}"));
    auto truncated = good;
    truncated.pop_back();
    expect(!parse_aos_container(truncated, error).has_value(), "truncated UGC chunk");
    expect(error.find("truncated") != std::string::npos, "says it is truncated");
    std::vector<unsigned char> unknown{'P', 'N', 'G', 0U, 1U, 0U, 0U, 0U, 'x'};
    expect(!parse_aos_container(unknown, error).has_value(), "unknown tag");
    // "VXLX" is not "VXL\0": retail compares with strcmp.
    std::vector<unsigned char> unterminated{'V', 'X', 'L', 'X', 1U, 0U, 0U, 0U, 'x'};
    expect(!parse_aos_container(unterminated, error).has_value(), "tag must be NUL-terminated");
    const auto vxl_only = std::vector<unsigned char>(good.begin(), good.begin() + 11);
    expect(!parse_aos_container(vxl_only, error).has_value(), "missing UGC");
    const auto not_json = build_aos_container(bytes("vox"), bytes("title=Castle"));
    expect(!parse_aos_container(not_json, error).has_value(), "sidecar must be a JSON object");
    std::vector<unsigned char> huge{'V', 'X', 'L', 0U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 'x'};
    expect(!parse_aos_container(huge, error).has_value(), "a length past the end is refused");
}

void unsafe_names_are_refused() {
    expect(workshop_safe_file_name("Castle.aos"), "a plain name");
    expect(workshop_safe_file_name("Subscribed_3.ugc"), "a sync name");
    for (const auto* const bad :
         {"", ".", "..", "../evil.vxl", "..\\evil.vxl", "maps/evil.vxl", "C:evil.vxl",
          "C:\\Windows\\evil.dll", "/etc/passwd", "\\\\server\\share", "a..b", "con",
          "CON.txt", "lpt1.aos", "trailing.", "trailing ", " leading", "tab\tname",
          "what?.aos", "pipe|name"}) {
        expect(!workshop_safe_file_name(bad), std::string{"must refuse: "} + bad);
    }
}

void plans_new_changed_kept_and_removed_items() {
    WorkshopIndex index;
    index.items[10U] = {100U, {"Subscribed_10.ugc"}};  // unchanged
    index.items[11U] = {100U, {"Subscribed_11.ugc"}};  // newer on Steam
    index.items[12U] = {100U, {"Subscribed_12.ugc"}};  // unsubscribed
    index.items[13U] = {100U, {"Subscribed_13.ugc"}};  // files deleted by hand
    const std::vector<WorkshopSubscribedItem> subscribed{
        {10U, 100U}, {11U, 200U}, {13U, 100U}, {14U, 50U}, {14U, 50U}, {0U, 1U}};
    const auto plan = plan_workshop_sync(index, subscribed,
                                         [](std::uint64_t id) { return id != 13U; });
    expect(plan.install == std::vector<std::uint64_t>{14U}, "new subscription installs once");
    expect(plan.update == std::vector<std::uint64_t>{11U, 13U},
           "newer version and missing files update");
    expect(plan.keep == std::vector<std::uint64_t>{10U}, "current item is kept");
    expect(plan.remove == std::vector<std::uint64_t>{12U}, "unsubscribed item is removed");
}

void an_older_steam_time_does_not_downgrade() {
    WorkshopIndex index;
    index.items[10U] = {300U, {"Subscribed_10.ugc"}};
    const std::vector<WorkshopSubscribedItem> subscribed{{10U, 200U}};
    const auto plan = plan_workshop_sync(index, subscribed, [](std::uint64_t) { return true; });
    expect(plan.keep.size() == 1U && plan.update.empty(), "older time keeps what is there");
}

void index_round_trips_and_ignores_garbage() {
    WorkshopIndex index;
    index.items[119024623U] = {1355000000U, {"Subscribed_119024623.vxl",
                                             "Subscribed_119024623.ugc"}};
    index.items[5'000'000'123ULL] = {7U, {}};
    const auto text = serialize_workshop_index(index);
    const auto parsed = parse_workshop_index(text);
    expect(parsed.items.size() == 2U, "both entries survive");
    expect(parsed.items.at(119024623U).time_updated == 1355000000U, "time survives");
    expect(parsed.items.at(119024623U).files == index.items[119024623U].files, "files survive");
    expect(parse_workshop_index("something else\n1 2 x\n").items.empty(),
           "a foreign file is no index");
    const auto hostile = parse_workshop_index(
        "battlespades-workshop-index 1\r\n"
        "5 9 ../../evil.dll C:\\x.vxl Subscribed_5.ugc\r\n"
        "nonsense line\r\n0 1 Subscribed_0.ugc\r\n6 99999999999 Subscribed_6.ugc\r\n");
    expect(hostile.items.size() == 1U, "bad ids and times are dropped");
    expect(hostile.items.at(5U).files == std::vector<std::string>{"Subscribed_5.ugc"},
           "unsafe names never enter the index");
}

void installs_files_atomically_and_removes_only_its_own() {
    const ScratchDirectory scratch{"install"};
    const auto maps = scratch.path() / "maps";
    const auto container =
        build_aos_container(bytes("VOXELDATA"), bytes("{\"title\": \"Castle\"}"));
    const std::vector<unsigned char> png{0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU, 1U};
    std::string error;
    const auto entry = install_workshop_map(maps, 77U, 1234U, container, png, error);
    expect(entry.has_value(), "install succeeds: " + error);
    expect(entry->time_updated == 1234U, "records the version");
    expect(entry->files == std::vector<std::string>{"Subscribed_77.vxl", "Subscribed_77.png",
                                                    "Subscribed_77.txt", "Subscribed_77.ugc"},
           "retail names, sidecar last");
    expect(read_text(maps / "Subscribed_77.vxl") == "VOXELDATA", "vxl payload");
    expect(read_text(maps / "Subscribed_77.ugc") == "{\"title\": \"Castle\"}", "ugc payload");
    expect(read_text(maps / "Subscribed_77.txt") == read_text(maps / "Subscribed_77.ugc"),
           ".txt is a copy of the sidecar");
    for (const auto& file : std::filesystem::directory_iterator{maps}) {
        expect(file.path().extension() != ".partial", "no temporary file is left behind");
    }
    expect(workshop_entry_files_present(maps, *entry), "files are present");

    // A second install replaces in place; a bad container writes nothing.
    const auto bad = install_workshop_map(maps, 78U, 1U, bytes("garbage"), {}, error);
    expect(!bad.has_value() && !std::filesystem::exists(maps / "Subscribed_78.vxl"),
           "nothing is written for a broken item");
    const auto replaced = install_workshop_map(
        maps, 77U, 2000U, build_aos_container(bytes("NEW"), bytes("{}")), {}, error);
    expect(replaced.has_value() && read_text(maps / "Subscribed_77.vxl") == "NEW",
           "an update replaces the voxels");

    // Files the sync never wrote stay, even when a tampered index names them.
    write_text(maps / "Subscribed_3.ugc", "{}");
    write_text(maps / "Subscribed_3.vxl", "retail");
    write_text(scratch.path() / "outside.txt", "keep");
    WorkshopIndexEntry hostile;
    hostile.files = {"Subscribed_3.ugc", "../outside.txt", "Subscribed_77.vxl.bak",
                     "Subscribed_77.exe", "Subscribed_77.vxl", "Subscribed_77.ugc"};
    const auto removed = remove_workshop_map(maps, 77U, hostile);
    expect(removed == 2U, "only Subscribed_77.{vxl,ugc} are removed");
    expect(std::filesystem::exists(maps / "Subscribed_3.ugc") &&
               std::filesystem::exists(maps / "Subscribed_3.vxl"),
           "another id's files are untouched");
    expect(std::filesystem::exists(scratch.path() / "outside.txt"), "nothing outside is touched");
}

void a_png_that_is_not_a_png_is_skipped() {
    const ScratchDirectory scratch{"preview"};
    std::string error;
    const auto entry = install_workshop_map(scratch.path(), 9U, 1U,
                                            build_aos_container(bytes("v"), bytes("{}")),
                                            bytes("<html>404</html>"), error);
    expect(entry.has_value(), "the map still installs");
    expect(!std::filesystem::exists(scratch.path() / "Subscribed_9.png"), "no bogus preview");
}

void index_saves_atomically() {
    const ScratchDirectory scratch{"index"};
    const auto file = scratch.path() / std::string{workshop_index_file_name};
    WorkshopIndex index;
    index.items[42U] = {9U, {"Subscribed_42.ugc"}};
    std::string error;
    expect(save_workshop_index(file, index, error), "saves: " + error);
    expect(load_workshop_index(file).items.at(42U).time_updated == 9U, "loads back");
    expect(load_workshop_index(scratch.path() / "missing").items.empty(),
           "a missing index is empty");
}

void concurrent_preview_publication_is_atomic() {
    const ScratchDirectory scratch{"concurrent-preview"};
    const auto target = scratch.path() / "same-preview.png";
    constexpr unsigned writers = 4U;
    constexpr unsigned rounds = 16U;
    constexpr std::size_t payload_size = 256U * 1024U;
    std::barrier gate{static_cast<std::ptrdiff_t>(writers + 1U)};
    std::atomic<unsigned> failures{};
    std::vector<std::jthread> workers;
    for (unsigned index{}; index < writers; ++index) {
        workers.emplace_back([&, index] {
            const std::vector<unsigned char> payload(payload_size, static_cast<unsigned char>('A' + index));
            for (unsigned round{}; round < rounds; ++round) {
                gate.arrive_and_wait();
                std::string error;
                if (!write_file_atomically(target, payload, error)) ++failures;
                gate.arrive_and_wait();
            }
        });
    }
    bool complete = true;
    for (unsigned round{}; round < rounds; ++round) {
        gate.arrive_and_wait();
        gate.arrive_and_wait();
        // Writers are blocked at the next round while the reader checks the
        // published file: it must be one complete image, never mixed bytes.
        const auto result = read_text(target);
        complete = complete && result.size() == payload_size &&
                   !result.empty() && result.front() >= 'A' && result.front() < 'A' + static_cast<int>(writers) &&
                   std::ranges::all_of(result, [&](char value) { return value == result.front(); });
    }
    workers.clear();
    expect(failures.load() == 0U, "concurrent preview writers must not steal each other's temporary file");
    expect(complete, "every published preview must contain one complete writer payload");
    expect(std::distance(std::filesystem::directory_iterator{scratch.path()},
                         std::filesystem::directory_iterator{}) == 1,
           "no concurrent publication temporary files remain");
}

} // namespace

int main() {
    try {
        stems_use_the_full_published_file_id();
        containers_round_trip_in_retail_layout();
        containers_accept_either_chunk_order();
        broken_containers_are_refused();
        unsafe_names_are_refused();
        plans_new_changed_kept_and_removed_items();
        an_older_steam_time_does_not_downgrade();
        index_round_trips_and_ignores_garbage();
        installs_files_atomically_and_removes_only_its_own();
        a_png_that_is_not_a_png_is_skipped();
        index_saves_atomically();
        concurrent_preview_publication_is_atomic();
        std::cout << "workshop sync tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "workshop sync tests failed: " << error.what() << '\n';
        return 1;
    }
}
