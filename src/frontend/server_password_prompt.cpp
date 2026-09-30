#include "battlespades/frontend/server_password_prompt.hpp"

#include <algorithm>
#include <utility>

namespace battlespades::frontend {
namespace {

[[nodiscard]] bool continuation_byte(char value) noexcept {
    return (static_cast<unsigned char>(value) & 0xC0U) == 0x80U;
}

[[nodiscard]] int hex_digit(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

/** Percent-decoding; nothing on a malformed escape or a control character. */
[[nodiscard]] bool percent_decode(std::string_view text, std::string& output) {
    output.clear();
    for (std::size_t index{}; index < text.size(); ++index) {
        char value = text[index];
        if (value == '%') {
            if (index + 2U >= text.size()) return false;
            const int high = hex_digit(text[index + 1U]);
            const int low = hex_digit(text[index + 2U]);
            if (high < 0 || low < 0) return false;
            value = static_cast<char>(high * 16 + low);
            index += 2U;
        }
        if (static_cast<unsigned char>(value) < 0x20U || value == 0x7F) return false;
        output.push_back(value);
    }
    return true;
}

} // namespace

void ServerPasswordPromptModel::wipe() noexcept {
    std::fill(text_.begin(), text_.end(), '\0');
    text_.clear();
}

void ServerPasswordPromptModel::open(bool rejected) {
    wipe();
    visible_ = true;
    rejected_ = rejected;
}

void ServerPasswordPromptModel::close() noexcept {
    wipe();
    visible_ = false;
    rejected_ = false;
}

std::string ServerPasswordPromptModel::masked() const {
    const auto characters = static_cast<std::size_t>(
        std::ranges::count_if(text_, [](char value) { return !continuation_byte(value); }));
    return std::string(characters, '*');
}

bool ServerPasswordPromptModel::append_text(std::string_view text) {
    if (!visible_ || text.empty() || text_.size() + text.size() > maximum_bytes) return false;
    const bool printable = std::ranges::all_of(text, [](char value) {
        const auto byte = static_cast<unsigned char>(value);
        return byte >= 0x20U && byte != 0x7FU;
    });
    if (!printable) return false;
    text_.append(text);
    return true;
}

bool ServerPasswordPromptModel::erase_character() noexcept {
    if (!visible_ || text_.empty()) return false;
    while (!text_.empty() && continuation_byte(text_.back())) {
        text_.back() = '\0';
        text_.pop_back();
    }
    if (!text_.empty()) {
        text_.back() = '\0';
        text_.pop_back();
    }
    return true;
}

std::optional<std::string> ServerPasswordPromptModel::submit() {
    if (!visible_ || text_.empty()) return std::nullopt;
    std::string answer = text_;
    wipe();
    visible_ = false;
    return answer;
}

EndpointWithPassword split_endpoint_password(std::string_view text) {
    constexpr std::string_view marker{"?password="};
    const auto position = text.find(marker);
    if (position == std::string_view::npos) return {std::string{text}, {}};
    const auto encoded = text.substr(position + marker.size());
    // Trailing whitespace belongs to the command line, not to the password.
    const auto end = encoded.find_last_not_of(" \t\r\n");
    std::string password;
    if (end == std::string_view::npos ||
        !percent_decode(encoded.substr(0U, end + 1U), password) || password.empty() ||
        password.size() > ServerPasswordPromptModel::maximum_bytes) {
        return {std::string{text}, {}};
    }
    return {std::string{text.substr(0U, position)}, std::move(password)};
}

} // namespace battlespades::frontend
