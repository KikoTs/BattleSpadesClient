#include "battlespades/network/revival_identity.hpp"
#include "battlespades/core/build_info.hpp"
#include "battlespades/network/cosmetic_slots.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sodium.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#else
#include <sys/stat.h>
#endif

namespace battlespades::network {
namespace {

using Json = nlohmann::json;

constexpr std::string_view user_agent{
    "BattleSpadesClient/" AOS_VERSION_STRING " (AoS Revival protocol 168)"};
constexpr std::size_t state_size_limit{64U * 1'024U};

struct HttpResult final {
    long status{};
    std::string body;
    std::string error_code;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error.empty() && status >= 200L && status < 300L;
    }
};

struct WriteBuffer final {
    std::string body;
    std::size_t maximum{};
    bool overflow{};
};

[[nodiscard]] int curl_cancel(void* context,
                              curl_off_t,
                              curl_off_t,
                              curl_off_t,
                              curl_off_t) noexcept {
    const auto* stop = static_cast<const std::stop_token*>(context);
    return stop != nullptr && stop->stop_requested() ? 1 : 0;
}

[[nodiscard]] std::string url_encode(std::string_view value) {
    constexpr char hex[]{"0123456789ABCDEF"};
    std::string output;
    output.reserve(value.size());
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0 || character == '-' || character == '_' ||
            character == '.' || character == '~') {
            output.push_back(character);
        } else {
            output.push_back('%');
            output.push_back(hex[(byte >> 4U) & 0x0FU]);
            output.push_back(hex[byte & 0x0FU]);
        }
    }
    return output;
}

[[nodiscard]] std::size_t curl_write(char* data,
                                     std::size_t size,
                                     std::size_t count,
                                     void* userdata) noexcept {
    const auto bytes = size * count;
    auto& output = *static_cast<WriteBuffer*>(userdata);
    if (bytes > output.maximum - (std::min)(output.maximum, output.body.size())) {
        output.overflow = true;
        return 0U;
    }
    output.body.append(data, bytes);
    return bytes;
}

void initialize_libraries() {
    static const bool initialized = [] {
        if (sodium_init() < 0) return false;
        return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    }();
    if (!initialized) {
        throw std::runtime_error{"identity cryptography/network initialization failed"};
    }
}

[[nodiscard]] std::string json_string(const Json& value,
                                      std::string_view key) {
    const auto found = value.find(key);
    return found != value.end() && found->is_string()
               ? found->get<std::string>()
               : std::string{};
}

[[nodiscard]] bool json_bool(const Json& value,
                             std::string_view key,
                             bool fallback = false) {
    const auto found = value.find(key);
    return found != value.end() && found->is_boolean()
               ? found->get<bool>()
               : fallback;
}

[[nodiscard]] std::optional<RevivalAccount> account_from_json(
    const Json& value) {
    if (!value.is_object()) return std::nullopt;
    RevivalAccount account;
    account.public_id = json_string(value, "public_id");
    if (const auto legacy = value.find("legacy_id"); legacy != value.end()) {
        if (legacy->is_string()) account.legacy_id = legacy->get<std::string>();
        else if (legacy->is_number_integer())
            account.legacy_id = std::to_string(legacy->get<std::int64_t>());
        else if (legacy->is_number_unsigned())
            account.legacy_id = std::to_string(legacy->get<std::uint64_t>());
    }
    account.nickname = json_string(value, "nickname");
    account.account_type = json_string(value, "account_type");
    account.identity_type = json_string(value, "identity_type");
    account.ranked_eligible = json_bool(value, "ranked_eligible");
    account.offline = json_bool(value, "offline");
    if (account.nickname.empty() || account.nickname.front() == '~') {
        return std::nullopt;
    }
    return account;
}

[[nodiscard]] Json account_to_json(const RevivalAccount& account) {
    return Json{
        {"public_id", account.public_id},
        {"legacy_id", account.legacy_id},
        {"nickname", account.nickname},
        {"account_type", account.account_type},
        {"identity_type", account.identity_type},
        {"ranked_eligible", account.ranked_eligible},
        {"offline", account.offline},
    };
}

[[nodiscard]] std::string base64_encode(std::span<const unsigned char> input,
                                        int variant) {
    std::string output(
        sodium_base64_encoded_len(input.size(), variant), '\0');
    sodium_bin2base64(output.data(),
                     output.size(),
                     input.data(),
                     input.size(),
                     variant);
    output.resize(std::char_traits<char>::length(output.c_str()));
    return output;
}

[[nodiscard]] std::optional<std::vector<unsigned char>>
base64_decode(std::string_view input, int variant) {
    std::vector<unsigned char> output(input.size() + 4U);
    std::size_t bytes{};
    if (sodium_base642bin(output.data(),
                         output.size(),
                         input.data(),
                         input.size(),
                         nullptr,
                         &bytes,
                         nullptr,
                         variant) != 0) {
        return std::nullopt;
    }
    output.resize(bytes);
    return output;
}

#if defined(_WIN32)
[[nodiscard]] std::optional<std::string> protect_secret(
    std::span<const unsigned char> clear) {
    DATA_BLOB input{
        static_cast<DWORD>(clear.size()),
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(clear.data()))};
    DATA_BLOB output{};
    constexpr wchar_t description[]{L"AoS Revival launcher credential"};
    if (CryptProtectData(&input,
                         description,
                         nullptr,
                         nullptr,
                         nullptr,
                         0U,
                         &output) == FALSE) {
        return std::nullopt;
    }
    const auto protected_value = std::span<const unsigned char>{
        output.pbData, static_cast<std::size_t>(output.cbData)};
    auto encoded = std::string{"dpapi:"} +
                   base64_encode(protected_value,
                                 sodium_base64_VARIANT_ORIGINAL);
    LocalFree(output.pbData);
    return encoded;
}

[[nodiscard]] std::optional<std::vector<unsigned char>>
unprotect_secret(std::string_view protected_value) {
    constexpr std::string_view prefix{"dpapi:"};
    if (!protected_value.starts_with(prefix)) return std::nullopt;
    auto decoded = base64_decode(protected_value.substr(prefix.size()),
                                 sodium_base64_VARIANT_ORIGINAL);
    if (!decoded.has_value()) return std::nullopt;
    DATA_BLOB input{
        static_cast<DWORD>(decoded->size()),
        reinterpret_cast<BYTE*>(decoded->data())};
    DATA_BLOB output{};
    if (CryptUnprotectData(
            &input, nullptr, nullptr, nullptr, nullptr, 0U, &output) == FALSE) {
        return std::nullopt;
    }
    std::vector<unsigned char> clear(
        output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    return clear;
}
#else
[[nodiscard]] std::optional<std::string> protect_secret(
    std::span<const unsigned char> clear) {
    return std::string{"portable:"} +
           base64_encode(clear, sodium_base64_VARIANT_ORIGINAL);
}

[[nodiscard]] std::optional<std::vector<unsigned char>>
unprotect_secret(std::string_view protected_value) {
    constexpr std::string_view prefix{"portable:"};
    if (!protected_value.starts_with(prefix)) return std::nullopt;
    return base64_decode(protected_value.substr(prefix.size()),
                         sodium_base64_VARIANT_ORIGINAL);
}
#endif

[[nodiscard]] std::string lowercase(std::string_view value) {
    std::string result{value};
    std::ranges::transform(result, result.begin(), [](char character) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    });
    return result;
}

[[nodiscard]] bool service_unavailable(const HttpResult& result) noexcept {
    return result.status >= 500L || result.error_code == "network_error" ||
           result.error_code == "invalid_response";
}

[[nodiscard]] std::string bounded_guest_name(std::string fingerprint) {
    return "Guest-" + fingerprint.substr(0U, 8U);
}

[[nodiscard]] std::optional<std::string> environment_value(
    const char* name) {
#if defined(_WIN32)
    char* value{};
    std::size_t size{};
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string result{value};
    std::free(value);
    return result;
#else
    const auto* value = std::getenv(name);
    if (value == nullptr) return std::nullopt;
    return std::string{value};
#endif
}

} // namespace

std::filesystem::path default_revival_state_path() {
    // Test/portable launchers may isolate identity state without touching the
    // user's installed session. Normal desktop launches never set this.
    if (const auto override_path =
            environment_value("AOS_REVIVAL_STATE_PATH");
        override_path.has_value() && !override_path->empty()) {
        return std::filesystem::path{*override_path};
    }
#if defined(_WIN32)
    if (const auto local = environment_value("LOCALAPPDATA");
        local.has_value() && !local->empty()) {
        return std::filesystem::path{*local} / "AoS Revival" /
               "launcher_state.json";
    }
#endif
    if (const auto home = environment_value("HOME");
        home.has_value() && !home->empty()) {
        return std::filesystem::path{*home} / ".local" / "share" /
               "AoS Revival" / "launcher_state.json";
    }
    return std::filesystem::current_path() / "AoS Revival" /
           "launcher_state.json";
}

bool valid_revival_username(std::string_view username) noexcept {
    if (username.size() < 3U || username.size() > 24U ||
        std::isalpha(static_cast<unsigned char>(username.front())) == 0) {
        return false;
    }
    return std::ranges::all_of(username, [](char character) {
        const auto value = static_cast<unsigned char>(character);
        return std::isalnum(value) != 0 || character == '_';
    });
}

bool valid_revival_registration_password(std::string_view password,
                                         std::string_view username) noexcept {
    if (password.size() < 12U || password.size() > 256U ||
        password.find('\0') != std::string_view::npos) {
        return false;
    }
    const auto normalized = lowercase(password);
    const auto username_key = lowercase(username);
    constexpr std::array common{
        std::string_view{"123456789012"},
        std::string_view{"aceofspades"},
        std::string_view{"battlespades"},
        std::string_view{"letmein123456"},
        std::string_view{"password1234"},
        std::string_view{"qwerty123456"},
    };
    return std::ranges::find(common, normalized) == common.end() &&
           (username_key.empty() ||
            normalized.find(username_key) == std::string::npos);
}

RevivalWorkshopProject read_revival_workshop_project(
    const std::filesystem::path& maps_root, std::string_view uid) {
    const std::filesystem::path filename{uid};
    if (uid.empty() || uid.size() > 180U || uid.find('\0') != std::string_view::npos ||
        filename.has_parent_path() || filename.filename() != filename || filename.extension() != ".ugc") {
        throw std::runtime_error{"Invalid Map Creator project identifier."};
    }
    const auto root = std::filesystem::canonical(maps_root);
    RevivalWorkshopProject result;
    constexpr std::array extensions{std::string_view{".ugc"}, std::string_view{".vxl"},
        std::string_view{".txt"}, std::string_view{".png"}};
    for (const auto extension : extensions) {
        auto path = root / filename;
        path.replace_extension(extension);
        const auto status = std::filesystem::symlink_status(path);
        if (!std::filesystem::exists(status) && extension == ".png") continue;
        if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status) ||
            std::filesystem::canonical(path).parent_path() != root) {
            throw std::runtime_error{"The project needs regular UGC, VXL and TXT files; links are not publishable."};
        }
        const auto size = std::filesystem::file_size(path);
        const auto modified = std::filesystem::last_write_time(path);
        const auto limit = extension == ".ugc" ? 1U << 20U : 64U << 20U;
        if (size == 0U || size > limit) throw std::runtime_error{"A project file is empty or exceeds the archive size limit."};
        RevivalWorkshopFile file;
        file.filename = path.filename().string();
        file.modified_ticks = std::to_string(static_cast<long long>(modified.time_since_epoch().count()));
        file.kind = extension == ".vxl" ? "map" : extension == ".png" ? "preview" : "metadata";
        file.content_type = extension == ".txt" ? "text/plain" : extension == ".png" ? "image/png" : "application/octet-stream";
        file.bytes.resize(static_cast<std::size_t>(size));
        std::ifstream input{path, std::ios::binary};
        if (!input.read(reinterpret_cast<char*>(file.bytes.data()), static_cast<std::streamsize>(size)) ||
            input.peek() != std::char_traits<char>::eof() ||
            modified != std::filesystem::last_write_time(path)) {
            throw std::runtime_error{"The project changed while preparing publication. Save it and retry."};
        }
        std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
        crypto_hash_sha256(digest.data(), file.bytes.data(), static_cast<unsigned long long>(file.bytes.size()));
        std::array<char, crypto_hash_sha256_BYTES * 2U + 1U> hex{};
        sodium_bin2hex(hex.data(), hex.size(), digest.data(), digest.size());
        file.sha256 = hex.data();
        if (extension == ".ugc") {
            const auto document = Json::parse(file.bytes);
            result.description = document.value("description", std::string{});
            result.author = document.value("author", std::string{});
            if (const auto tags = document.find("tags"); tags != document.end() && tags->is_array()) {
                for (const auto& tag : *tags) {
                    if (tag.is_string() && tag.get_ref<const std::string&>().size() <= 32U && result.tags.size() < 12U)
                        result.tags.push_back(tag.get<std::string>());
                }
            }
        }
        result.files.push_back(std::move(file));
    }
    if (result.description.size() > 16000U || result.author.size() > 128U) {
        throw std::runtime_error{"Project description or author exceeds the archive limit."};
    }
    return result;
}

namespace {
[[nodiscard]] bool save_workshop_receipt(const std::filesystem::path& maps_root, std::string_view uid,
    const RevivalWorkshopProject& project, std::string_view url) noexcept {
    try {
        const auto root = std::filesystem::canonical(maps_root);
        const auto destination = root / (std::string{uid} + ".publication.json");
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(destination))) return false;
        auto temporary = destination;
        temporary += "." + std::to_string(randombytes_random()) + ".tmp";
        if (std::filesystem::exists(std::filesystem::symlink_status(temporary))) return false;
        Json files = Json::array();
        for (const auto& file : project.files) files.push_back({{"filename", file.filename},
            {"size", file.bytes.size()}, {"modified_ticks", file.modified_ticks}, {"sha256", file.sha256}});
        const auto payload = Json{{"version", 1}, {"url", url}, {"files", files}}.dump(2);
        {
            std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
            if (!output.write(payload.data(), static_cast<std::streamsize>(payload.size()))) return false;
            output.flush();
            if (!output) return false;
        }
#if defined(_WIN32)
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(temporary);
            return false;
        }
#else
        std::filesystem::rename(temporary, destination);
#endif
        return true;
    } catch (...) { return false; }
}
} // namespace

class RevivalIdentityService::Impl final {
public:
    explicit Impl(RevivalIdentityConfig source)
        : config{std::move(source)} {
        initialize_libraries();
        if (config.state_path.empty()) {
            config.state_path = default_revival_state_path();
        }
        if (!valid_base_url(config.api_base)) {
            throw std::invalid_argument{
                "AoSPlay identity API must use HTTPS or loopback HTTP"};
        }
        while (!config.api_base.empty() && config.api_base.back() == '/') {
            config.api_base.pop_back();
        }
        load();
    }

    [[nodiscard]] static bool valid_base_url(std::string_view url) noexcept {
        return url.starts_with("https://") ||
               url.starts_with("http://127.0.0.1") ||
               url.starts_with("http://localhost");
    }

    [[nodiscard]] std::optional<RevivalAccount> cached_account() const {
        std::scoped_lock lock{mutex};
        return account;
    }

    [[nodiscard]] bool has_online_session() const {
        std::scoped_lock lock{mutex};
        return !access_token.empty();
    }

    [[nodiscard]] RevivalAuthResult refresh() {
        std::scoped_lock lock{mutex};
        if (access_token.empty()) {
            if (account.has_value() && account->offline) {
                return RevivalAuthResult{account};
            }
            return error("authentication_required",
                         "Choose Sign in, Register, or Play as guest.");
        }
        const auto response = request("/api/auth/me", "GET", std::nullopt, access_token);
        if (!response) {
            if (service_unavailable(response) && account.has_value()) {
                return RevivalAuthResult{account};
            }
            clear_session();
            static_cast<void>(save());
            return from_http_error(response);
        }
        const auto body = parse_json(response);
        if (!body.has_value() || !json_bool(*body, "authenticated")) {
            clear_session();
            static_cast<void>(save());
            return error("authentication_required",
                         "Your session expired. Please sign in again.",
                         response.status);
        }
        const auto next = account_from_json((*body)["account"]);
        if (!next.has_value()) {
            return error("invalid_response",
                         "AoSPlay returned an invalid account.",
                         response.status);
        }
        account = next;
        state["account"] = account_to_json(*account);
        if (!save()) {
            return error("state_write_failed",
                         "Could not update the protected account state.");
        }
        return RevivalAuthResult{account};
    }

    [[nodiscard]] RevivalAuthResult login(std::string username,
                                          std::string password) {
        std::scoped_lock lock{mutex};
        if (!valid_revival_username(username) || password.empty()) {
            wipe(password);
            return error("invalid_credentials",
                         "Enter a valid username and password.");
        }
        const Json payload{
            {"username", username},
            {"password", password},
            {"client", "launcher"},
        };
        auto response = request("/api/auth/login",
                                "POST",
                                std::optional<Json>{payload},
                                {});
        wipe(password);
        return accept_session(response);
    }

    [[nodiscard]] RevivalAuthResult register_account(std::string username,
                                                     std::string password) {
        std::scoped_lock lock{mutex};
        if (!valid_revival_username(username)) {
            wipe(password);
            return error("invalid_username",
                         "Use 3-24 characters, start with a letter, and use only letters, numbers, or underscore.");
        }
        if (!valid_revival_registration_password(password, username)) {
            wipe(password);
            return error("invalid_password",
                         "Use a 12-128 character password without your username or a common phrase.");
        }
        const Json payload{
            {"username", username},
            {"password", password},
            {"client", "launcher"},
        };
        auto response = request("/api/auth/register",
                                "POST",
                                std::optional<Json>{payload},
                                {});
        wipe(password);
        return accept_session(response);
    }

    [[nodiscard]] RevivalAuthResult guest_login() {
        std::scoped_lock lock{mutex};
        auto seed = guest_seed();
        if (!seed.has_value()) {
            std::array<unsigned char, crypto_sign_SEEDBYTES> next{};
            randombytes_buf(next.data(), next.size());
            if (!store_secret("guest_seed", next) || !save()) {
                return error("state_write_failed",
                             "Could not protect the guest identity.");
            }
            seed = next;
        }

        std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> public_key{};
        std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secret_key{};
        crypto_sign_seed_keypair(public_key.data(), secret_key.data(), seed->data());
        const auto public_value =
            base64_encode(public_key, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
        auto challenge = request("/api/auth/guest/challenge",
                                 "POST",
                                 std::optional<Json>{
                                     Json{{"public_key", public_value}}},
                                 {});
        if (!challenge) {
            sodium_memzero(secret_key.data(), secret_key.size());
            return service_unavailable(challenge)
                       ? offline_guest(public_key, challenge.error)
                       : from_http_error(challenge);
        }
        const auto challenge_json = parse_json(challenge);
        if (!challenge_json.has_value()) {
            sodium_memzero(secret_key.data(), secret_key.size());
            return offline_guest(public_key,
                                 "AoSPlay returned an invalid guest challenge.");
        }
        const auto challenge_id = json_string(*challenge_json, "challenge_id");
        const auto nonce = json_string(*challenge_json, "nonce");
        const auto message = json_string(*challenge_json, "message");
        if (challenge_id.empty() || nonce.empty() || message.empty()) {
            sodium_memzero(secret_key.data(), secret_key.size());
            return error("invalid_guest_challenge",
                         "AoSPlay returned an incomplete guest challenge.");
        }
        std::array<unsigned char, crypto_sign_BYTES> signature{};
        crypto_sign_detached(signature.data(),
                             nullptr,
                             reinterpret_cast<const unsigned char*>(message.data()),
                             static_cast<unsigned long long>(message.size()),
                             secret_key.data());
        sodium_memzero(secret_key.data(), secret_key.size());
        const auto signature_value =
            base64_encode(signature, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
        auto complete = request(
            "/api/auth/guest/complete",
            "POST",
            std::optional<Json>{
                Json{{"challenge_id", challenge_id},
                     {"nonce", nonce},
                     {"signature", signature_value},
                     {"client", "launcher"}}},
            {});
        if (!complete && service_unavailable(complete)) {
            return offline_guest(public_key, complete.error);
        }
        return accept_session(complete);
    }

    [[nodiscard]] RevivalAuthResult logout() {
        std::scoped_lock lock{mutex};
        if (!access_token.empty()) {
            static_cast<void>(request(
                "/api/auth/logout",
                "POST",
                std::optional<Json>{Json::object()},
                access_token));
        }
        clear_session();
        if (!save()) {
            return error("state_write_failed",
                         "Signed out, but could not update the protected state.");
        }
        return {};
    }

    [[nodiscard]] RevivalTicketResult game_ticket(std::string server_id) {
        // Copy the token and release the lock before the request, as
        // create_relay_lobby does. Holding it across the call puts a join
        // behind every other AoSPlay request in progress: a Steam joiner sat
        // on "Authorizing with AoSPlay..." for minutes while presence and
        // social syncs took their turns, though each request times out in
        // five seconds.
        std::string token;
        {
            std::scoped_lock lock{mutex};
            token = access_token;
        }
        if (token.empty()) {
            return RevivalTicketResult{
                {}, "authentication_required",
                "Choose Sign in or Play as guest first."};
        }
        auto response = request("/api/auth/game-ticket",
                                "POST",
                                std::optional<Json>{
                                    Json{{"server_id", server_id},
                                         {"client_capabilities", Json::array({"battlespades-cosmetics-v1"})}}},
                                token);
        if (!response) {
            const auto result = from_http_error(response);
            return RevivalTicketResult{{}, result.error_code, result.error};
        }
        const auto body = parse_json(response);
        if (!body.has_value()) {
            return RevivalTicketResult{
                {}, "invalid_response",
                "AoSPlay returned invalid ticket data."};
        }
        auto ticket = json_string(*body, "join_code");
        if (ticket.empty()) ticket = json_string(*body, "ticket");
        if (ticket.size() != 15U || ticket.front() != '~' ||
            !std::ranges::all_of(ticket, [](char character) {
                return static_cast<unsigned char>(character) < 0x80U;
            })) {
            return RevivalTicketResult{
                {}, "invalid_join_code",
                "AoSPlay returned an incompatible join code."};
        }
        RevivalTicketResult result;
        result.join_code = std::move(ticket);
        return result;
    }

    [[nodiscard]] RevivalRelayLobbyResult create_relay_lobby(
        const RevivalRelayLobbyRequest& values,
        std::stop_token stop) {
        std::string token;
        {
            std::scoped_lock lock{mutex};
            token = access_token;
        }
        RevivalRelayLobbyResult result;
        if (token.empty()) {
            result.error_code = "authentication_required";
            result.error = "Sign in to publish a community match.";
            return result;
        }
        const auto payload = Json{{"name", values.name},
                                  {"map", values.map},
                                  {"game_mode", values.game_mode},
                                  {"mode_tla", values.mode_tla},
                                  {"max_players", values.max_players},
                                  {"playlist_id", values.playlist_id},
                                  {"texture_skin", values.texture_skin},
                                  {"classic", values.classic},
                                  {"steam_host_id", values.steam_host_id}};
        const auto response = request("/api/lobbies", "POST",
                                      std::optional<Json>{payload}, token,
                                      std::chrono::milliseconds{8'000}, stop);
        wipe(token);
        result.http_status = response.status;
        if (!response) {
            result.error_code = response.error_code.empty() ? "relay_lobby_error"
                                                             : response.error_code;
            result.error = response.error.empty() ? "Could not allocate a public match relay."
                                                   : response.error;
            return result;
        }
        const auto body = parse_json(response);
        if (!body.has_value()) {
            result.error_code = "invalid_response";
            result.error = "AoSPlay returned invalid relay data.";
            return result;
        }
        RevivalRelayLobby lobby;
        lobby.lobby_id = json_string(*body, "lobby_id");
        lobby.server_id = json_string(*body, "server_id");
        lobby.server_token = json_string(*body, "server_token");
        lobby.master_url = json_string(*body, "master_url");
        const auto tunnel = body->find("tunnel");
        if (tunnel != body->end() && tunnel->is_object()) {
            lobby.allocation_id = json_string(*tunnel, "allocation_id");
            lobby.relay_host = json_string(*tunnel, "host");
            lobby.host_key = json_string(*tunnel, "host_key");
            const auto port = tunnel->find("port");
            if (port != tunnel->end() && port->is_number_integer()) {
                const auto value = port->get<std::int64_t>();
                if (value > 0 && value <= 65'535) {
                    lobby.relay_port = static_cast<std::uint16_t>(value);
                }
            }
            const auto keepalive = tunnel->find("keepalive_seconds");
            if (keepalive != tunnel->end() && keepalive->is_number_integer()) {
                const auto value = keepalive->get<std::int64_t>();
                if (value > 0 && value <= 300) {
                    lobby.keepalive_seconds = static_cast<std::uint16_t>(value);
                }
            }
        }
        if (lobby.lobby_id.empty() || lobby.server_id.empty() ||
            !lobby.server_token.starts_with("aos_lobby_") ||
            lobby.master_url.empty() || lobby.allocation_id.size() != 36U ||
            lobby.relay_host.empty() || lobby.relay_port == 0U ||
            lobby.host_key.size() != 43U) {
            wipe(lobby.server_token);
            wipe(lobby.host_key);
            result.error_code = "invalid_response";
            result.error = "AoSPlay returned incomplete relay credentials.";
            return result;
        }
        result.lobby = std::move(lobby);
        return result;
    }

    [[nodiscard]] bool close_relay_lobby(const RevivalRelayLobby& lobby,
                                         std::stop_token stop) {
        if (lobby.lobby_id.empty() || lobby.server_token.empty()) return false;
        auto credential = lobby.server_token;
        const auto response = request("/api/lobbies/" + url_encode(lobby.lobby_id),
                                      "DELETE", std::nullopt, credential,
                                      std::chrono::milliseconds{5'000}, stop);
        wipe(credential);
        return static_cast<bool>(response) || response.status == 404L;
    }

    [[nodiscard]] AosPlayProfileResult own_profile() {
        std::string token;
        { const std::scoped_lock lock{mutex}; token=access_token; }
        if (token.empty()) {
            return {{}, "Sign in to view your persistent profile."};
        }
        const auto response = request("/api/profile/me", "GET", std::nullopt, token);
        wipe(token);
        if (!response) {
            return {{}, response.error.empty() ? "Could not load your AoSPlay profile."
                                               : response.error};
        }
        auto parsed = parse_aosplay_profile(response.body);
        if (!parsed) {
            return parsed;
        }
        if (!parsed.profile.has_value()) {
            parsed.error = "AoSPlay returned no profile for this account.";
        }
        return parsed;
    }

    /** The caller holds mutex so account and its bearer token share a snapshot. */
    [[nodiscard]] std::filesystem::path hosted_results_directory_locked() const {
        if (!account || account->public_id.empty() || !std::ranges::all_of(account->public_id, [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        })) return {};
        return config.state_path.parent_path() / "hosted-results" / account->public_id;
    }

    [[nodiscard]] std::filesystem::path hosted_results_directory() const {
        const std::scoped_lock lock{mutex};
        return hosted_results_directory_locked();
    }

    [[nodiscard]] HostedResultsUpload flush_hosted_results(std::stop_token stop) {
        HostedResultsUpload result;
        // Only one pass may own the scan position. Other identity operations
        // remain independent while an upload waits for HTTP.
        if (stop.stop_requested()) return result;
        const std::unique_lock upload_lock{hosted_results_mutex, std::try_to_lock};
        if (!upload_lock.owns_lock()) return result;
        std::filesystem::path directory;
        std::string token;
        {
            const std::scoped_lock lock{mutex};
            directory = hosted_results_directory_locked();
            token = access_token;
        }
        struct TokenGuard { std::string& value; ~TokenGuard() { wipe(value); } } guard{token};
        if (directory.empty() || token.empty() || stop.stop_requested()) return result;
        try {
            const auto root_status = std::filesystem::symlink_status(directory);
            if (!std::filesystem::exists(root_status)) return result;
            if (std::filesystem::is_symlink(root_status) || !std::filesystem::is_directory(root_status))
                throw std::runtime_error{"Hosted results directory is not a regular directory."};
            // Resume after the previous batch, even if every HTTP request in
            // that batch failed. Retaining the iterator also lets bounded
            // scans progress through more than 4,096 directory entries.
            // At the end, the next pass rescans to see newly mirrored files.
            if (hosted_results_scan_directory != directory ||
                hosted_results_next == std::filesystem::directory_iterator{}) {
                hosted_results_next = std::filesystem::directory_iterator{directory};
                hosted_results_scan_directory = directory;
            }
            std::size_t examined{}, attempted{};
            while (hosted_results_next != std::filesystem::directory_iterator{} &&
                   !stop.stop_requested() && examined < 4096U && attempted < 16U) {
                const auto entry = *hosted_results_next;
                ++hosted_results_next;
                ++examined;
                std::error_code file_error;
                if (entry.path().extension() != ".json" ||
                    !std::filesystem::is_regular_file(entry.symlink_status(file_error)) || file_error) continue;
                const auto bytes = entry.file_size(file_error);
                if (file_error || bytes == 0U || bytes > 256U * 1024U) continue;
                const auto read_report = [](const std::filesystem::path& path) {
                    std::ifstream stream{path, std::ios::binary};
                    std::string content(256U * 1024U + 1U, '\0');
                    stream.read(content.data(), static_cast<std::streamsize>(content.size()));
                    content.resize(static_cast<std::size_t>(stream.gcount()));
                    return content;
                };
                const auto encoded = read_report(entry.path());
                if (encoded.empty() || encoded.size() > 256U * 1024U) continue;
                auto payload = Json::parse(encoded, nullptr, false);
                if (!payload.is_object() || !payload.contains("relay_lobby_id") ||
                    !payload.contains("event_id") || !payload["event_id"].is_string() ||
                    payload["event_id"].get_ref<const std::string&>() != entry.path().stem().string()) continue;
                {
                    const std::scoped_lock lock{mutex};
                    if (hosted_results_directory_locked() != directory || access_token != token) break;
                }
                ++attempted;
                // A cold result transaction can outlive the short social-action
                // deadline. This runs off the UI thread and remains cancellable;
                // only an acknowledgement removes its durable retry file.
                const auto response = request("/api/master/stats", "POST", std::move(payload), token, std::chrono::seconds{15}, stop);
                const auto acknowledgement = response ? parse_json(response) : std::nullopt;
                if (!acknowledgement || !acknowledgement->value("accepted", false)) {
                    result.error = response.error.empty() ? "Hosted results are queued for retry." : response.error;
                    continue;
                }
                // A second uploader or server may have acknowledged the same file.
                // Do not remove a different report replaced during this request.
                const auto current_bytes = read_report(entry.path());
                if (current_bytes != encoded) continue;
                if (std::filesystem::remove(entry.path())) ++result.uploaded;
            }
        } catch (const std::filesystem::filesystem_error& error) {
            hosted_results_next = {};
            hosted_results_scan_directory.clear();
            result.error = error.what();
        } catch (const std::exception& error) { result.error = error.what(); }
        return result;
    }

    [[nodiscard]] InventoryResult inventory_request(const InventoryRequest& action, std::stop_token stop) {
        std::string token;
        {
            const std::scoped_lock lock{mutex};
            if (!action.expected_account.empty() &&
                (!account || account->public_id != action.expected_account))
                return {{}, "Your account changed. Reload your collection.", "account_changed", 401};
            token = access_token;
        }
        if (token.empty()) return {{}, "Sign in to load your collection.", "authentication_required", 401};
        struct TokenGuard { std::string& value; ~TokenGuard() { wipe(value); } } guard{token};
        std::string path{"/api/inventory/snapshot?compact=1"}, method{"GET"};
        std::optional<Json> payload;
        switch (action.kind) {
        case InventoryRequestKind::snapshot: break;
        case InventoryRequestKind::crates:
            path = "/api/crates/me?limit=50&cursor=" + url_encode(action.target); break;
        case InventoryRequestKind::history:
            path = "/api/crates/openings?limit=50&cursor=" + url_encode(action.target); break;
        case InventoryRequestKind::open:
            path = "/api/crates/" + url_encode(action.target) + "/open";
            method = "POST";
            payload = Json{{"schema_version", 1}, {"idempotency_key", action.idempotency_key}, {"client_nonce", action.nonce}, {"include_collection", true}};
            break;
        case InventoryRequestKind::equip:
        case InventoryRequestKind::unequip:
            path = "/api/inventory/equipped/" + url_encode(
                std::string{cosmetic_storage_slot(action.cosmetic_id,action.target)});
            method = action.kind == InventoryRequestKind::equip ? "PUT" : "DELETE";
            payload = Json{{"schema_version", 1}, {"cosmetic_id", action.cosmetic_id},
                {"expected_inventory_revision", action.revision}, {"content_version", 5}, {"include_collection", true}};
            break;
        }
        const auto response = request(path, method, std::move(payload), token, std::chrono::seconds{8}, stop, 512U*1024U);
        if (!response) return {{}, response.error.empty() ? "Collection service is unavailable." : response.error,
            response.error_code, response.status};
        const auto parsed = parse_json(response);
        if (!parsed || !parsed->is_object() || parsed->value("schema_version", 0) != 1)
            return {{}, "Collection response is unsupported.", "invalid_collection", response.status};
        return {*parsed, {}, {}, response.status};
    }

    [[nodiscard]] RevivalSocialResult social_request(
        const RevivalSocialRequest& social,
        std::stop_token stop) {
        std::string token;
        {
            std::scoped_lock lock{mutex};
            token = access_token;
        }
        RevivalSocialResult failure;
        failure.request = social;
        if (token.empty()) {
            failure.error_code = "authentication_required";
            failure.error = "Social features require an online Revival account.";
            return failure;
        }

        std::string path;
        std::string method{"GET"};
        std::optional<Json> payload;
        auto timeout = config.timeout;
        switch (social.kind) {
        case RevivalSocialRequestKind::sync: {
            path = "/api/social/sync?cursor=" + url_encode(social.cursor) +
                   "&client_instance_id=" + url_encode(social.client_instance_id) +
                   "&status=" + url_encode(social.presence);
            if (social.payload.is_object() && !social.payload.empty()) {
                path += "&metadata=" + url_encode(social.payload.dump());
            }
            timeout = std::chrono::milliseconds{6'000};
            break;
        }
        case RevivalSocialRequestKind::presence_offline:
            path = "/api/social/sync";
            method = "DELETE";
            payload = Json{{"client_instance_id", social.client_instance_id}};
            timeout = std::chrono::milliseconds{4'000};
            break;
        case RevivalSocialRequestKind::find_friends:
            path = "/api/social/friends";
            if (!social.query.empty()) path += "?query=" + url_encode(social.query);
            timeout = std::chrono::milliseconds{6'000};
            break;
        case RevivalSocialRequestKind::friend_action:
            path = "/api/social/friends";
            method = "POST";
            payload = Json{{"action", social.action}, {"target", social.target}};
            timeout = std::chrono::milliseconds{6'000};
            break;
        case RevivalSocialRequestKind::list_lobbies:
            path = "/api/social/lobbies";
            timeout = std::chrono::milliseconds{6'000};
            break;
        case RevivalSocialRequestKind::create_lobby: {
            path = "/api/social/lobbies";
            method = "POST";
            auto body = social.payload.is_object() ? social.payload : Json::object();
            body["action"] = "create";
            payload = std::move(body);
            timeout = std::chrono::milliseconds{6'000};
            break;
        }
        case RevivalSocialRequestKind::lobby_action: {
            if (social.lobby_id.empty()) {
                failure.error_code = "invalid_lobby";
                failure.error = "The selected lobby is no longer available.";
                wipe(token);
                return failure;
            }
            path = "/api/social/lobbies/" + url_encode(social.lobby_id);
            method = "POST";
            auto body = social.payload.is_object() ? social.payload : Json::object();
            body["action"] = social.action;
            if (!social.target.empty()) body["target"] = social.target;
            payload = std::move(body);
            timeout = std::chrono::milliseconds{8'000};
            break;
        }
        }

        const auto response = request(path, method, std::move(payload), token, timeout, stop);
        wipe(token);
        if (!response) {
            failure.error_code = response.error_code.empty() ? "http_error" : response.error_code;
            failure.error = response.error.empty() ? "AoSPlay rejected the social request."
                                                   : response.error;
            failure.http_status = response.status;
            return failure;
        }
        auto parsed = parse_revival_social_response(social, response.body);
        parsed.http_status = response.status;
        return parsed;
    }

    [[nodiscard]] RevivalWorkshopResult publish_ugc_project(
        const std::filesystem::path& maps_root, std::string_view uid,
        std::string_view title, std::stop_token stop) {
        std::string token;
        {
            const std::scoped_lock lock{mutex};
            if (access_token.empty() || !account || account->account_type != "registered")
                return {{}, "Sign in to a registered AoSPlay account to publish a map."};
            token = access_token;
        }
        // The scoped account token is used only with the configured AoSPlay
        // origin; Blob receives a separate, single-file upload credential.
        struct TokenGuard { std::string& value; ~TokenGuard() { wipe(value); } } guard{token};
        try {
            if (stop.stop_requested()) throw std::runtime_error{"Publication cancelled."};
            if (title.empty() || title.size() > 96U) throw std::runtime_error{"The map needs a title of 1–96 characters."};
            const auto project = read_revival_workshop_project(maps_root, uid);
            const auto success = [&](std::string url) {
                const auto saved = save_workshop_receipt(maps_root, uid, project, url);
                return RevivalWorkshopResult{std::move(url), {}, saved ? "" :
                    "The map was published, but its local publication receipt could not be saved."};
            };
            Json manifest = Json::array();
            for (const auto& file : project.files) manifest.push_back({{"filename", file.filename},
                {"size", file.bytes.size()}, {"sha256", file.sha256}});
            const auto call = [&](Json payload) {
                if (stop.stop_requested()) throw std::runtime_error{"Publication cancelled."};
                const auto response = request("/api/workshop/native", "POST", std::move(payload),
                    token, std::chrono::seconds{60}, stop);
                if (!response) throw std::runtime_error{response.error};
                const auto parsed = parse_json(response);
                if (!parsed) throw std::runtime_error{"The archive returned an invalid response."};
                return *parsed;
            };
            const auto item_url = [&](const Json& response) {
                const auto path = json_string(response, "path");
                if (!path.starts_with("/workshop/") || path.size() > 106U ||
                    path.size() <= 10U || !std::ranges::all_of(path.substr(10), [](unsigned char value) {
                        return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '-';
                    })) throw std::runtime_error{"The archive returned an invalid item page."};
                return config.api_base + path;
            };
            const auto prepared = call({{"action", "prepare"}, {"submission", {
                {"title", title}, {"description", project.description}, {"original_author", project.author},
                {"item_type", "map"}, {"source_type", "community"}, {"tags", project.tags}, {"files", manifest}}}});
            if (prepared.value("published", false)) return success(item_url(prepared));
            const auto& submission = prepared.at("submission");
            const auto item = json_string(submission, "id");
            const auto& assets = submission.at("assets");
            if (item.size() != 36U || !assets.is_array() || assets.size() != project.files.size())
                throw std::runtime_error{"The archive returned an invalid file manifest."};
            for (const auto& file : project.files) {
                const auto asset = std::ranges::find_if(assets, [&](const auto& value) {
                    return json_string(value, "filename") == file.filename;
                });
                if (asset == assets.end()) throw std::runtime_error{"The archive file manifest is incomplete."};
                const auto asset_id = json_string(*asset, "id");
                const auto credentials = call({{"action", "token"}, {"item_id", item}, {"asset_id", asset_id}});
                if (credentials.value("uploaded", false)) continue;
                const auto upload_path = json_string(credentials, "pathname");
                auto upload_token = json_string(credentials, "token");
                TokenGuard upload_guard{upload_token};
                if (upload_path != json_string(*asset, "uploadPath") ||
                    !upload_path.starts_with("workshop/" + item + "/" + asset_id + "/") ||
                    upload_path.size() > 512U || !upload_token.starts_with("vercel_blob_client_") ||
                    upload_token.find_first_of("\r\n") != std::string::npos || upload_token.size() > 8192U ||
                    json_string(credentials, "api_version") != "12")
                    throw std::runtime_error{"The archive returned an invalid upload target."};
                // A lost PUT response is reconciled by the verified object at
                // its database-owned path, including on a later Publish retry.
                const auto uploaded = put_workshop_blob(upload_path, upload_token, file, stop);
                const auto verified = call({{"action", "verify"}, {"item_id", item}, {"asset_id", asset_id}});
                if (!verified.value("uploaded", false)) throw std::runtime_error{
                    uploaded ? "The archive has not verified this file yet. Retry Publish." : "The map upload was interrupted. Retry Publish to resume."};
            }
            const auto complete = call({{"action", "complete"}, {"item_id", item}});
            return success(item_url(complete));
        } catch (const std::exception& exception) { return {{}, exception.what()}; }
    }

private:
    [[nodiscard]] static bool put_workshop_blob(std::string_view path, std::string_view token,
        const RevivalWorkshopFile& file, std::stop_token stop) {
        auto* handle = curl_easy_init();
        if (handle == nullptr) return false;
        const auto url = "https://vercel.com/api/blob/?pathname=" + url_encode(path);
        curl_slist* headers = nullptr;
        for (const auto& header : std::array<std::string, 6>{"Authorization: Bearer " + std::string{token},
            "x-api-version: 12", "x-content-type: " + file.content_type,
            "Content-Type: " + file.content_type, "x-add-random-suffix: 0", "x-vercel-blob-access: public"})
            headers = curl_slist_append(headers, header.c_str());
        WriteBuffer write{{}, 65536U, false};
        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, file.bytes.data());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(file.bytes.size()));
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &curl_write);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &write);
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, &curl_cancel);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &stop);
        const auto result = curl_easy_perform(handle);
        long status{};
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(handle);
        return result == CURLE_OK && status >= 200L && status < 300L && !write.overflow;
    }

    [[nodiscard]] HttpResult request(std::string_view path,
                                     std::string_view method,
                                     std::optional<Json> payload,
                                     std::string_view token,
                                     std::chrono::milliseconds timeout = {},
                                     std::stop_token stop = {},
                                     std::size_t maximum_payload = 0U) const {
        HttpResult result;
        CurlHandle owned{nullptr,curl_easy_cleanup};
        {
            std::lock_guard lock{transport_mutex};
            if (!idle_handles.empty()) { owned=std::move(idle_handles.back()); idle_handles.pop_back(); }
        }
        if (!owned) owned.reset(curl_easy_init());
        auto* handle = owned.get();
        if (handle == nullptr) {
            result.error_code = "network_error";
            result.error = "Could not initialize the AoSPlay request.";
            return result;
        }
        const auto url = config.api_base + std::string{path};
        const auto body = payload.has_value() ? payload->dump() : std::string{};
        WriteBuffer write{{}, maximum_payload ? maximum_payload : config.maximum_payload_bytes, false};
        char error_buffer[CURL_ERROR_SIZE]{};
        curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/json");
        if (payload.has_value()) {
            headers = curl_slist_append(headers, "Content-Type: application/json");
        }
        std::string authorization;
        if (!token.empty()) {
            authorization = "Authorization: Bearer " + std::string{token};
            headers = curl_slist_append(headers, authorization.c_str());
        }
        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(handle, CURLOPT_USERAGENT, user_agent.data());
        if (timeout <= std::chrono::milliseconds::zero()) timeout = config.timeout;
        curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout.count()));
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS,
                         static_cast<long>((std::min)(
                                               timeout,
                                               std::chrono::milliseconds{3'000})
                                               .count()));
        curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(handle, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &curl_write);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &write);
        curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, error_buffer);
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, &curl_cancel);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &stop);
        if (method == "POST" || method == "PUT") {
            if (method == "POST") curl_easy_setopt(handle, CURLOPT_POST, 1L);
            else curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.data());
            curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                             static_cast<long>(body.size()));
        } else if (method == "DELETE") {
            curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "DELETE");
            if (!body.empty()) {
                curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.data());
                curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                                 static_cast<long>(body.size()));
            }
        }

        const auto code = curl_easy_perform(handle);
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &result.status);
        curl_slist_free_all(headers);
        // Reset request credentials/options, retaining DNS, TLS and keep-alive connections.
        curl_easy_reset(handle);
        {
            std::lock_guard lock{transport_mutex};
            if (idle_handles.size()<4U) idle_handles.push_back(std::move(owned));
        }
        result.body = std::move(write.body);
        if (code != CURLE_OK || write.overflow) {
            result.error_code =
                write.overflow ? "invalid_response"
                : code == CURLE_ABORTED_BY_CALLBACK && stop.stop_requested() ? "cancelled"
                                                                          : "network_error";
            result.error = write.overflow
                               ? "AoSPlay returned too much data."
                           : code == CURLE_ABORTED_BY_CALLBACK && stop.stop_requested()
                               ? "Social request was cancelled."
                               : "Could not reach AoSPlay.";
            return result;
        }
        if (result.status < 200L || result.status >= 300L) {
            result.error_code = "http_error";
            result.error = "AoSPlay rejected the request.";
            if (const auto parsed = parse_json(result); parsed.has_value()) {
                const auto code_value = json_string(*parsed, "error");
                const auto message = json_string(*parsed, "message");
                const auto detail = json_string(*parsed, "detail");
                if (!code_value.empty()) result.error_code = code_value;
                if (!message.empty()) result.error = message;
                else if (!detail.empty()) result.error = detail;
            }
        }
        return result;
    }

    [[nodiscard]] static std::optional<Json> parse_json(
        const HttpResult& response) noexcept {
        try {
            auto value = Json::parse(response.body);
            if (!value.is_object()) return std::nullopt;
            return value;
        } catch (...) {
            return std::nullopt;
        }
    }

    [[nodiscard]] RevivalAuthResult accept_session(const HttpResult& response) {
        if (!response) return from_http_error(response);
        const auto body = parse_json(response);
        if (!body.has_value()) {
            return error("invalid_response",
                         "AoSPlay returned invalid session data.",
                         response.status);
        }
        const auto next_account = account_from_json((*body)["account"]);
        auto token = json_string(*body, "access_token");
        if (!next_account.has_value() || token.empty()) {
            wipe(token);
            return error("invalid_session",
                         "AoSPlay did not return a launcher session.",
                         response.status);
        }
        if (!store_secret(
                "access_token",
                std::span<const unsigned char>{
                    reinterpret_cast<const unsigned char*>(token.data()),
                    token.size()})) {
            wipe(token);
            return error("state_write_failed",
                         "Could not protect the launcher session.");
        }
        wipe(access_token);
        access_token = std::move(token);
        account = next_account;
        state["account"] = account_to_json(*account);
        state["api_base"] = config.api_base;
        if (const auto session = body->find("session");
            session != body->end() && session->is_object()) {
            state["session_expires_at"] = json_string(*session, "expires_at");
        }
        if (!save()) {
            clear_session();
            return error("state_write_failed",
                         "Could not save the protected launcher session.");
        }
        RevivalAuthResult result{account};
        result.recovery_code = json_string(*body, "recovery_code");
        result.http_status = response.status;
        return result;
    }

    [[nodiscard]] RevivalAuthResult offline_guest(
        std::span<const unsigned char> public_key,
        std::string reason) {
        const auto fingerprint =
            base64_encode(public_key,
                          sodium_base64_VARIANT_URLSAFE_NO_PADDING)
                .substr(0U, 8U);
        RevivalAccount offline;
        offline.nickname = bounded_guest_name(fingerprint);
        offline.legacy_id = "LOCAL-" + lowercase(fingerprint);
        std::ranges::transform(offline.legacy_id,
                               offline.legacy_id.begin(),
                               [](char character) {
                                   return static_cast<char>(
                                       std::toupper(static_cast<unsigned char>(
                                           character)));
                               });
        offline.account_type = "guest";
        offline.identity_type = "guest_offline";
        offline.offline = true;
        offline.ranked_eligible = false;
        wipe(access_token);
        access_token.clear();
        account = offline;
        state["account"] = account_to_json(offline);
        state["api_base"] = config.api_base;
        auto& secrets = state["secrets"];
        if (secrets.is_object()) secrets.erase("access_token");
        state.erase("session_expires_at");
        if (!save()) {
            return error("state_write_failed",
                         "Could not save the offline guest identity.");
        }
        RevivalAuthResult result{account};
        result.error_code = "offline_guest";
        result.error = std::move(reason);
        // Offline fallback is an accepted identity, not a failed action.
        result.error.clear();
        return result;
    }

    [[nodiscard]] static RevivalAuthResult from_http_error(
        const HttpResult& response) {
        return error(response.error_code.empty() ? "http_error"
                                                 : response.error_code,
                     response.error.empty()
                         ? "AoSPlay rejected the request."
                         : response.error,
                     response.status);
    }

    [[nodiscard]] static RevivalAuthResult error(std::string code,
                                                 std::string message,
                                                 long status = 0L) {
        RevivalAuthResult result;
        result.error_code = std::move(code);
        result.error = std::move(message);
        result.http_status = status;
        return result;
    }

    [[nodiscard]] std::optional<
        std::array<unsigned char, crypto_sign_SEEDBYTES>>
    guest_seed() const {
        auto clear = secret("guest_seed");
        if (!clear.has_value() || clear->size() != crypto_sign_SEEDBYTES) {
            return std::nullopt;
        }
        std::array<unsigned char, crypto_sign_SEEDBYTES> result{};
        std::ranges::copy(*clear, result.begin());
        sodium_memzero(clear->data(), clear->size());
        return result;
    }

    [[nodiscard]] std::optional<std::vector<unsigned char>>
    secret(std::string_view name) const {
        const auto secrets = state.find("secrets");
        if (secrets == state.end() || !secrets->is_object()) return std::nullopt;
        const auto found = secrets->find(name);
        if (found == secrets->end() || !found->is_string()) return std::nullopt;
        return unprotect_secret(found->get_ref<const std::string&>());
    }

    [[nodiscard]] bool store_secret(
        std::string_view name,
        std::span<const unsigned char> clear) {
        const auto protected_value = protect_secret(clear);
        if (!protected_value.has_value()) return false;
        if (!state["secrets"].is_object()) state["secrets"] = Json::object();
        state["secrets"][name] = *protected_value;
        return true;
    }

    void clear_session() {
        wipe(access_token);
        access_token.clear();
        account.reset();
        state.erase("account");
        state.erase("session_expires_at");
        auto secrets = state.find("secrets");
        if (secrets != state.end() && secrets->is_object()) {
            secrets->erase("access_token");
        }
    }

    void load() {
        state = Json{{"version", 1}};
        std::error_code error_code;
        const auto size = std::filesystem::file_size(config.state_path, error_code);
        if (error_code || size > state_size_limit) return;
        std::ifstream stream{config.state_path, std::ios::binary};
        if (!stream) return;
        try {
            auto loaded = Json::parse(stream);
            if (!loaded.is_object() || loaded.value("version", 0) != 1) return;
            state = std::move(loaded);
            if (const auto found = state.find("account");
                found != state.end()) {
                account = account_from_json(*found);
            }
            if (auto clear = secret("access_token");
                clear.has_value() && !clear->empty()) {
                access_token.assign(
                    reinterpret_cast<const char*>(clear->data()),
                    clear->size());
                sodium_memzero(clear->data(), clear->size());
            }
        } catch (...) {
            state = Json{{"version", 1}};
            account.reset();
            wipe(access_token);
            access_token.clear();
        }
    }

    [[nodiscard]] bool save() {
        std::error_code error_code;
        const auto parent = config.state_path.parent_path();
        std::filesystem::create_directories(parent, error_code);
        if (error_code) return false;
        state["version"] = 1;
        const auto serialized = state.dump(2);
        if (serialized.size() > state_size_limit) return false;
        auto temporary = config.state_path;
        temporary += ".tmp";
        {
            std::ofstream output{
                temporary, std::ios::binary | std::ios::trunc};
            if (!output) return false;
            output.write(serialized.data(),
                         static_cast<std::streamsize>(serialized.size()));
            output.flush();
            if (!output) return false;
        }
#if !defined(_WIN32)
        static_cast<void>(chmod(temporary.string().c_str(), S_IRUSR | S_IWUSR));
#endif
#if defined(_WIN32)
        const auto moved = MoveFileExW(
            temporary.c_str(),
            config.state_path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (moved == FALSE) {
            std::filesystem::remove(temporary, error_code);
            return false;
        }
        return true;
#else
        std::filesystem::rename(temporary, config.state_path, error_code);
        return !error_code;
#endif
    }

    static void wipe(std::string& value) noexcept {
        if (!value.empty()) sodium_memzero(value.data(), value.size());
    }

    mutable std::mutex mutex;
    using CurlHandle = std::unique_ptr<CURL,decltype(&curl_easy_cleanup)>;
    mutable std::mutex transport_mutex;
    mutable std::vector<CurlHandle> idle_handles;
    std::mutex hosted_results_mutex;
    std::filesystem::path hosted_results_scan_directory;
    std::filesystem::directory_iterator hosted_results_next;
    RevivalIdentityConfig config;
    Json state{{"version", 1}};
    std::optional<RevivalAccount> account;
    std::string access_token;
};

RevivalIdentityService::RevivalIdentityService(RevivalIdentityConfig config)
    : impl_{std::make_unique<Impl>(std::move(config))} {}

RevivalIdentityService::~RevivalIdentityService() = default;
RevivalIdentityService::RevivalIdentityService(
    RevivalIdentityService&&) noexcept = default;
RevivalIdentityService& RevivalIdentityService::operator=(
    RevivalIdentityService&&) noexcept = default;

std::optional<RevivalAccount> RevivalIdentityService::cached_account() const {
    return impl_->cached_account();
}

bool RevivalIdentityService::has_online_session() const {
    return impl_->has_online_session();
}

RevivalAuthResult RevivalIdentityService::refresh() {
    return impl_->refresh();
}

RevivalAuthResult RevivalIdentityService::login(std::string username,
                                                std::string password) {
    return impl_->login(std::move(username), std::move(password));
}

RevivalAuthResult RevivalIdentityService::register_account(
    std::string username,
    std::string password) {
    return impl_->register_account(std::move(username), std::move(password));
}

RevivalAuthResult RevivalIdentityService::guest_login() {
    return impl_->guest_login();
}

RevivalAuthResult RevivalIdentityService::logout() {
    return impl_->logout();
}

RevivalTicketResult RevivalIdentityService::game_ticket(
    std::string server_id) {
    return impl_->game_ticket(std::move(server_id));
}

RevivalRelayLobbyResult RevivalIdentityService::create_relay_lobby(
    const RevivalRelayLobbyRequest& request,
    std::stop_token stop) {
    return impl_->create_relay_lobby(request, stop);
}

bool RevivalIdentityService::close_relay_lobby(
    const RevivalRelayLobby& lobby,
    std::stop_token stop) {
    return impl_->close_relay_lobby(lobby, stop);
}

AosPlayProfileResult RevivalIdentityService::own_profile() {
    return impl_->own_profile();
}

InventoryResult RevivalIdentityService::inventory_request(const InventoryRequest& request, std::stop_token stop) {
    return impl_->inventory_request(request, stop);
}

std::filesystem::path RevivalIdentityService::hosted_results_directory() const {
    return impl_->hosted_results_directory();
}

HostedResultsUpload RevivalIdentityService::flush_hosted_results(std::stop_token stop) {
    return impl_->flush_hosted_results(stop);
}

RevivalWorkshopResult RevivalIdentityService::publish_ugc_project(
    const std::filesystem::path& maps_root, std::string_view uid,
    std::string_view title, std::stop_token stop) {
    return impl_->publish_ugc_project(maps_root, uid, title, stop);
}

RevivalSocialResult RevivalIdentityService::social_request(
    const RevivalSocialRequest& request,
    std::stop_token stop) {
    return impl_->social_request(request, stop);
}

} // namespace battlespades::network
