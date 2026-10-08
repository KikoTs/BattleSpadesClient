#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::platform {

enum class LocalServerProgram : std::uint8_t {
    game_server,
    map_creator,
};

enum class LocalServerState : std::uint8_t { unavailable, starting, ready, stopping, stopped, failed };

/** Bounded versioned native status, matched to the unique session and endpoint. */
[[nodiscard]] LocalServerState read_local_server_status(
    const std::filesystem::path& session_directory, std::uint16_t port, std::string_view mode) noexcept;

/** Retail Map Creator defaults consumed only by BattleSpadesMapCreator. */
struct LocalMapCreatorLaunchConfig final {
    std::string project{"MyUGCMap"};
    std::string terrain{"grassland"};
    std::string target_mode{"tdm"};
    std::string title{"MyUGCMap"};
    std::string author{"Player"};
    /** Must name the client-owned `hosted_ugc` directory. */
    std::filesystem::path publish_root;
    /** Directory containing the recovered `ugc/maps` and `ugc/kv6` trees. */
    std::filesystem::path retail_root;
    std::optional<std::uint8_t> prefab_set{};
};

/**
 * One normalized Create Match snapshot for an owned BattleSpades process.
 *
 * The launcher writes a disposable TOML beside its log and never edits the
 * server bundle's config. All process I/O is redirected; no console window is
 * created. The object owns only the child it launched.
 */
struct LocalServerLaunchConfig final {
    LocalServerProgram program{LocalServerProgram::game_server};
    std::filesystem::path bundle_root;
    std::filesystem::path session_parent;
    std::string server_name{"Local BattleSpades Match"};
    std::string mode{"tdm"};
    std::string map_name{"AncientEgypt"};
    std::map<std::string, std::string, std::less<>> rule_overrides;
    std::uint16_t preferred_port{27015U};
    std::uint16_t maximum_players{12U};
    std::uint16_t match_minutes{15U};
    std::uint16_t bot_count{};
    std::string bot_difficulty{"mixed"};
    /** Allowlisted child-only environment used for public relay registration. */
    std::map<std::string, std::string, std::less<>> environment_overrides;
    std::optional<LocalMapCreatorLaunchConfig> map_creator{};
    /**
     * Authored Map Creator triplet (`<map_name>.vxl/.txt/.ugc`, optional
     * `.png`) hosted from Create Match's Saved/Subscribed Maps. start()
     * copies the regular, non-symlink files into `<session>/maps` and points
     * the child's `[world] maps_path` at it; stock maps leave this empty.
     */
    std::vector<std::filesystem::path> custom_map_files{};
    /** `[world] maps_path` written into the TOML; start() fills it for custom maps. */
    std::filesystem::path maps_path{};
};

/**
 * Validates an authored map's source files: 3-4 distinct regular,
 * non-symlink files whose stem equals `map_name`, drawn from .vxl/.txt/.ugc
 * (all required) and .png (optional). Returns an empty string when valid.
 */
[[nodiscard]] std::string validate_custom_map_files(
    std::string_view map_name, const std::vector<std::filesystem::path>& files);

/** Resolve an explicit bundle or the newest complete staged release by executable age. */
[[nodiscard]] std::optional<std::filesystem::path>
find_local_server_bundle(const std::filesystem::path& root);

/** Address allocate_local_server_port() binds when it probes a port. */
enum class LocalServerPortProbe : std::uint8_t {
    /** Default and product behaviour: probe 0.0.0.0, the address the hosted server binds. */
    all_interfaces,
    /**
     * Test-only: probe 127.0.0.1. A loopback bind never raises the Windows
     * Firewall prompt, so tests that only need a free port for an owned
     * child use this. It can miss a conflict on another interface.
     */
    loopback_only,
};

/** Process-wide; call it before any start(). The product never calls it. */
void set_local_server_port_probe(LocalServerPortProbe probe) noexcept;

/** Returns the first exclusively bindable UDP port at or after `preferred`. */
[[nodiscard]] std::uint16_t allocate_local_server_port(std::uint16_t preferred,
                                                       std::string& error) noexcept;

/** Pure serializer exposed for contract tests. */
[[nodiscard]] std::string build_local_server_toml(const LocalServerLaunchConfig& config,
                                                  std::uint16_t resolved_port);

/**
 * RAII owner for one hidden local dedicated server.
 *
 * start() performs filesystem/process setup on its calling thread. Callers
 * should invoke it from a worker. stop() first asks the server's public
 * `--control-stdin` path to shut down, then terminates its owned job/process
 * group if the bounded grace period expires. POSIX retains the waitable leader
 * until cleanup so an early exit cannot orphan helpers or target a reused PID.
 */
class LocalServerProcess final {
public:
    LocalServerProcess();
    ~LocalServerProcess();

    LocalServerProcess(const LocalServerProcess&) = delete;
    LocalServerProcess& operator=(const LocalServerProcess&) = delete;
    LocalServerProcess(LocalServerProcess&&) noexcept;
    LocalServerProcess& operator=(LocalServerProcess&&) noexcept;

    [[nodiscard]] bool start(const LocalServerLaunchConfig& config, std::string& error);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] LocalServerState status() const noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] const std::filesystem::path& session_directory() const noexcept;
    [[nodiscard]] const std::filesystem::path& log_path() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::platform
