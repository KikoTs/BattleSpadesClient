#include "battlespades/audio/audio_port.hpp"
#include "battlespades/core/application.hpp"
#include "battlespades/core/command_line.hpp"
#include "battlespades/headless/headless_module.hpp"
#include "battlespades/network/transport_port.hpp"
#include "battlespades/platform/window_port.hpp"
#include "battlespades/render/renderer_port.hpp"

#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using battlespades::core::Application;
using battlespades::core::RunResult;
using battlespades::core::RuntimeConfig;
using battlespades::core::RuntimeModule;
using battlespades::core::TickContext;
using battlespades::core::TickDecision;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

class RecordingModule final : public RuntimeModule {
public:
    RecordingModule(std::string name,
                    std::vector<std::string>& events,
                    bool start_succeeds = true,
                    std::optional<std::uint64_t> stop_at_tick = std::nullopt)
        : name_{std::move(name)}, events_{events}, start_succeeds_{start_succeeds},
          stop_at_tick_{stop_at_tick} {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }

    [[nodiscard]] bool start() override {
        events_.push_back("start:" + name_);
        return start_succeeds_;
    }

    [[nodiscard]] TickDecision tick(const TickContext& context) override {
        events_.push_back("tick:" + name_ + ":" + std::to_string(context.index));
        if (stop_at_tick_.has_value() && context.index == *stop_at_tick_) {
            return TickDecision::stop;
        }
        return TickDecision::continue_running;
    }

    void stop() noexcept override {
        events_.push_back("stop:" + name_);
    }

private:
    std::string name_;
    std::vector<std::string>& events_;
    bool start_succeeds_;
    std::optional<std::uint64_t> stop_at_tick_;
};

void command_line_defaults_are_safe() {
    const std::vector<std::string_view> arguments;
    const auto parsed = battlespades::core::parse_command_line(arguments);

    expect(static_cast<bool>(parsed), "default command line should parse");
    expect(parsed.options->runtime.tick_limit == 1U, "default must run one tick");
    expect(!parsed.options->headless, "graphical mode must be the default launch intent");
    expect(!parsed.options->runtime_lifetime_explicit,
           "default launch must not claim an explicit lifetime");
    expect(!parsed.options->runtime.pace_to_wall_clock, "smoke default must not sleep");
    expect(parsed.options->runtime.fixed_delta.count() == 16'666'666,
           "default clock must match explicit 60 Hz");
}

void command_line_parses_headless_runtime_options() {
    const std::vector<std::string_view> arguments{
        "--headless",
        "--ticks",
        "12",
        "--tick-rate",
        "60",
        "--pace",
    };
    const auto parsed = battlespades::core::parse_command_line(arguments);

    expect(static_cast<bool>(parsed), "valid headless options should parse");
    expect(parsed.options->headless, "--headless must select the headless adapter");
    expect(parsed.options->runtime.tick_limit == 12U, "tick limit should be 12");
    expect(parsed.options->runtime_lifetime_explicit,
           "--ticks must mark the runtime lifetime explicit");
    expect(parsed.options->runtime.pace_to_wall_clock, "pacing should be enabled");
    expect(parsed.options->runtime.fixed_delta.count() == 16'666'666,
           "60 Hz period should use integer nanoseconds");
}

void command_line_rejects_malformed_values() {
    const std::vector<std::string_view> zero_rate{"--tick-rate", "0"};
    const std::vector<std::string_view> unknown{"--definitely-not-real"};

    expect(!static_cast<bool>(battlespades::core::parse_command_line(zero_rate)),
           "zero tick rate must fail closed");
    expect(!static_cast<bool>(battlespades::core::parse_command_line(unknown)),
           "unknown options must fail closed");
}

void command_line_parses_tutorial_tool() {
    const std::vector<std::string_view> arguments{
        "--tutorial-tool", "55", "--tutorial-aim"};
    const auto parsed = battlespades::core::parse_command_line(arguments);

#if AOS_ENABLE_DEVELOPER_TOOLS
    expect(static_cast<bool>(parsed), "tutorial tool option must parse");
    expect(parsed.options->tutorial_debug_tool == 55U,
           "tutorial tool option must retain the retail tool id");
    expect(parsed.options->tutorial_debug_aim,
           "tutorial aim option must retain the requested state");

    const std::vector<std::string_view> invalid{"--tutorial-tool", "65"};
    expect(!static_cast<bool>(battlespades::core::parse_command_line(invalid)),
           "tool ids outside Protocol 168 must fail closed");
#else
    expect(!static_cast<bool>(parsed),
           "player builds must not expose Tutorial debug loadout options");
#endif
}

void command_line_parses_live_connect_shortcut() {
    const std::vector<std::string_view> arguments{
        "--connect", "127.0.0.1:32887"};
    const auto parsed = battlespades::core::parse_command_line(arguments);
    expect(static_cast<bool>(parsed) &&
               parsed.options->startup_endpoint == "127.0.0.1:32887",
           "live connect shortcut must preserve the complete endpoint");
    const std::vector<std::string_view> missing{"--connect"};
    expect(!static_cast<bool>(battlespades::core::parse_command_line(missing)),
           "live connect shortcut without an endpoint must fail closed");
}

void command_line_parses_offline_vfx_oracle() {
    const std::vector<std::string_view> arguments{
        "--debug-vfx", "rocket", "--debug-vfx-age", "0.32"};
    const auto parsed = battlespades::core::parse_command_line(arguments);
#if AOS_ENABLE_DEVELOPER_TOOLS
    expect(static_cast<bool>(parsed) && parsed.options->debug_vfx == "rocket",
           "offline VFX oracle must retain the validated emitter name");
    expect(parsed.options->debug_vfx_age == 0.32,
           "offline VFX oracle must retain the exact capture age");

    const std::vector<std::string_view> invalid{"--debug-vfx", "invented"};
    expect(!static_cast<bool>(battlespades::core::parse_command_line(invalid)),
           "offline VFX oracle must fail closed on substitute effects");

    const std::vector<std::string_view> online{
        "--debug-vfx", "corpse", "--connect", "127.0.0.1:32887"};
    expect(!static_cast<bool>(battlespades::core::parse_command_line(online)),
           "offline VFX oracle must never connect or send gameplay packets");

    const std::vector<std::string_view> orphan_age{"--debug-vfx-age", "0.2"};
    expect(!static_cast<bool>(battlespades::core::parse_command_line(orphan_age)),
           "an exact VFX age without an emitter must fail closed");
#else
    expect(!static_cast<bool>(parsed),
           "player builds must not expose the offline VFX oracle");
#endif
}

void command_line_parses_offline_ui_oracle() {
    const std::vector<std::string_view> arguments{"--debug-ui", "scoreboard"};
    const auto parsed = battlespades::core::parse_command_line(arguments);
#if AOS_ENABLE_DEVELOPER_TOOLS
    expect(static_cast<bool>(parsed) && parsed.options->debug_ui == "scoreboard",
           "offline UI oracle must retain the validated screen name");

    const auto chat = battlespades::core::parse_command_line(
        std::vector<std::string_view>{"--debug-ui", "chat"});
    const auto vote = battlespades::core::parse_command_line(
        std::vector<std::string_view>{"--debug-ui", "vote"});
    expect(static_cast<bool>(chat) && chat.options->debug_ui == "chat" &&
               static_cast<bool>(vote) && vote.options->debug_ui == "vote",
           "chat and vote must be available as deterministic UI capture oracles");

    const std::vector<std::string_view> invalid{"--debug-ui", "invented"};
    expect(!static_cast<bool>(battlespades::core::parse_command_line(invalid)),
           "offline UI oracle must reject unknown screens");
    const std::vector<std::string_view> online{
        "--debug-ui", "leaderboard", "--connect", "127.0.0.1:32887"};
    expect(!static_cast<bool>(battlespades::core::parse_command_line(online)),
           "offline UI oracle must never connect to a server");
    const std::vector<std::string_view> mixed{
        "--debug-ui", "endgame", "--debug-vfx", "rocket"};
    expect(!static_cast<bool>(battlespades::core::parse_command_line(mixed)),
           "UI and VFX visual oracles must remain mutually exclusive");
#else
    expect(!static_cast<bool>(parsed),
           "player builds must not expose the offline UI oracle");
#endif
}

void command_line_parses_offline_layout_editor() {
    const auto parsed = battlespades::core::parse_command_line(
        std::vector<std::string_view>{"--debug-ui", "leaderboard", "--ui-editor"});
    const auto online = battlespades::core::parse_command_line(
        std::vector<std::string_view>{"--ui-editor", "--connect", "127.0.0.1:32887"});
#if AOS_ENABLE_DEVELOPER_TOOLS
    expect(static_cast<bool>(parsed) && parsed.options->ui_layout_editor,
           "layout editor must compose with an offline UI oracle");
    expect(!static_cast<bool>(online),
           "layout editor must never be available on a live connection");
#else
    expect(!static_cast<bool>(parsed) && !static_cast<bool>(online),
           "player builds must not expose the UI layout editor");
#endif
}

void application_ticks_and_stops_modules_in_reverse_order() {
    std::vector<std::string> events;
    RuntimeConfig config{};
    config.tick_limit = 2U;
    Application application{config};

    expect(application.add_module(std::make_unique<RecordingModule>("one", events)),
           "first module should register");
    expect(application.add_module(std::make_unique<RecordingModule>("two", events)),
           "second module should register");
    expect(application.run() == RunResult::success, "runtime should succeed");

    const std::vector<std::string> expected{
        "start:one",
        "start:two",
        "tick:one:0",
        "tick:two:0",
        "tick:one:1",
        "tick:two:1",
        "stop:two",
        "stop:one",
    };
    expect(events == expected, "module lifecycle order must be deterministic");
}

void startup_failure_unwinds_started_modules() {
    std::vector<std::string> events;
    Application application{RuntimeConfig{}};

    expect(application.add_module(std::make_unique<RecordingModule>("ready", events)),
           "ready module should register");
    expect(application.add_module(std::make_unique<RecordingModule>("broken", events, false)),
           "broken module should still register");
    expect(application.run() == RunResult::module_start_failed,
           "failed startup should be reported");

    const std::vector<std::string> expected{
        "start:ready",
        "start:broken",
        "stop:ready",
    };
    expect(events == expected, "only successfully started modules should unwind");
}

void headless_module_observes_fixed_ticks() {
    RuntimeConfig config{};
    config.tick_limit = 3U;
    Application application{config};
    auto module = std::make_unique<battlespades::headless::HeadlessModule>();
    auto* const observer = module.get();

    expect(application.add_module(std::move(module)), "headless module should register");
    expect(application.run() == RunResult::success, "headless runtime should succeed");
    expect(observer->ticks_observed() == 3U, "headless module should see three ticks");
    expect(!observer->is_running(), "headless module should be stopped after run");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"command_line_defaults_are_safe", command_line_defaults_are_safe},
        {"command_line_parses_headless_runtime_options",
         command_line_parses_headless_runtime_options},
        {"command_line_rejects_malformed_values", command_line_rejects_malformed_values},
        {"command_line_parses_tutorial_tool", command_line_parses_tutorial_tool},
        {"command_line_parses_live_connect_shortcut",
         command_line_parses_live_connect_shortcut},
        {"command_line_parses_offline_vfx_oracle",
         command_line_parses_offline_vfx_oracle},
        {"command_line_parses_offline_ui_oracle",
         command_line_parses_offline_ui_oracle},
        {"command_line_parses_offline_layout_editor",
         command_line_parses_offline_layout_editor},
        {"application_ticks_and_stops_modules_in_reverse_order",
         application_ticks_and_stops_modules_in_reverse_order},
        {"startup_failure_unwinds_started_modules", startup_failure_unwinds_started_modules},
        {"headless_module_observes_fixed_ticks", headless_module_observes_fixed_ticks},
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
