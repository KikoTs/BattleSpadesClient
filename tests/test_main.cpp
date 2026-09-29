#include "battlespades/audio/audio_port.hpp"
#include "battlespades/core/application.hpp"
#include "battlespades/core/command_line.hpp"
#include "battlespades/core/frame_pacing.hpp"
#include "battlespades/headless/headless_module.hpp"
#include "battlespades/network/transport_port.hpp"
#include "battlespades/platform/window_port.hpp"
#include "battlespades/render/renderer_port.hpp"

#include <algorithm>
#include <chrono>
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
                    std::optional<std::uint64_t> stop_at_tick = std::nullopt,
                    std::vector<TickContext>* contexts = nullptr)
        : name_{std::move(name)}, events_{events}, start_succeeds_{start_succeeds},
          stop_at_tick_{stop_at_tick}, contexts_{contexts} {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }

    [[nodiscard]] bool start() override {
        events_.push_back("start:" + name_);
        return start_succeeds_;
    }

    [[nodiscard]] TickDecision tick(const TickContext& context) override {
        events_.push_back("tick:" + name_ + ":" + std::to_string(context.index));
        if (contexts_ != nullptr) {
            contexts_->push_back(context);
        }
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
    std::vector<TickContext>* contexts_;
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
        "--tutorial-tool", "55", "--tutorial-aim", "--tutorial-cosmetic", "community-stg44-v2"};
    const auto parsed = battlespades::core::parse_command_line(arguments);

#if AOS_ENABLE_DEVELOPER_TOOLS
    expect(static_cast<bool>(parsed), "tutorial tool option must parse");
    expect(parsed.options->tutorial_debug_tool == 55U,
           "tutorial tool option must retain the retail tool id");
    expect(parsed.options->tutorial_debug_aim,
           "tutorial aim option must retain the requested state");
    expect(parsed.options->tutorial_debug_cosmetic == "community-stg44-v2",
           "local visual checks must retain the requested bundled cosmetic");
    const std::vector<std::string_view> invalid_cosmetic{"--tutorial-cosmetic", "../outside"};
    expect(!static_cast<bool>(battlespades::core::parse_command_line(invalid_cosmetic)),
           "cosmetic preview ids must not allow filesystem traversal");

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

    // Steam's own launch forms for a friend's Join Game / accepted invite.
    const std::vector<std::string_view> steam_join{"+connect", "steam:76561198000000001"};
    const auto joined = battlespades::core::parse_command_line(steam_join);
    expect(static_cast<bool>(joined) &&
               joined.options->startup_endpoint == "steam:76561198000000001",
           "+connect steam:<id> must reach the loader");
    const std::vector<std::string_view> bare{"steam:76561198000000001"};
    const auto bare_joined = battlespades::core::parse_command_line(bare);
    expect(static_cast<bool>(bare_joined) &&
               bare_joined.options->startup_endpoint == "steam:76561198000000001",
           "a bare steam:<id> appended by Steam must not close the game");
    const std::vector<std::string_view> lobby{"+connect_lobby", "109775241021923456"};
    const auto lobby_joined = battlespades::core::parse_command_line(lobby);
    expect(static_cast<bool>(lobby_joined) &&
               lobby_joined.options->startup_steam_lobby == 109775241021923456ULL,
           "+connect_lobby must carry the lobby id");
    const std::vector<std::string_view> bad_lobby{"+connect_lobby", "abc"};
    expect(!static_cast<bool>(battlespades::core::parse_command_line(bad_lobby)),
           "+connect_lobby with a malformed id must fail closed");
    const std::vector<std::string_view> bad_bare{"steam:notanid"};
    expect(!static_cast<bool>(battlespades::core::parse_command_line(bad_bare)),
           "a malformed steam token is still an unknown option");
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
    for (const auto fixture : {"loading/map", "loading/mode", "loading/scores", "loading/hosting", "loading/error", "create_match/notice",
                               "friends", "friends/requests", "friends/invites", "friends/lobby", "friends/offline", "friends/busy"}) {
        const auto fixture_options = battlespades::core::parse_command_line(
            std::vector<std::string_view>{"--debug-ui", fixture});
        expect(static_cast<bool>(fixture_options) && fixture_options.options->debug_ui == fixture,
               "loading, lobby notice and Friends oracles must be available for visual regression checks");
    }
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

void unpaced_application_keeps_every_fixed_step_and_presentation() {
    std::vector<std::string> events;
    std::vector<TickContext> contexts;
    RuntimeConfig config{};
    config.tick_limit = 12U;
    Application application{config};
    expect(application.add_module(std::make_unique<RecordingModule>(
               "observer", events, true, std::nullopt, &contexts)),
           "context observer should register");
    expect(application.run() == RunResult::success, "unpaced application must succeed");
    expect(contexts.size() == *config.tick_limit, "unpaced ticks must never be skipped");
    for (std::size_t index{}; index < contexts.size(); ++index) {
        expect(contexts[index].index == index && contexts[index].present &&
                   contexts[index].fixed_delta == config.fixed_delta &&
                   contexts[index].elapsed == config.fixed_delta *
                                                 static_cast<std::int64_t>(index),
               "headless/smoke runs must retain exact tick indices, time and presentation");
    }
}

void fixed_step_pacer_keeps_simulation_cadence_when_rendering_is_slow() {
    using namespace std::chrono_literals;
    using Pacer = battlespades::core::FixedStepPacer;
    constexpr auto period = 16'666'666ns;
    for (const auto render_cost : {5ms, 20ms, 33ms}) {
        Pacer::Clock::time_point now{};
        Pacer pacer{now, period};
        const auto finish = now + 10s;
        std::uint64_t ticks{};
        std::uint64_t presentations{};
        std::int64_t consecutive_skips{};
        while (now < finish && ticks < 1'000U) {
            const auto step = pacer.step(now);
            ++ticks; // Input, network and simulation execute on every step.
            now += 200us;
            if (step.present) {
                ++presentations;
                consecutive_skips = 0;
                now += render_cost;
            } else {
                ++consecutive_skips;
            }
            expect(consecutive_skips <= Pacer::maximum_catch_up_ticks,
                   "the UI must be presented at least once per four simulation steps");
            now = std::max(now, step.next_tick); // Fake sleep_until, no wall-clock sleeps.
        }
        expect(ticks >= 599U && ticks <= 602U,
               "a 20/33ms presentation must not reduce simulation to 50/30Hz");
        if (render_cost == 5ms) {
            expect(presentations == ticks, "on-time steps must all present");
        } else {
            expect(presentations < ticks,
                   "slow rendering must yield catch-up steps without redundant draws");
        }
    }
}

void fixed_step_pacer_bounds_loading_stall_debt() {
    using namespace std::chrono_literals;
    using Pacer = battlespades::core::FixedStepPacer;
    constexpr auto period = 16'666'666ns;
    Pacer::Clock::time_point now{};
    Pacer pacer{now, period};
    const auto first = pacer.step(now);
    expect(first.present && first.next_tick == now + period,
           "the first step must present immediately and schedule one period");

    now += 30s; // Loading, debugger pause or OS suspend.
    for (std::int64_t step_index{}; step_index < Pacer::maximum_catch_up_ticks; ++step_index) {
        const auto step = pacer.step(now);
        expect(!step.present, "bounded debt must recover with simulation-only ticks");
        expect(now - step.next_tick <= period * (Pacer::maximum_catch_up_ticks - 1),
               "a long pause must not retain seconds of simulation debt");
    }
    const auto recovered = pacer.step(now);
    expect(recovered.present && recovered.next_tick == now + period,
           "three catch-up steps must recover the clock after even a 30-second stall");
    now = recovered.next_tick;
    const auto normal = pacer.step(now);
    expect(normal.present && normal.next_tick == now + period,
           "normal cadence must resume immediately after the bounded recovery");
}

void fixed_step_pacer_still_presents_when_simulation_itself_overruns() {
    using namespace std::chrono_literals;
    using Pacer = battlespades::core::FixedStepPacer;
    constexpr auto period = 16'666'666ns;
    Pacer::Clock::time_point now{};
    Pacer pacer{now, period};
    std::int64_t consecutive_skips{};
    std::size_t presentations{};
    for (std::size_t index{}; index < 120U; ++index) {
        const auto step = pacer.step(now);
        if (step.present) {
            ++presentations;
            consecutive_skips = 0;
        } else {
            ++consecutive_skips;
        }
        expect(consecutive_skips <= Pacer::maximum_catch_up_ticks,
               "persistent overload must not starve presentation indefinitely");
        expect(now - step.next_tick <= period * Pacer::maximum_catch_up_ticks,
               "persistent overload must not accumulate unbounded future catch-up");
        now += 100ms;
    }
    expect(presentations >= 30U,
           "even CPU overload must leave a bounded path to visible input feedback");
}

void intermediate_frames_fit_between_fixed_ticks() {
    using namespace std::chrono_literals;
    using Clock = battlespades::core::FixedStepPacer::Clock;
    using battlespades::core::intermediate_frame_alpha;
    using battlespades::core::next_intermediate_frame;
    constexpr auto tick = 16'666'666ns;
    constexpr auto hz144 = 6'944'444ns;
    const Clock::time_point tick_time{};
    const auto next_tick = tick_time + tick;

    // 144 Hz: frames at +6.9 and +13.9 ms fit; +20.8 would cross the tick.
    auto previous = tick_time;
    std::size_t frames{};
    while (const auto slot = next_intermediate_frame(previous, previous, next_tick, hz144)) {
        const double alpha = intermediate_frame_alpha(tick_time, slot->at, tick);
        expect(alpha > 0.0 && alpha < 1.0, "intermediate alpha must stay inside one fixed step");
        expect(slot->at + hz144 / 4 < next_tick,
               "interpolation must leave slack before the next fixed tick");
        previous = slot->at;
        ++frames;
    }
    expect(frames == 2U, "144 Hz fits two render-only frames between 60 Hz ticks");

    expect(!next_intermediate_frame(tick_time, tick_time, next_tick, 0ns).has_value(),
           "a zero period (retail pacing / 60 Hz display) never adds frames");
    expect(!next_intermediate_frame(tick_time, tick_time, next_tick, tick).has_value(),
           "a 60 Hz period never adds frames between 60 Hz ticks");
    // A slow tick presentation pushes the next frame to 'now', never earlier.
    const auto late = next_intermediate_frame(tick_time, tick_time + 9ms, next_tick, hz144);
    expect(late.has_value() && late->at == tick_time + 9ms,
           "a late tick schedules the next intermediate frame immediately");
    expect(intermediate_frame_alpha(tick_time, tick_time - 1ms, tick) == 0.0 &&
               intermediate_frame_alpha(tick_time, tick_time + 40ms, tick) < 1.0,
           "alpha is clamped to [0, 1)");
}

class InterpolatingModule final : public RuntimeModule {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "interpolating"; }
    [[nodiscard]] bool start() override { return true; }
    [[nodiscard]] battlespades::core::TickDecision tick(const TickContext&) override {
        ++ticks;
        return battlespades::core::TickDecision::continue_running;
    }
    void stop() noexcept override {}
    [[nodiscard]] std::chrono::nanoseconds intermediate_frame_period() const noexcept override {
        return period;
    }
    [[nodiscard]] battlespades::core::TickDecision present_intermediate(double alpha) override {
        alphas.push_back(alpha);
        return battlespades::core::TickDecision::continue_running;
    }

    std::chrono::nanoseconds period{};
    std::size_t ticks{};
    std::vector<double> alphas;
};

void application_presents_intermediate_frames_only_when_requested() {
    using namespace std::chrono_literals;
    {
        RuntimeConfig config;
        config.tick_limit = 6U;
        config.pace_to_wall_clock = true;
        auto module = std::make_unique<InterpolatingModule>();
        auto* observed = module.get();
        observed->period = 4ms;
        Application application{config};
        expect(application.add_module(std::move(module)), "interpolating module must register");
        expect(application.run() == RunResult::success, "paced run must succeed");
        expect(observed->ticks == 6U, "render-only frames must not add or drop fixed ticks");
        expect(observed->alphas.size() <= 6U * 4U,
               "intermediate frames stay bounded by the requested period");
        for (const double alpha : observed->alphas) {
            expect(alpha >= 0.0 && alpha < 1.0, "intermediate alpha must be in [0, 1)");
        }
    }
    {
        RuntimeConfig config;
        config.tick_limit = 3U;
        auto module = std::make_unique<InterpolatingModule>();
        auto* observed = module.get();
        observed->period = 4ms;
        Application application{config};
        expect(application.add_module(std::move(module)), "interpolating module must register");
        expect(application.run() == RunResult::success, "unpaced run must succeed");
        expect(observed->alphas.empty(),
               "unpaced (headless/test) runs never render intermediate frames");
    }
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
        {"unpaced_application_keeps_every_fixed_step_and_presentation",
         unpaced_application_keeps_every_fixed_step_and_presentation},
        {"fixed_step_pacer_keeps_simulation_cadence_when_rendering_is_slow",
         fixed_step_pacer_keeps_simulation_cadence_when_rendering_is_slow},
        {"fixed_step_pacer_bounds_loading_stall_debt",
         fixed_step_pacer_bounds_loading_stall_debt},
        {"fixed_step_pacer_still_presents_when_simulation_itself_overruns",
         fixed_step_pacer_still_presents_when_simulation_itself_overruns},
        {"intermediate_frames_fit_between_fixed_ticks",
         intermediate_frames_fit_between_fixed_ticks},
        {"application_presents_intermediate_frames_only_when_requested",
         application_presents_intermediate_frames_only_when_requested},
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
