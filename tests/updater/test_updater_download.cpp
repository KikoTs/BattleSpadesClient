#include "updater_test_support.hpp"

#include "battlespades/updater/http_range.hpp"
#include "battlespades/updater/sha256.hpp"
#include "battlespades/updater/update_plan.hpp"

namespace up = battlespades::updater;
namespace fs = std::filesystem;
using updater_test::expect;
using updater_test::write_file;

int main() {
    return updater_test::run("aos_updater_download_tests", [] {
        expect(up::acceptable_url("http://localhost:1234/package.zip") &&
                   up::acceptable_url("http://127.0.0.1/package.zip") &&
                   up::acceptable_url("HTTP://LOCALHOST:80/test"), "HTTP loopback mirrors allowed");
        for (const auto* url : {"http://localhost.evil.example/p", "http://127.0.0.10/p",
                               "http://127.0.0.1@evil.example/p", "http://localhost:bad/p",
                               "http://localhost:65536/p", "http://localhost:80@evil.example/p"}) {
            expect(!up::acceptable_url(url), std::string{"reject non-loopback HTTP: "} + url);
        }
        const auto range = up::parse_http_content_range(" bytes 123-999/1000\r\n");
        expect(range.has_value() && range->first == 123U && range->last == 999U && range->total == 1000U,
               "single byte range parsed");
        for (const auto* invalid : {"", "bytes */1000", "bytes 0-999/*", "bytes 0-1000/1000",
                                    "bytes 2-1/10", "bytes 0-1/2junk", "bytes -1-1/10",
                                    "bytes 0-1/18446744073709551616", "bytes 0-0/0", "bytes 0-1/2,3-4/5"}) {
            expect(!up::parse_http_content_range(invalid).has_value(), std::string{"reject invalid range: "} + invalid);
        }
        updater_test::TempDir temp{"download"};
        const auto partial = temp.path() / "package.zip.partial";
        const auto old_hash = up::sha256_hex("old package");
        const auto new_hash = up::sha256_hex("new package");
        std::string error;
        expect(up::prepare_package_partial(partial, 11U, old_hash, error) == 0U, "new download: " + error);
        write_file(partial, "old ");
        expect(up::prepare_package_partial(partial, 11U, old_hash, error) == 4U, "matching package resumes");
        expect(up::prepare_package_partial(partial, 11U, new_hash, error) == 0U && !fs::exists(partial),
               "same filename, same size, new hash: discard old bytes before transfer");
        write_file(partial, "new package");
        expect(up::prepare_package_partial(partial, 11U, new_hash, error) == 11U,
               "complete partial verified and reused without HTTP");
        write_file(partial, "bad package");
        expect(up::prepare_package_partial(partial, 11U, new_hash, error) == 0U && !fs::exists(partial),
               "corrupt complete partial restarts immediately");
        write_file(partial, "new package plus more");
        expect(up::prepare_package_partial(partial, 11U, new_hash, error) == 0U && !fs::exists(partial),
               "oversized partial restarts immediately");
        auto identity = partial;
        identity += ".identity";
        fs::remove(identity);
        write_file(partial, "new ");
        expect(up::prepare_package_partial(partial, 11U, new_hash, error) == 4U && fs::exists(identity),
               "old installer partials are adopted once without losing progress");
    });
}
