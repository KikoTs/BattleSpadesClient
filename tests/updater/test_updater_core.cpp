// Version comparison, SHA-256, release manifest / GitHub parsing, launcher
// argument handling (Steam %command%) and updater.json.

#include "updater_test_support.hpp"

#include "battlespades/updater/launcher_args.hpp"
#include "battlespades/updater/release_manifest.hpp"
#include "battlespades/updater/semver.hpp"
#include "battlespades/updater/sha256.hpp"
#include "battlespades/updater/updater_config.hpp"

namespace up = battlespades::updater;
using updater_test::expect;

namespace {

void test_versions() {
    const auto parsed = up::parse_version("v0.2.0-beta.1");
    expect(parsed.has_value() && parsed->major == 0U && parsed->minor == 2U && parsed->patch == 0U &&
               parsed->prerelease.size() == 2U,
           "tag with v prefix and pre-release parses");
    expect(up::to_string(*parsed) == "0.2.0-beta.1", "round trip to text");
    expect(up::is_newer_version("0.2.0", "0.2.0-beta.1"), "release outranks its pre-release");
    expect(up::is_newer_version("0.2.0-beta.2", "0.2.0-beta.1"), "beta.2 > beta.1");
    expect(up::is_newer_version("0.2.0-beta.10", "0.2.0-beta.9"), "numeric identifiers compare numerically");
    expect(up::is_newer_version("0.2.0-rc.1", "0.2.0-beta.5"), "rc > beta lexically");
    expect(up::is_newer_version("0.2.0-beta.1.1", "0.2.0-beta.1"), "longer pre-release wins on ties");
    expect(up::is_newer_version("0.10.0", "0.9.9"), "minor compares numerically");
    expect(up::is_newer_version("1.0.0", "0.99.99"), "major dominates");
    expect(!up::is_newer_version("0.2.0-beta.1", "0.2.0-beta.1"), "equal is not newer");
    expect(!up::is_newer_version("0.2.0+build.9", "0.2.0+build.1"), "build metadata is ignored");
    expect(!up::is_newer_version("0.1.9", "0.2.0-beta.1"), "older release is not newer");
    expect(!up::is_newer_version("garbage", "0.1.0"), "unparseable candidate is never newer");
    expect(!up::is_newer_version("1.0.0", "garbage"), "unparseable installed version blocks comparison");
    expect(up::parse_version("1.2").has_value() && up::parse_version("1.2")->patch == 0U, "missing patch");
    expect(!up::parse_version("01.2.3").has_value(), "leading zero rejected");
    expect(!up::parse_version("1.2.3-beta..1").has_value(), "empty identifier rejected");
    expect(!up::parse_version("1.2.3.4").has_value(), "four numeric parts rejected");
    expect(up::compare_versions(*up::parse_version("1.0.0-alpha"), *up::parse_version("1.0.0-alpha.1")) < 0 &&
               up::compare_versions(*up::parse_version("1.0.0-alpha.beta"), *up::parse_version("1.0.0-beta")) < 0 &&
               up::compare_versions(*up::parse_version("1.0.0-beta.11"), *up::parse_version("1.0.0-rc.1")) < 0,
           "semver.org precedence examples");
}

void test_sha256() {
    expect(up::sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty");
    expect(up::sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc");
    expect(up::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
           "two-block vector");
    const std::string million(1000000U, 'a');
    expect(up::sha256_hex(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
           "one million a");
    updater_test::TempDir temp{"sha"};
    updater_test::write_file(temp.path() / "file.bin", million);
    std::string error;
    const auto digest = up::sha256_hex_file(temp.path() / "file.bin", error);
    expect(digest.has_value() && *digest == up::sha256_hex(million), "streamed file hash matches");
    expect(up::same_sha256("CDC76E5C9914FB9281A1C7E284D73E67F1809A48A497200E046D39CCC7112CD0", *digest),
           "digest comparison is case-insensitive");
    expect(!up::same_sha256("abc", "abc"), "short strings are never valid digests");
}

constexpr std::string_view release_json = R"({
  "tag_name": "v0.3.0-beta.1",
  "name": "Beta 0.3",
  "draft": false,
  "prerelease": false,
  "html_url": "https://github.com/KikoTs/BattleSpadesClient/releases/tag/v0.3.0-beta.1",
  "assets": [
    {"name": "BattleSpadesClient-0.3.0-beta.1-Windows-AMD64.zip", "size": 1234,
     "browser_download_url": "https://github.com/KikoTs/BattleSpadesClient/releases/download/v0.3.0-beta.1/BattleSpadesClient-0.3.0-beta.1-Windows-AMD64.zip"},
    {"name": "BattleSpadesClient-0.3.0-beta.1-Windows-AMD64.zip.sha256", "size": 100,
     "browser_download_url": "https://example.invalid/sum"},
    {"name": "battlespades-release.json", "size": 600, "browser_download_url": "https://example.invalid/manifest"}
  ]
})";

constexpr std::string_view manifest_json = R"({
  "schema": 1,
  "product": "BattleSpadesClient",
  "version": "0.3.0-beta.1",
  "channel": "beta",
  "platforms": {
    "windows-x64": {
      "package": "BattleSpadesClient-0.3.0-beta.1-Windows-AMD64.zip",
      "size": 1234,
      "sha256": "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
      "root": "BattleSpadesClient-0.3.0-beta.1-Windows-AMD64/bin"
    }
  },
  "preserve": ["ui-layout.json"],
  "mirror_directories": ["shaders", "server/_internal"],
  "remove": ["obsolete.dll"]
})";

void test_manifest_and_release() {
    std::string error;
    const auto release = up::parse_github_release(release_json, error);
    expect(release.has_value() && release->assets.size() == 3U, "GitHub release parses: " + error);

    std::string url;
    expect(up::plan_release(*release, url, error) == up::ResolveStage::need_manifest &&
               url == "https://example.invalid/manifest",
           "a published manifest is preferred");
    const auto update = up::resolve_with_manifest(*release, manifest_json, error);
    expect(update.has_value(), "manifest resolves: " + error);
    expect(update->version == "0.3.0-beta.1" && update->package.sha256 == up::sha256_hex("abc"),
           "digest is normalised to lower case");
    expect(update->package.url.find("/releases/download/v0.3.0-beta.1/") != std::string::npos,
           "package URL comes from the matching release asset");
    expect(update->package.root == "BattleSpadesClient-0.3.0-beta.1-Windows-AMD64/bin", "package root kept");
    expect(update->manifest.mirror_directories.size() == 2U && update->manifest.remove.size() == 1U,
           "apply options kept");

    std::string wrong_size{manifest_json};
    updater_test::replace_all(wrong_size, "\"size\": 1234", "\"size\": 99");
    expect(!up::resolve_with_manifest(*release, wrong_size, error).has_value(),
           "manifest size must match the uploaded asset");
    std::string bad_digest{manifest_json};
    updater_test::replace_all(bad_digest, "BA7816BF", "ZZ7816BF");
    expect(!up::resolve_with_manifest(*release, bad_digest, error).has_value(), "non-hex digest rejected");
    std::string escaping{manifest_json};
    updater_test::replace_all(escaping, "\"ui-layout.json\"", "\"../settings.toml\"");
    expect(!up::parse_release_manifest(escaping, error).has_value(), "paths escaping the install are rejected");
    std::string other_product{manifest_json};
    updater_test::replace_all(other_product, "\"BattleSpadesClient\"", "\"BattleSpades\"");
    expect(!up::parse_release_manifest(other_product, error).has_value(), "the server's manifest is rejected");
    std::string http_url{manifest_json};
    updater_test::replace_all(http_url, "\"size\": 1234,", "\"size\": 1234, \"url\": \"http://evil.invalid/x.zip\",");
    expect(!up::parse_release_manifest(http_url, error).has_value(), "plain-http package URLs are rejected");

    // Without a manifest, the CPack ZIP and its .sha256 are enough.
    auto checksum_release = *release;
    checksum_release.assets.pop_back();
    expect(up::plan_release(checksum_release, url, error) == up::ResolveStage::need_checksum &&
               url == "https://example.invalid/sum",
           "falls back to the CPack checksum asset");
    const auto from_sum = up::resolve_with_checksum(
        checksum_release,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  "
        "BattleSpadesClient-0.3.0-beta.1-Windows-AMD64.zip\n",
        error);
    expect(from_sum.has_value() && from_sum->version == "0.3.0-beta.1" && from_sum->package.size == 1234U,
           "checksum release resolves: " + error);
    expect(!from_sum->manifest.mirror_directories.empty(), "checksum release gets default apply options");
    expect(!up::resolve_with_checksum(checksum_release,
                                      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  other.zip\n",
                                      error)
                .has_value(),
           "a checksum for another file is not accepted");

    auto insecure = checksum_release;
    insecure.assets[0].download_url = "http://mirror.invalid/pkg.zip";
    expect(!up::resolve_with_checksum(insecure,
                                      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n", error)
                .has_value(),
           "plain-http asset URLs are refused");

    auto unverifiable = checksum_release;
    unverifiable.assets.pop_back();
    expect(up::plan_release(unverifiable, url, error) == up::ResolveStage::unusable,
           "a package without any checksum is never installed");

    expect(up::parse_sha256_file("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD *pkg.zip\r\n",
                                 "pkg.zip") == up::sha256_hex("abc"),
           "binary-mode sha256sum lines parse");
    expect(up::parse_sha256_file("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n", "pkg.zip")
               .has_value(),
           "bare digest parses");

    const auto list = up::parse_github_release_list(
        R"([{"tag_name":"v0.4.0-beta.1","prerelease":true,"assets":[]},
            {"tag_name":"v0.3.1","prerelease":false,"assets":[]},
            {"tag_name":"v9.0.0","draft":true,"assets":[]},
            {"tag_name":"nightly","assets":[]}])",
        error);
    expect(list.has_value() && list->size() == 4U, "release list parses");
    expect(up::pick_newest_release(*list, false)->tag_name == "v0.3.1", "stable channel skips pre-releases");
    expect(up::pick_newest_release(*list, true)->tag_name == "v0.4.0-beta.1", "pre-release channel");
    expect(!up::parse_github_release(R"({"message":"Not Found"})", error).has_value() &&
               error.find("Not Found") != std::string::npos,
           "GitHub error bodies are reported");
}

void test_launcher_arguments() {
    const auto steam = up::parse_launcher_arguments(
        {"C:\\Program Files (x86)\\Steam\\steamapps\\common\\aceofspades\\aos.exe", "+connect_lobby",
         "109775241"});
    expect(steam.dropped_command.ends_with("aos.exe"), "Steam's %command% executable is dropped");
    expect(steam.forwarded.size() == 2U && steam.forwarded[0] == "+connect_lobby" && steam.forwarded[1] == "109775241",
           "Steam's trailing arguments are forwarded");

    const auto mixed = up::parse_launcher_arguments({"--no-update", "D:\\Games\\AOS.EXE", "--connect",
                                                     "1.2.3.4:32887", "--update-api", "https://localhost/x"});
    expect(mixed.no_update && mixed.api_url == "https://localhost/x" && mixed.forwarded.size() == 2U,
           "launcher switches are consumed, the rest forwarded");
    const auto plain = up::parse_launcher_arguments({"--connect", "steam:76561198000000001"});
    expect(plain.dropped_command.empty() && plain.forwarded.size() == 2U, "no Steam command, nothing dropped");
    const auto twice = up::parse_launcher_arguments({"a.exe", "b.exe"});
    expect(twice.dropped_command == "a.exe" && twice.forwarded == std::vector<std::string>{"b.exe"},
           "only the first executable is Steam's");
    expect(!up::parse_launcher_arguments({"--update-api"}).error.empty(), "missing switch value reported");

    expect(up::quote_windows_argument("plain") == "plain", "simple argument unquoted");
    expect(up::quote_windows_argument("") == "\"\"", "empty argument quoted");
    expect(up::quote_windows_argument("with space") == "\"with space\"", "space quoted");
    expect(up::quote_windows_argument("say \"hi\"") == "\"say \\\"hi\\\"\"", "embedded quotes escaped");
    expect(up::quote_windows_argument("C:\\dir with space\\") == "\"C:\\dir with space\\\\\"",
           "trailing backslash doubled before the closing quote");
    expect(up::build_windows_command_line("C:\\A B\\c.exe", {"+connect", "1.2.3.4:1"}) ==
               "\"C:\\A B\\c.exe\" +connect 1.2.3.4:1",
           "command line assembly");
}

void test_updater_config() {
    std::string error;
    auto config = up::parse_updater_config(R"({"include_prereleases": true, "check_timeout_ms": 10})", error);
    expect(config.include_prereleases && config.check_timeout_ms == 1000U, "timeout clamps to 1 s");
    expect(up::release_endpoint(config) ==
               "https://api.github.com/repos/KikoTs/BattleSpadesClient/releases?per_page=20",
           "pre-release channel lists releases");
    config = up::parse_updater_config("{}", error);
    expect(up::release_endpoint(config) == "https://api.github.com/repos/KikoTs/BattleSpadesClient/releases/latest",
           "default endpoint is /releases/latest");
    config = up::parse_updater_config(R"({"repository": "evil/../x", "api_url": "http://evil.invalid"})", error);
    expect(config.repository == "KikoTs/BattleSpadesClient" && config.api_url.empty(),
           "invalid repository and insecure api_url are ignored");
    config = up::parse_updater_config(R"({"api_url": "http://127.0.0.1:8080/latest.json"})", error);
    expect(config.api_url == "http://127.0.0.1:8080/latest.json", "localhost test mirror allowed");
    config = up::parse_updater_config("not json", error);
    expect(config.enabled && !error.empty(), "broken updater.json falls back to defaults");
}

} // namespace

int main() {
    return updater_test::run("aos_updater_core_tests", [] {
        test_versions();
        test_sha256();
        test_manifest_and_release();
        test_launcher_arguments();
        test_updater_config();
    });
}
