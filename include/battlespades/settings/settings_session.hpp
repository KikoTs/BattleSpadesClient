#pragma once

#include "battlespades/settings/client_settings.hpp"

namespace battlespades::settings {

/**
 * Transactional Settings-menu edit session.
 *
 * The committed snapshot is the last user-approved configuration. UI controls
 * edit the normalized draft; Cancel restores the committed snapshot, while
 * commit() atomically promotes the complete draft. Persistence and runtime
 * previews remain explicit application-service responsibilities.
 */
class SettingsSession final {
public:
    explicit SettingsSession(ClientSettings current = retail_default_settings()) noexcept;

    [[nodiscard]] const ClientSettings& committed() const noexcept;
    [[nodiscard]] const ClientSettings& draft() const noexcept;
    [[nodiscard]] bool dirty() const noexcept;

    void set_main(MainSettings settings) noexcept;
    void set_graphics(GraphicsSettings settings) noexcept;
    void set_controls(ControlsSettings settings) noexcept;

    /** Restores the active tab only, preserving pending edits on other tabs. */
    void reset_tab(SettingsTab tab) noexcept;

    /** Applies the retail duplicate and inventory-slot binding guards. */
    [[nodiscard]] BindingAssignmentResult assign_binding(ControlAction action,
                                                         InputBinding binding) noexcept;

    /** Promotes the complete normalized draft; returns whether it changed. */
    [[nodiscard]] bool commit() noexcept;

    /** Discards every uncommitted edit. */
    void cancel() noexcept;

private:
    ClientSettings committed_{};
    ClientSettings draft_{};
};

} // namespace battlespades::settings
