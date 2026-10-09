#include "battlespades/network/demo_stream.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <bit>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace battlespades::network;
namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}
template<typename T> void integer(std::ostream& file, T value) {
    for (std::size_t i{}; i < sizeof(T); ++i)
        file.put(static_cast<char>((value >> (i * 8U)) & 255U));
}
void legacy(const std::filesystem::path& path, float timestamp, std::uint16_t size = 1U) {
    std::ofstream file{path, std::ios::binary};
    file.put(1); file.put(3);
    integer(file, std::bit_cast<std::uint32_t>(timestamp));
    integer(file, size);
    file.put(18);
}
}

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("battlespades-demo-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    try {
        const auto path = directory / "roundtrip.bsdem";
        const std::array first{std::byte{114}, std::byte{7}, std::byte{255}};
        const std::array second{std::byte{5}, std::byte{1}};
        DemoWriter writer;
        check(writer.open(path, 168U), "recording opens");
        check(writer.append(0U, first) && writer.append(50'000U, second) && writer.finish(), "recording writes and closes");
        DemoWriter duplicate;
        check(!duplicate.open(path, 168U), "an existing recording is preserved");
        const auto contested_path = directory / "concurrent.bsdem";
        std::atomic<unsigned> acquired{};
        std::barrier gate{2};
        const auto attempt = [&] {
            DemoWriter contender;
            gate.arrive_and_wait();
            if (contender.open(contested_path, 168U)) {
                ++acquired;
                static_cast<void>(contender.append(0U, first));
                static_cast<void>(contender.finish());
            }
        };
        std::thread contender_a{attempt};
        std::thread contender_b{attempt};
        contender_a.join(); contender_b.join();
        check(acquired == 1U, "concurrent writers exclusively create one recording without truncation");
        DemoReader reader;
        check(reader.open(path) && reader.protocol() == 168U, "header protocol roundtrips");
        auto packet = reader.next();
        check(packet && packet->time_us == 0U && packet->bytes == std::vector(first.begin(), first.end()), "first packet roundtrips");
        packet = reader.next();
        check(packet && packet->time_us == 50'000U && packet->bytes == std::vector(second.begin(), second.end()), "timed packet roundtrips");
        check(!reader.next() && reader.finished() && reader.error().empty(), "explicit end marker ends cleanly");

        const auto broken = directory / "truncated.bsdem";
        std::filesystem::copy_file(path, broken);
        std::filesystem::resize_file(broken, std::filesystem::file_size(broken) - 1U);
        check(reader.open(broken), "truncated record header can open");
        while (reader.next()) {}
        check(!reader.error().empty() && !reader.finished(), "truncation cannot become successful EOF");

        const auto old = directory / "zerospades.dem";
        legacy(old, 1.5F);
        check(reader.open(old) && reader.protocol() == 3U, "legacy format is recognized");
        packet = reader.next();
        check(packet && packet->time_us == 1'500'000U && packet->bytes.front() == std::byte{18}, "legacy timestamp and packet parse");
        check(!reader.next() && reader.finished(), "legacy EOF is accepted");
        reader = DemoReader{};
        legacy(old, std::numeric_limits<float>::quiet_NaN());
        check(reader.open(old) && !reader.next() && !reader.error().empty(), "NaN timestamp rejected");
        reader = DemoReader{};
        legacy(old, -1.0F);
        check(reader.open(old) && !reader.next() && !reader.error().empty(), "negative timestamp rejected");
        reader = DemoReader{};
        legacy(old, 1.0F, 65535U);
        check(reader.open(old) && !reader.next() && !reader.error().empty(), "truncated large payload rejected before allocation");
        reader = DemoReader{};

        DemoWriter invalid;
        check(!invalid.open(directory / "bad.bsdem", 169U), "unsupported protocols rejected");
        check(invalid.open(directory / "backwards.bsdem", 3U), "new writer opens after rejected protocol");
        check(invalid.append(100U, first) && !invalid.append(99U, second), "backwards timestamps rejected");
        static_cast<void>(invalid.finish());
        DemoWriter duration;
        const auto duration_path = directory / "duration.bsdem";
        check(duration.open(duration_path, 168U) && duration.append(0U, first) &&
              !duration.append(demo_maximum_time_us + 1U, second), "duration budget ends recording");
        check(reader.open(duration_path) && reader.next().has_value() && !reader.next() && reader.finished(),
              "duration limit preserves a complete playable prefix");
        reader = DemoReader{};
        check(!reader.open(directory), "directory cannot be a demo");
        std::filesystem::remove_all(directory);
        std::cout << "Demo stream tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
