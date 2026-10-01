#include "battlespades/updater/sha256.hpp"

#include "battlespades/updater/file_util.hpp"

#include <fstream>
#include <vector>

namespace battlespades::updater {
namespace {

constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U,
};

[[nodiscard]] constexpr std::uint32_t rotr(std::uint32_t value, int count) noexcept {
    return (value >> count) | (value << (32 - count));
}

[[nodiscard]] int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

Sha256::Sha256() noexcept
    : state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
             0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U} {}

void Sha256::transform(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16U; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4U]) << 24U) |
               (static_cast<std::uint32_t>(block[i * 4U + 1U]) << 16U) |
               (static_cast<std::uint32_t>(block[i * 4U + 2U]) << 8U) |
               static_cast<std::uint32_t>(block[i * 4U + 3U]);
    }
    for (std::size_t i = 16U; i < 64U; ++i) {
        const auto s0 = rotr(w[i - 15U], 7) ^ rotr(w[i - 15U], 18) ^ (w[i - 15U] >> 3U);
        const auto s1 = rotr(w[i - 2U], 17) ^ rotr(w[i - 2U], 19) ^ (w[i - 2U] >> 10U);
        w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }
    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];
    for (std::size_t i = 0; i < 64U; ++i) {
        const auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const auto choose = (e & f) ^ (~e & g);
        const auto t1 = h + s1 + choose + round_constants[i] + w[i];
        const auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto t2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(std::span<const std::uint8_t> bytes) noexcept {
    length_ += bytes.size();
    for (const auto byte : bytes) {
        buffer_[buffered_++] = byte;
        if (buffered_ == buffer_.size()) {
            transform(buffer_.data());
            buffered_ = 0U;
        }
    }
}

void Sha256::update(std::string_view text) noexcept {
    update(std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(text.data()),
                                         text.size()});
}

std::array<std::uint8_t, 32> Sha256::finish() noexcept {
    const std::uint64_t bit_length = length_ * 8U;
    buffer_[buffered_++] = 0x80U;
    if (buffered_ > 56U) {
        while (buffered_ < 64U) buffer_[buffered_++] = 0U;
        transform(buffer_.data());
        buffered_ = 0U;
    }
    while (buffered_ < 56U) buffer_[buffered_++] = 0U;
    for (int shift = 56; shift >= 0; shift -= 8) {
        buffer_[buffered_++] = static_cast<std::uint8_t>((bit_length >> shift) & 0xffU);
    }
    transform(buffer_.data());
    std::array<std::uint8_t, 32> digest{};
    for (std::size_t i = 0; i < 8U; ++i) {
        digest[i * 4U] = static_cast<std::uint8_t>(state_[i] >> 24U);
        digest[i * 4U + 1U] = static_cast<std::uint8_t>(state_[i] >> 16U);
        digest[i * 4U + 2U] = static_cast<std::uint8_t>(state_[i] >> 8U);
        digest[i * 4U + 3U] = static_cast<std::uint8_t>(state_[i]);
    }
    return digest;
}

std::string to_hex(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2U);
    for (const auto byte : bytes) {
        text.push_back(digits[byte >> 4U]);
        text.push_back(digits[byte & 0x0fU]);
    }
    return text;
}

std::string sha256_hex(std::string_view data) {
    Sha256 hash;
    hash.update(data);
    const auto digest = hash.finish();
    return to_hex(digest);
}

std::optional<std::string> sha256_hex_file(const std::filesystem::path& file, std::string& error) {
    std::ifstream input{file, std::ios::binary};
    if (!input) {
        error = "cannot open " + path_to_utf8(file) + " for hashing";
        return std::nullopt;
    }
    Sha256 hash;
    std::vector<char> chunk(1U << 16U);
    while (input) {
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto count = input.gcount();
        if (count > 0) {
            hash.update(std::span<const std::uint8_t>{
                reinterpret_cast<const std::uint8_t*>(chunk.data()),
                static_cast<std::size_t>(count)});
        }
    }
    if (input.bad()) {
        error = "read error while hashing " + path_to_utf8(file);
        return std::nullopt;
    }
    const auto digest = hash.finish();
    return to_hex(digest);
}

bool is_sha256_hex(std::string_view text) noexcept {
    if (text.size() != 64U) return false;
    for (const char c : text) {
        if (hex_value(c) < 0) return false;
    }
    return true;
}

bool same_sha256(std::string_view left, std::string_view right) noexcept {
    if (!is_sha256_hex(left) || !is_sha256_hex(right)) return false;
    for (std::size_t i = 0; i < 64U; ++i) {
        if (hex_value(left[i]) != hex_value(right[i])) return false;
    }
    return true;
}

} // namespace battlespades::updater
