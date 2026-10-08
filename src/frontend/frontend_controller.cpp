#include "battlespades/frontend/frontend_controller.hpp"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace battlespades::frontend {
namespace {

constexpr ui::ScreenId select_menu_screen{1U};
constexpr ui::ScreenId settings_screen{2U};
constexpr ui::ScreenId resolution_confirmation_screen{3U};

[[nodiscard]] std::optional<FrontendRoute> route_for(ui::ScreenId screen) noexcept {
    if (screen == select_menu_screen) {
        return FrontendRoute::select_menu;
    }
    if (screen == settings_screen) {
        return FrontendRoute::settings;
    }
    if (screen == resolution_confirmation_screen) {
        return FrontendRoute::resolution_confirmation;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ui::Point>
to_retail_subpixels(std::optional<ui::Point> point) noexcept {
    if (!point.has_value()) {
        return std::nullopt;
    }
    constexpr auto scale = MainMenuModel::subpixels_per_pixel;
    constexpr auto lower = std::numeric_limits<std::int32_t>::min() / scale;
    constexpr auto upper = std::numeric_limits<std::int32_t>::max() / scale;
    if (point->x < lower || point->x > upper || point->y < lower || point->y > upper) {
        return std::nullopt;
    }
    return ui::Point{point->x * scale, point->y * scale};
}

[[nodiscard]] RuntimeAudioEffect menu_sound(SettingsMenuSound sound) noexcept {
    switch (sound) {
    case SettingsMenuSound::confirm:
        return {RuntimeAudioEffectKind::menu_confirm, 0.0};
    case SettingsMenuSound::back:
        return {RuntimeAudioEffectKind::menu_back, 0.0};
    case SettingsMenuSound::scroll:
        return {RuntimeAudioEffectKind::menu_scroll, 0.0};
    }
    return {RuntimeAudioEffectKind::menu_confirm, 0.0};
}

[[nodiscard]] std::string binding_rejection_message(
    settings::BindingAssignmentStatus status) {
    switch (status) {
    case settings::BindingAssignmentStatus::assigned:
        return {};
    case settings::BindingAssignmentStatus::invalid_action:
        return "The selected control action cannot be rebound.";
    case settings::BindingAssignmentStatus::invalid_binding:
        return "The selected input cannot be used as a binding.";
    case settings::BindingAssignmentStatus::reserved_inventory_key:
        return "Number keys are reserved for the fixed inventory slots.";
    case settings::BindingAssignmentStatus::conflicts_with_existing_action:
        return "That input is already assigned to another action.";
    }
    return "The input binding was rejected.";
}

} // namespace

struct FrontendController::Impl final {
    explicit Impl(FrontendControllerConfig value)
        : config{std::move(value)},
          store{config.settings_path},
          session{settings::retail_default_settings()},
          settings_menu{session, config.settings_environment},
          routes{3U} {}

    FrontendControllerConfig config{};
    settings::TomlSettingsStore store;
    settings::SettingsSession session;
    SettingsMenuModel settings_menu;
    MainMenuModel main_menu{};
    ResolutionConfirmationModel resolution{};
    ui::ScreenStack routes;
    FrontendShellModel shell{};
    settings::ClientSettings persisted{settings::retail_default_settings()};
    settings::ClientSettings applied_preview{settings::retail_default_settings()};
    std::optional<SettingsCommitCommand> pending_resolution_commit{};
    std::vector<RuntimeSettingsEffect> runtime_effects{};
    std::vector<MainMenuAction> main_actions{};
    std::string last_error{};
    bool started{};
    bool suppress_committed_close{};

    void emit(RuntimeSettingsEffectPayload effect) {
        runtime_effects.push_back(RuntimeSettingsEffect{std::move(effect)});
    }

    void emit_audio(RuntimeAudioEffectKind kind, double value = 0.0) {
        emit(RuntimeAudioEffect{kind, value});
    }

    void emit_live_main_differences(const settings::ClientSettings& before,
                                    const settings::ClientSettings& after) {
        if (before.main.master_volume != after.main.master_volume) {
            emit_audio(RuntimeAudioEffectKind::set_master_volume,
                       after.main.master_volume);
        }
        if (before.main.music_volume != after.main.music_volume) {
            emit_audio(RuntimeAudioEffectKind::set_music_volume, after.main.music_volume);
        }
        if (before.main.fullscreen != after.main.fullscreen) {
            emit(RuntimeDisplayEffect{
                RuntimeDisplayEffectKind::set_fullscreen,
                after.main.fullscreen,
                after.graphics.resolution,
            });
        }
    }

    void emit_live_vsync_difference(const settings::ClientSettings& before,
                                    const settings::ClientSettings& after) {
        if (before.graphics.vsync != after.graphics.vsync) {
            emit(RuntimeDisplayEffect{
                RuntimeDisplayEffectKind::set_vsync,
                after.graphics.vsync,
                after.graphics.resolution,
            });
        }
    }

    void preview(const SettingsPreviewEffect& effect) {
        const auto before = applied_preview;
        switch (effect.source) {
        case SettingsRowId::master_volume:
            // Main-tab Defaults emits one preview at its first row. Comparing
            // all live Main fields preserves that aggregate retail behavior.
            emit_live_main_differences(before, effect.draft);
            applied_preview.main.master_volume = effect.draft.main.master_volume;
            applied_preview.main.music_volume = effect.draft.main.music_volume;
            applied_preview.main.fullscreen = effect.draft.main.fullscreen;
            break;
        case SettingsRowId::music_volume:
            if (before.main.music_volume != effect.draft.main.music_volume) {
                emit_audio(RuntimeAudioEffectKind::set_music_volume,
                           effect.draft.main.music_volume);
            }
            applied_preview.main.music_volume = effect.draft.main.music_volume;
            break;
        case SettingsRowId::fullscreen:
            if (before.main.fullscreen != effect.draft.main.fullscreen) {
                emit(RuntimeDisplayEffect{
                    RuntimeDisplayEffectKind::set_fullscreen,
                    effect.draft.main.fullscreen,
                    effect.draft.graphics.resolution,
                });
            }
            applied_preview.main.fullscreen = effect.draft.main.fullscreen;
            break;
        case SettingsRowId::vsync:
            emit_live_vsync_difference(before, effect.draft);
            applied_preview.graphics.vsync = effect.draft.graphics.vsync;
            break;
        default:
            // Resolution and quality changes are staged until Done. Controls
            // are consumed directly from the committed settings snapshot.
            break;
        }
    }

    void apply_committed_runtime(const settings::ClientSettings& value,
                                 bool apply_resolution,
                                 bool restart_required) {
        const auto previous = applied_preview;
        emit_live_main_differences(applied_preview, value);
        emit_live_vsync_difference(applied_preview, value);
        if (apply_resolution && applied_preview.graphics.resolution != value.graphics.resolution) {
            emit(RuntimeDisplayEffect{
                RuntimeDisplayEffectKind::set_resolution,
                false,
                value.graphics.resolution,
            });
        }
        if (applied_preview.graphics != value.graphics) {
            emit(RuntimePresentationEffect{value.graphics});
        }
        if (previous.main.invert_mouse != value.main.invert_mouse ||
            previous.controls != value.controls) {
            emit(RuntimeInputEffect{value.main.invert_mouse, value.controls});
        }
        if (restart_required) {
            emit(RuntimeSettingsNoticeEffect{
                RuntimeSettingsNoticeKind::restart_required,
                "Some graphics changes take effect after restarting the client.",
                std::nullopt,
                std::nullopt,
            });
        }
        applied_preview = value;
    }

    void restore_runtime(const settings::ClientSettings& value) {
        const auto previous = applied_preview;
        emit_live_main_differences(applied_preview, value);
        emit_live_vsync_difference(applied_preview, value);
        if (applied_preview.graphics.resolution != value.graphics.resolution) {
            emit(RuntimeDisplayEffect{
                RuntimeDisplayEffectKind::set_resolution,
                false,
                value.graphics.resolution,
            });
        }
        if (applied_preview.graphics != value.graphics) {
            emit(RuntimePresentationEffect{value.graphics});
        }
        if (previous.main.invert_mouse != value.main.invert_mouse ||
            previous.controls != value.controls) {
            emit(RuntimeInputEffect{value.main.invert_mouse, value.controls});
        }
        applied_preview = value;
    }

    void navigate_to_settings() {
        if (!routes.apply(ui::ScreenCommand::push(settings_screen))) {
            return;
        }
        static_cast<void>(shell.navigate(settings_screen, NavigationDirection::forward));
        if (settings_menu.active_tab() != settings::SettingsTab::main) {
            settings_menu.set_active_tab(settings::SettingsTab::main);
            // Opening Settings is already represented by the Select-menu
            // confirmation. Do not duplicate a tab-switch sound here.
            static_cast<void>(settings_menu.take_effects());
        }
    }

    void navigate_to_select() {
        static_cast<void>(routes.apply(ui::ScreenCommand::reset(select_menu_screen)));
        static_cast<void>(shell.navigate(select_menu_screen, NavigationDirection::back));
        main_menu.pointer_move(std::nullopt);
    }

    void return_to_settings_graphics() {
        static_cast<void>(routes.apply(ui::ScreenCommand::pop()));
        static_cast<void>(shell.navigate(settings_screen, NavigationDirection::back));
        if (settings_menu.active_tab() != settings::SettingsTab::graphics) {
            settings_menu.set_active_tab(settings::SettingsTab::graphics);
            static_cast<void>(settings_menu.take_effects());
        }
    }

    void dispatch_main_action(MainMenuAction action) {
        emit_audio(RuntimeAudioEffectKind::menu_confirm);
        if (action == MainMenuAction::settings) {
            navigate_to_settings();
            return;
        }
        main_actions.push_back(action);
    }

    void report_persistence_failure(std::string error) {
        last_error = std::move(error);
        emit(RuntimeSettingsNoticeEffect{
            RuntimeSettingsNoticeKind::persistence_failed,
            last_error,
            std::nullopt,
            std::nullopt,
        });
    }

    void process_commit(const SettingsCommitCommand& command) {
        suppress_committed_close = false;
        if (!command.changed) {
            // Saving an unchanged file is unnecessary, but live previews still
            // become the committed runtime state when Done is pressed.
            applied_preview = command.settings;
            return;
        }

        if (command.resolution_changed) {
            pending_resolution_commit = command;
            suppress_committed_close = true;
            // The temporary mode must be visible immediately, but user-facing
            // restart guidance is deferred until Keep also persists the edit.
            apply_committed_runtime(command.settings, true, false);
            resolution.restart();
            if (routes.apply(ui::ScreenCommand::push(resolution_confirmation_screen))) {
                static_cast<void>(shell.navigate(resolution_confirmation_screen,
                                                 NavigationDirection::forward));
            }
            return;
        }

        const auto saved = store.save(command.settings);
        if (!saved) {
            suppress_committed_close = true;
            report_persistence_failure(saved.error);
            restore_runtime(persisted);
            session = settings::SettingsSession{persisted};
            return;
        }

        last_error.clear();
        persisted = command.settings;
        apply_committed_runtime(command.settings, false, command.restart_required);
    }

    void process_settings_effect(SettingsMenuEffect& effect) {
        std::visit(
            [this](auto& value) {
                using T = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::is_same_v<T, SettingsSoundEffect>) {
                    emit(menu_sound(value.sound));
                } else if constexpr (std::is_same_v<T, SettingsPreviewEffect>) {
                    preview(value);
                } else if constexpr (std::is_same_v<T, SettingsDefaultsCommand>) {
                    // The model emits one aggregate preview after Defaults.
                    // Language now precedes the recovered Main rows, so route
                    // the aggregate through master volume explicitly. The
                    // first Graphics row is Resolution (staged), so VSync's
                    // recovered live behavior is also restored explicitly.
                    if (value.tab == settings::SettingsTab::main) {
                        preview(SettingsPreviewEffect{SettingsRowId::master_volume, value.draft});
                    } else if (value.tab == settings::SettingsTab::graphics) {
                        preview(SettingsPreviewEffect{SettingsRowId::vsync, value.draft});
                    }
                } else if constexpr (std::is_same_v<T, SettingsCommitCommand>) {
                    process_commit(value);
                } else if constexpr (std::is_same_v<T, SettingsRestoreCommand>) {
                    restore_runtime(value.settings);
                } else if constexpr (std::is_same_v<T, SettingsFavoriteServerCommand>) {
                    emit(RuntimeFavoriteServerEffect{value.favorite});
                } else if constexpr (std::is_same_v<T, SettingsCloseCommand>) {
                    if (!value.committed || !suppress_committed_close) {
                        navigate_to_select();
                    }
                } else if constexpr (std::is_same_v<T, SettingsBindingRejectedEffect>) {
                    emit(RuntimeSettingsNoticeEffect{
                        RuntimeSettingsNoticeKind::binding_rejected,
                        binding_rejection_message(value.status),
                        value.action,
                        value.conflicting_action,
                    });
                }
            },
            effect);
    }

    void drain_settings_effects() {
        auto effects = settings_menu.take_effects();
        for (auto& effect : effects) {
            process_settings_effect(effect);
        }
    }

    void resolve_resolution(ResolutionConfirmationAction action) {
        if (!pending_resolution_commit.has_value()) {
            return;
        }

        if (action == ResolutionConfirmationAction::keep) {
            emit_audio(RuntimeAudioEffectKind::menu_confirm);
            const auto command = *pending_resolution_commit;
            const auto saved = store.save(command.settings);
            if (saved) {
                last_error.clear();
                persisted = command.settings;
                if (command.restart_required) {
                    emit(RuntimeSettingsNoticeEffect{
                        RuntimeSettingsNoticeKind::restart_required,
                        "Some graphics changes take effect after restarting the client.",
                        std::nullopt,
                        std::nullopt,
                    });
                }
                pending_resolution_commit.reset();
                suppress_committed_close = false;
                navigate_to_select();
                return;
            }

            report_persistence_failure(saved.error);
        } else {
            emit_audio(RuntimeAudioEffectKind::menu_back);
        }

        restore_runtime(persisted);
        session = settings::SettingsSession{persisted};
        pending_resolution_commit.reset();
        suppress_committed_close = false;
        return_to_settings_graphics();
    }
};

FrontendController::FrontendController(FrontendControllerConfig config)
    : impl_{std::make_unique<Impl>(std::move(config))} {}

FrontendController::~FrontendController() = default;
FrontendController::FrontendController(FrontendController&&) noexcept = default;
FrontendController& FrontendController::operator=(FrontendController&&) noexcept = default;

bool FrontendController::start() {
    if (impl_->started || impl_->config.settings_path.empty()) {
        if (impl_->config.settings_path.empty()) {
            impl_->last_error = "settings path is empty";
        }
        return false;
    }

    const auto loaded = impl_->store.load();
    auto initial = loaded ? loaded.settings : settings::retail_default_settings();
    initial = settings::normalize_settings(initial);
    impl_->session = settings::SettingsSession{initial};
    impl_->persisted = initial;
    impl_->applied_preview = initial;
    if (!loaded) {
        impl_->last_error = loaded.error;
    }

    if (!impl_->routes.apply(ui::ScreenCommand::reset(select_menu_screen)) ||
        !impl_->shell.start(select_menu_screen)) {
        impl_->last_error = "failed to initialize the frontend route stack";
        return false;
    }
    impl_->started = true;
    return true;
}

void FrontendController::tick(std::chrono::nanoseconds elapsed) {
    if (!impl_->started) {
        return;
    }
    impl_->shell.tick();
    if (route() == FrontendRoute::resolution_confirmation) {
        if (const auto action = impl_->resolution.tick(elapsed); action.has_value()) {
            impl_->resolve_resolution(*action);
        }
    }
}

bool FrontendController::started() const noexcept {
    return impl_->started;
}

std::optional<FrontendRoute> FrontendController::route() const noexcept {
    const auto top = impl_->routes.top();
    return top.has_value() ? route_for(*top) : std::nullopt;
}

const ui::ScreenStack& FrontendController::routes() const noexcept {
    return impl_->routes;
}

const FrontendShellModel& FrontendController::shell() const noexcept {
    return impl_->shell;
}

const MainMenuModel& FrontendController::main_menu() const noexcept {
    return impl_->main_menu;
}

const SettingsMenuModel& FrontendController::settings_menu() const noexcept {
    return impl_->settings_menu;
}

const ResolutionConfirmationModel& FrontendController::resolution_confirmation() const noexcept {
    return impl_->resolution;
}

const settings::SettingsSession& FrontendController::settings_session() const noexcept {
    return impl_->session;
}

void FrontendController::pointer_move(std::optional<ui::Point> retail_point) {
    if (!impl_->started || !impl_->shell.accepts_input()) {
        return;
    }
    switch (route().value_or(FrontendRoute::select_menu)) {
    case FrontendRoute::select_menu:
        impl_->main_menu.pointer_move(to_retail_subpixels(retail_point));
        break;
    case FrontendRoute::settings:
        impl_->settings_menu.pointer_move(retail_point);
        break;
    case FrontendRoute::resolution_confirmation:
        impl_->resolution.pointer_move(to_retail_subpixels(retail_point));
        break;
    }
}

void FrontendController::pointer_press(std::optional<ui::Point> retail_point) {
    if (!impl_->started || !impl_->shell.accepts_input()) {
        return;
    }
    switch (route().value_or(FrontendRoute::select_menu)) {
    case FrontendRoute::select_menu:
        impl_->main_menu.pointer_press(to_retail_subpixels(retail_point));
        break;
    case FrontendRoute::settings:
        if (retail_point.has_value()) {
            impl_->settings_menu.pointer_press(*retail_point);
        } else {
            impl_->settings_menu.cancel_pointer_capture();
        }
        break;
    case FrontendRoute::resolution_confirmation:
        impl_->resolution.pointer_press(to_retail_subpixels(retail_point));
        break;
    }
}

void FrontendController::pointer_drag(std::optional<ui::Point> retail_point) {
    if (!impl_->started || !impl_->shell.accepts_input()) {
        return;
    }
    if (route() == FrontendRoute::settings && retail_point.has_value()) {
        impl_->settings_menu.pointer_drag(*retail_point);
        impl_->drain_settings_effects();
    } else {
        pointer_move(retail_point);
    }
}

void FrontendController::pointer_release(std::optional<ui::Point> retail_point) {
    if (!impl_->started || !impl_->shell.accepts_input()) {
        return;
    }
    switch (route().value_or(FrontendRoute::select_menu)) {
    case FrontendRoute::select_menu:
        if (const auto action =
                impl_->main_menu.pointer_release(to_retail_subpixels(retail_point));
            action.has_value()) {
            impl_->dispatch_main_action(*action);
        }
        break;
    case FrontendRoute::settings:
        if (retail_point.has_value()) {
            impl_->settings_menu.pointer_release(*retail_point);
            impl_->drain_settings_effects();
        } else {
            impl_->settings_menu.cancel_pointer_capture();
        }
        break;
    case FrontendRoute::resolution_confirmation:
        if (const auto action =
                impl_->resolution.pointer_release(to_retail_subpixels(retail_point));
            action.has_value()) {
            impl_->resolve_resolution(*action);
        }
        break;
    }
}

bool FrontendController::mouse_wheel(ui::Point retail_point, std::int32_t vertical_steps) {
    if (!impl_->started || !impl_->shell.accepts_input() ||
        route() != FrontendRoute::settings) {
        return false;
    }
    const auto handled = impl_->settings_menu.mouse_wheel(retail_point, vertical_steps);
    impl_->drain_settings_effects();
    return handled;
}

bool FrontendController::handle(ui::InputEvent event) {
    if (!impl_->started || !impl_->shell.accepts_input()) {
        return false;
    }
    switch (route().value_or(FrontendRoute::select_menu)) {
    case FrontendRoute::select_menu:
        if (const auto action = impl_->main_menu.handle(event); action.has_value()) {
            impl_->dispatch_main_action(*action);
            return true;
        }
        return false;
    case FrontendRoute::settings: {
        const auto handled = impl_->settings_menu.handle(event);
        impl_->drain_settings_effects();
        return handled;
    }
    case FrontendRoute::resolution_confirmation:
        // Retail deliberately ignored keyboard/controller input on this
        // safety screen; only Keep, Revert, and timeout are accepted.
        return false;
    }
    return false;
}

settings::BindingAssignmentResult
FrontendController::capture_scancode(std::uint32_t scancode) {
    if (!impl_->started || !impl_->shell.accepts_input() ||
        route() != FrontendRoute::settings) {
        return {settings::BindingAssignmentStatus::invalid_action, std::nullopt};
    }
    const auto result = impl_->settings_menu.capture_scancode(scancode);
    impl_->drain_settings_effects();
    return result;
}

settings::BindingAssignmentResult
FrontendController::capture_mouse_button(std::uint32_t button) {
    if (!impl_->started || !impl_->shell.accepts_input() ||
        route() != FrontendRoute::settings) {
        return {settings::BindingAssignmentStatus::invalid_action, std::nullopt};
    }
    const auto result = impl_->settings_menu.capture_mouse_button(button);
    impl_->drain_settings_effects();
    return result;
}

void FrontendController::cancel_pointer_capture() noexcept {
    impl_->main_menu.pointer_press(std::nullopt);
    static_cast<void>(impl_->main_menu.pointer_release(std::nullopt));
    impl_->settings_menu.cancel_pointer_capture();
    impl_->resolution.cancel_pointer_capture();
}

std::span<const RuntimeSettingsEffect> FrontendController::effects() const noexcept {
    return impl_->runtime_effects;
}

std::vector<RuntimeSettingsEffect> FrontendController::take_effects() noexcept {
    auto result = std::move(impl_->runtime_effects);
    impl_->runtime_effects.clear();
    return result;
}

std::span<const MainMenuAction> FrontendController::unhandled_main_actions() const noexcept {
    return impl_->main_actions;
}

std::vector<MainMenuAction> FrontendController::take_unhandled_main_actions() noexcept {
    auto result = std::move(impl_->main_actions);
    impl_->main_actions.clear();
    return result;
}

std::string_view FrontendController::last_error() const noexcept {
    return impl_->last_error;
}

} // namespace battlespades::frontend
