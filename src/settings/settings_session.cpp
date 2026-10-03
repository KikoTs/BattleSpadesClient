#include "battlespades/settings/settings_session.hpp"

#include <cstddef>

namespace battlespades::settings {
namespace {

[[nodiscard]] constexpr bool valid_action(ControlAction action) noexcept {
    return static_cast<std::size_t>(action) < control_action_count;
}

} // namespace

SettingsSession::SettingsSession(ClientSettings current) noexcept
    : committed_{normalize_settings(current)}, draft_{committed_} {}

const ClientSettings& SettingsSession::committed() const noexcept {
    return committed_;
}

const ClientSettings& SettingsSession::draft() const noexcept {
    return draft_;
}

bool SettingsSession::dirty() const noexcept {
    return draft_ != committed_;
}

void SettingsSession::set_main(MainSettings settings) noexcept {
    auto candidate = draft_;
    candidate.main = settings;
    draft_ = normalize_settings(candidate);
}

void SettingsSession::set_graphics(GraphicsSettings settings) noexcept {
    auto candidate = draft_;
    candidate.graphics = settings;
    draft_ = normalize_settings(candidate);
}

void SettingsSession::set_controls(ControlsSettings settings) noexcept {
    auto candidate = draft_;
    candidate.controls = settings;
    draft_ = normalize_settings(candidate);
}

void SettingsSession::reset_tab(SettingsTab tab) noexcept {
    const auto defaults = retail_default_settings();
    switch (tab) {
    case SettingsTab::main:
        // Retail config.MAIN_DEFAULT names only these keys (its fourth,
        // fullscreen, is the Graphics tab's window mode now). Language, audio
        // device, skins, weapon motion and ability hints are native additions
        // and survive the Main tab's Defaults button.
        draft_.main.master_volume = defaults.main.master_volume;
        draft_.main.music_volume = defaults.main.music_volume;
        draft_.main.invert_mouse = defaults.main.invert_mouse;
        break;
    case SettingsTab::graphics: {
        // settings.toml-only native preferences have no menu row, so the
        // retail Defaults button must not silently reset them.
        const bool interpolation = draft_.graphics.render_interpolation;
        const double hud_scale = draft_.graphics.hud_scale;
        draft_.graphics = defaults.graphics;
        draft_.graphics.render_interpolation = interpolation;
        draft_.graphics.hud_scale = hud_scale;
        break;
    }
    case SettingsTab::controls:
        draft_.controls = defaults.controls;
        break;
    }
}

BindingAssignmentResult SettingsSession::assign_binding(ControlAction action,
                                                         InputBinding binding) noexcept {
    if (!valid_action(action)) {
        return {BindingAssignmentStatus::invalid_action, std::nullopt};
    }
    if (!valid_binding(binding)) {
        return {BindingAssignmentStatus::invalid_binding, std::nullopt};
    }
    if (is_reserved_inventory_binding(binding)) {
        return {BindingAssignmentStatus::reserved_inventory_key, std::nullopt};
    }

    if (!binding.is_unbound()) {
        for (std::size_t index{}; index < control_action_count; ++index) {
            const auto existing_action = static_cast<ControlAction>(index);
            if (existing_action != action && draft_.controls.bindings[index] == binding) {
                return {BindingAssignmentStatus::conflicts_with_existing_action,
                        existing_action};
            }
        }
    }

    static_cast<void>(draft_.controls.set_binding(action, binding));
    return {};
}

bool SettingsSession::commit() noexcept {
    draft_ = normalize_settings(draft_);
    const auto changed = dirty();
    committed_ = draft_;
    return changed;
}

void SettingsSession::cancel() noexcept {
    draft_ = committed_;
}

} // namespace battlespades::settings
