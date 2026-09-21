#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace battlespades::world {

/** CPython 2.7 random.Random for the one-byte Shoot and Damage packet seeds. */
class RetailRandom final {
public:
    explicit RetailRandom(std::uint8_t seed) noexcept {
        seed_array(static_cast<std::uint32_t>(seed));
    }

    [[nodiscard]] double random() noexcept {
        const std::uint32_t first = next() >> 5U;
        const std::uint32_t second = next() >> 6U;
        return (static_cast<double>(first) * 67'108'864.0 +
                static_cast<double>(second)) /
               9'007'199'254'740'992.0;
    }

private:
    static constexpr std::size_t state_size{624U};
    static constexpr std::size_t middle{397U};
    std::array<std::uint32_t, state_size> state_{};
    std::size_t index_{state_size};

    void seed_single(std::uint32_t seed) noexcept {
        state_[0U] = seed;
        for (std::size_t index{1U}; index < state_size; ++index) {
            const std::uint32_t previous = state_[index - 1U];
            state_[index] = 1'812'433'253U * (previous ^ (previous >> 30U)) +
                            static_cast<std::uint32_t>(index);
        }
    }

    void seed_array(std::uint32_t seed) noexcept {
        seed_single(19'650'218U);
        std::size_t state_index{1U};
        std::size_t key_index{};
        for (std::size_t count{state_size}; count > 0U; --count) {
            const std::uint32_t previous = state_[state_index - 1U];
            state_[state_index] =
                (state_[state_index] ^
                 ((previous ^ (previous >> 30U)) * 1'664'525U)) +
                seed + static_cast<std::uint32_t>(key_index);
            ++state_index;
            if (state_index >= state_size) {
                state_[0U] = state_[state_size - 1U];
                state_index = 1U;
            }
            // A one-byte retail seed becomes one little-endian key word.
            key_index = 0U;
        }
        for (std::size_t count{state_size - 1U}; count > 0U; --count) {
            const std::uint32_t previous = state_[state_index - 1U];
            state_[state_index] =
                (state_[state_index] ^
                 ((previous ^ (previous >> 30U)) * 1'566'083'941U)) -
                static_cast<std::uint32_t>(state_index);
            ++state_index;
            if (state_index >= state_size) {
                state_[0U] = state_[state_size - 1U];
                state_index = 1U;
            }
        }
        state_[0U] = 0x80000000U;
        index_ = state_size;
    }

    void twist() noexcept {
        constexpr std::uint32_t upper_mask{0x80000000U};
        constexpr std::uint32_t lower_mask{0x7FFFFFFFU};
        constexpr std::uint32_t matrix{0x9908B0DFU};
        for (std::size_t position{}; position < state_size; ++position) {
            const std::uint32_t joined =
                (state_[position] & upper_mask) |
                (state_[(position + 1U) % state_size] & lower_mask);
            state_[position] = state_[(position + middle) % state_size] ^
                               (joined >> 1U) ^
                               ((joined & 1U) != 0U ? matrix : 0U);
        }
        index_ = 0U;
    }

    [[nodiscard]] std::uint32_t next() noexcept {
        if (index_ >= state_size) {
            twist();
        }
        std::uint32_t value = state_[index_++];
        value ^= value >> 11U;
        value ^= (value << 7U) & 0x9D2C5680U;
        value ^= (value << 15U) & 0xEFC60000U;
        value ^= value >> 18U;
        return value;
    }
};

} // namespace battlespades::world
