#include "battlespades/core/utf8.hpp"

#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void ascii_names_are_limited_by_code_points() {
    const std::string name(33U, 'A');
    expect(battlespades::core::utf8_code_point_prefix(name, 32U) == std::string(32U, 'A'),
           "the retail 32-code-point player-name limit must exclude character 33");
}

void multibyte_scalar_at_the_boundary_is_kept_whole() {
    const std::string name = std::string(31U, 'A') + "\xF0\x9F\xA7\xB1" + "ignored";
    const auto prefix = battlespades::core::utf8_code_point_prefix(name, 32U);
    expect(prefix == std::string(31U, 'A') + "\xF0\x9F\xA7\xB1",
           "code-point truncation must preserve the complete U+1F9F1 sequence");
    expect(prefix.size() == 35U, "32 Unicode code points may occupy more than 32 UTF-8 bytes");
}

void malformed_sequences_fail_closed_at_the_last_valid_boundary() {
    const std::string malformed = std::string{"OK"} + "\xED\xA0\x80" + "hidden";
    expect(battlespades::core::utf8_code_point_prefix(malformed, 32U) == "OK",
           "UTF-8 surrogate encodings must not reach the text rasterizer");

    const std::string truncated = std::string{"OK"} + "\xE2\x82";
    expect(battlespades::core::utf8_code_point_prefix(truncated, 32U) == "OK",
           "a truncated multi-byte sequence must never be copied partially");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"ascii_names_are_limited_by_code_points", ascii_names_are_limited_by_code_points},
        {"multibyte_scalar_at_the_boundary_is_kept_whole",
         multibyte_scalar_at_the_boundary_is_kept_whole},
        {"malformed_sequences_fail_closed_at_the_last_valid_boundary",
         malformed_sequences_fail_closed_at_the_last_valid_boundary},
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
