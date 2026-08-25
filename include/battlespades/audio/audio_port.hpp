#pragma once

#include "battlespades/core/runtime_module.hpp"

#include <cstdint>

namespace battlespades::audio {

using SoundHandle = std::uint32_t;

struct SoundPosition final {
    float x{};
    float y{};
    float z{};
};

/**
 * Audio boundary for the future OpenAL Soft adapter.
 *
 * Gameplay emits sound events through handles; it never calls OpenAL directly.
 */
class AudioPort : public core::RuntimeModule {
public:
    virtual void set_listener(SoundPosition position) = 0;
    virtual void play_one_shot(SoundHandle sound, SoundPosition position, float gain) = 0;
    virtual void stop_all() noexcept = 0;
};

} // namespace battlespades::audio
