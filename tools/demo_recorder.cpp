#include "battlespades/network/demo_recorder.hpp"

#include <csignal>
#include <iostream>
#include <vector>

namespace {
volatile std::sig_atomic_t interrupted{};
void stop_recording(int) { interrupted = 1; }
}

int main(int argc, char** argv) {
    using namespace battlespades::network;
    std::vector<std::string_view> arguments;
    for (int index{1}; index < argc; ++index) arguments.emplace_back(argv[index]);
    std::string error;
    const auto options = parse_demo_recorder_options(arguments, error);
    if (!options) { std::cerr << error << '\n' << demo_recorder_usage(); return 2; }
    if (options->help) { std::cout << demo_recorder_usage(); return 0; }
    std::signal(SIGINT, stop_recording);
    std::signal(SIGTERM, stop_recording);
    const auto result = run_demo_recorder(*options, [] { return interrupted != 0; },
        [](std::string_view message) { std::cout << message << std::endl; });
    if (!result.error.empty()) { std::cerr << result.error << '\n'; return 1; }
    std::cout << "Demo finalized (" << result.received_packets << " incoming packets).\n";
    return 0;
}
