#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::platform {

/** Identity returned by the player's owned retail Steam runtime. */
struct NativeSteamIdentity final {
    std::uint64_t steam_id{};
    std::string persona_name;
    std::string language;
};

/**
 * One retail Protocol-168 authentication key and its Steam cancellation token.
 *
 * `wire_bytes` are already the original wrapper's ASCII-hex
 * `SteamID || GetAuthSessionTicket` representation. They are opaque to the
 * client and must never be logged or persisted.
 */
struct NativeSteamTicket final {
    std::uint32_t handle{};
    std::vector<std::byte> wire_bytes;
};

/**
 * Where the retail runtime is in its life, as the identity screen shows it.
 *
 * `starting` covers the bridge launch and SteamAPI_Init, which can take
 * seconds while Steam itself is still starting. `retrying` means the last
 * attempt failed for a reason that can change (Steam not running yet, not
 * signed in) and another attempt is scheduled; `unavailable` means nothing
 * will change without the player acting (no imported runtime, no bridge).
 */
enum class NativeSteamState : std::uint8_t {
    unavailable,
    starting,
    ready,
    retrying,
};

/** A Steam "Join Game" or accepted invite the bridge was told about. */
struct NativeSteamJoinEvent final {
    /** The friend's rich presence connect value; empty for a lobby event. */
    std::string connect;
    std::uint64_t lobby_id{};
    std::uint64_t friend_id{};
};

struct NativeSteamClientConfig final {
    std::filesystem::path bridge_executable;
    std::filesystem::path steam_api_library;
    std::uint32_t app_id{224540U};
    /** Bound on one command's answer once the bridge is ready. */
    std::chrono::milliseconds response_timeout{2'000};
    /**
     * Bound on SteamAPI_Init plus the handshake.
     *
     * This used to be the 2 s response timeout, which a Steam still starting
     * (or its overlay injecting into the bridge) exceeded often enough that
     * the Steam sign-in appeared on one launch and not the next.
     */
    std::chrono::milliseconds startup_timeout{20'000};
    /** Delay before another attempt after a transient failure. */
    std::chrono::milliseconds retry_interval{10'000};
};

/**
 * Supervises the architecture-matched helper for the original Steamworks DLL.
 *
 * The Windows retail DLL is 32-bit while the renderer is 64-bit, so it cannot
 * be loaded into the game process. A hidden child owns the DLL and pumps Steam
 * callbacks; this façade exchanges bounded commands over inherited anonymous
 * pipes. No thread is created here: begin_start() and poll() never block, so
 * the presentation thread drives the handshake a frame at a time. All public
 * methods fail closed after a broken/malformed response and never fabricate
 * identity.
 */
class NativeSteamClient final {
public:
    explicit NativeSteamClient(NativeSteamClientConfig config);
    ~NativeSteamClient();

    NativeSteamClient(const NativeSteamClient&) = delete;
    NativeSteamClient& operator=(const NativeSteamClient&) = delete;
    NativeSteamClient(NativeSteamClient&&) noexcept;
    NativeSteamClient& operator=(NativeSteamClient&&) noexcept;

    /** Blocking start for tools: begin_start() then poll() until settled. */
    [[nodiscard]] bool start() noexcept;
    /** Launch the bridge without waiting for Steam; false only if it cannot launch. */
    [[nodiscard]] bool begin_start() noexcept;
    /**
     * Advance the handshake, collect join events, notice a dead bridge and
     * run a scheduled retry. Call once per frame; it never blocks.
     */
    NativeSteamState poll() noexcept;
    void stop() noexcept;

    [[nodiscard]] NativeSteamState state() const noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] const NativeSteamIdentity* identity() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;
    /** Join requests received since the last call, oldest first. */
    [[nodiscard]] std::vector<NativeSteamJoinEvent> take_join_events();

    [[nodiscard]] std::optional<NativeSteamTicket> session_ticket() noexcept;
    [[nodiscard]] bool cancel_ticket(std::uint32_t handle) noexcept;
    [[nodiscard]] bool set_lobby_presence() noexcept;
    [[nodiscard]] bool set_server_presence(std::string_view endpoint) noexcept;
    /**
     * Publish `connect` and `status` rich presence through the retail app.
     *
     * Only for builds without the in-process Steam runtime: both are attached
     * as the same application, and presence is per application, so two
     * writers overwrite each other.
     */
    [[nodiscard]] bool set_presence(std::string_view connect, std::string_view status) noexcept;
    [[nodiscard]] bool clear_presence() noexcept;
    [[nodiscard]] bool show_achievements() noexcept;
    [[nodiscard]] bool increment_int_stat(std::string_view name,
                                          std::int32_t amount) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/** Locate the optional runtime imported by BattleSpadesAssetInstaller. */
[[nodiscard]] NativeSteamClientConfig default_native_steam_config(
    const std::filesystem::path& executable_directory) noexcept;

} // namespace battlespades::platform
