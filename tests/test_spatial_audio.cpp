#include "battlespades/audio/retail_mix.hpp"
#include "battlespades/audio/spatial_pcm.hpp"
#include "battlespades/render/camera_basis.hpp"

#if defined(__APPLE__)
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif

#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
void check(bool result, const char* message) {
    if (!result) throw std::runtime_error{message};
}

using OpenLoopback = ALCdevice* (ALC_APIENTRY*)(const ALCchar*);
using RenderSamples = void (ALC_APIENTRY*)(ALCdevice*, ALCvoid*, ALCsizei);
constexpr ALCint format_channels = 0x1990;
constexpr ALCint format_type = 0x1991;
constexpr ALCint stereo_channels = 0x1501;
constexpr ALCint float_samples = 0x1406;
constexpr ALenum direct_channels = 0x1033;

struct Mixer final {
    ALCdevice* device{};
    ALCcontext* context{};
    ALuint source{};
    std::array<ALuint, 2> buffers{};
    RenderSamples render{};

    ~Mixer() {
        if (context) {
            alDeleteSources(1, &source);
            alDeleteBuffers(2, buffers.data());
            alcMakeContextCurrent(nullptr);
            alcDestroyContext(context);
        }
        if (device) alcCloseDevice(device);
    }

    std::array<double, 2> energy(bool relative, float source_y, double yaw,
                                 float translated_z = 0.0F, double pitch = 0.0) {
        const auto basis = battlespades::render::world_camera_basis(yaw, pitch);
        const std::array<ALfloat, 6> orientation{
            static_cast<float>(basis.forward[0]), static_cast<float>(basis.forward[1]),
            static_cast<float>(basis.forward[2]), static_cast<float>(basis.up[0]),
            static_cast<float>(basis.up[1]), static_cast<float>(basis.up[2])};
        alListener3f(AL_POSITION, 256.0F, 256.0F, 40.0F + translated_z);
        alListenerfv(AL_ORIENTATION, orientation.data());
        alSourceStop(source);
        alSourcei(source, AL_BUFFER, 0);
        alSourcei(source, AL_SOURCE_RELATIVE, relative ? AL_TRUE : AL_FALSE);
        if (alIsExtensionPresent("AL_SOFT_direct_channels"))
            alSourcei(source, direct_channels, relative ? AL_TRUE : AL_FALSE);
        alSourcef(source, AL_ROLLOFF_FACTOR, 0.0F);
        alSourcei(source, AL_LOOPING, AL_TRUE);
        const auto position = relative ? battlespades::audio::SoundPosition{} :
            battlespades::audio::retail_source_position(
                {255.5F, 255.5F + source_y, 40.5F + translated_z});
        alSource3f(source, AL_POSITION, position.x, position.y, position.z);
        alSourcei(source, AL_BUFFER, static_cast<ALint>(buffers[relative ? 0U : 1U]));
        alSourcePlay(source);
        check(alGetError() == AL_NO_ERROR, "configure world/local loopback source");
        std::vector<float> output(16'384U);
        render(device, output.data(), 8192);
        check(alcGetError(device) == ALC_NO_ERROR, "render loopback samples");
        std::array<double, 2> result{};
        // Ignore the source's short gain ramp; compare actual speaker energy.
        for (std::size_t i = 2048; i < output.size(); ++i)
            result[i % 2U] += static_cast<double>(output[i]) * output[i];
        return result;
    }
};
} // namespace

int main() {
    try {
        std::cerr << "Checking spatial PCM conversion\n" << std::flush;
        using battlespades::audio::spatial_mono_pcm;
        const std::array<short, 8> extremes{32767, 32767, -32768, -32768, 32000, -16000, 0, 0};
        check(spatial_mono_pcm(extremes) == std::vector<short>{32767, -32768, 8000, 0},
              "stereo downmix must not overflow or change duration");
        bool rejected{};
        try { static_cast<void>(spatial_mono_pcm(std::span{extremes}.first(3))); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "reject an incomplete stereo frame");

        std::cerr << "Loading OpenAL loopback entry points\n" << std::flush;
        const auto open = reinterpret_cast<OpenLoopback>(
            alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT"));
        const auto render = reinterpret_cast<RenderSamples>(
            alcGetProcAddress(nullptr, "alcRenderSamplesSOFT"));
        if (!open || !render) {
            std::cout << "SKIP: OpenAL implementation has no loopback renderer\n";
            return 77;
        }
        Mixer mixer;
        std::cerr << "Opening OpenAL loopback device\n" << std::flush;
        mixer.device = open(nullptr);
        check(mixer.device != nullptr, "open loopback device");
        const std::array<ALCint, 7> attributes{
            ALC_FREQUENCY, 48000, format_channels, stereo_channels, format_type, float_samples, 0};
        std::cerr << "Creating OpenAL loopback context\n" << std::flush;
        mixer.context = alcCreateContext(mixer.device, attributes.data());
        check(mixer.context && alcMakeContextCurrent(mixer.context), "create loopback context");
        mixer.render = render;
        alGenSources(1, &mixer.source);
        alGenBuffers(2, mixer.buffers.data());
        // A real stereo asset with different authored levels in each channel.
        std::vector<short> stereo(24'000U);
        for (std::size_t frame{}; frame < stereo.size() / 2U; ++frame) {
            const double wave = std::sin(2.0 * std::numbers::pi * 600.0 *
                                         static_cast<double>(frame) / 48000.0);
            stereo[frame * 2U] = static_cast<short>(9000.0 * wave);
            stereo[frame * 2U + 1U] = static_cast<short>(3000.0 * wave);
        }
        const auto mono = spatial_mono_pcm(stereo);
        alBufferData(mixer.buffers[0], AL_FORMAT_STEREO16, stereo.data(),
                     static_cast<ALsizei>(stereo.size() * sizeof(short)), 48000);
        alBufferData(mixer.buffers[1], AL_FORMAT_MONO16, mono.data(),
                     static_cast<ALsizei>(mono.size() * sizeof(short)), 48000);
        check(alGetError() == AL_NO_ERROR, "upload stereo and spatial PCM");

        std::cerr << "Rendering world panning samples\n" << std::flush;
        const auto right = mixer.energy(false, 12.0F, 180.0);
        const auto left = mixer.energy(false, -12.0F, 180.0);
        const auto turned = mixer.energy(false, 12.0F, 0.0);
        const auto raised = mixer.energy(false, 12.0F, 180.0, 176.0F, 60.0);
        check(right[1] > right[0] * 5.0 && right[1] > 0.01, "right-side world sound must pan right");
        check(left[0] > left[1] * 5.0 && left[0] > 0.01, "left-side world sound must pan left");
        check(turned[0] > turned[1] * 5.0, "turning the listener must reverse world panning");
        check(raised[1] > raised[0] * 5.0, "Classic z-offset and pitch must preserve handedness");
        std::cerr << "Rendering local stereo samples\n" << std::flush;
        const auto local = mixer.energy(true, 12.0F, 180.0);
        const auto local_turned = mixer.energy(true, -12.0F, 0.0);
        check(local[0] > local[1] * 7.0 && local[0] < local[1] * 11.0,
              "head-relative playback must preserve authored stereo balance");
        check(std::abs(local[0] / local[1] - local_turned[0] / local_turned[1]) < 0.01,
              "head-relative stereo must not follow world position or view rotation");
        std::cerr << "Spatial PCM and real OpenAL left/right/local-stereo rendering passed\n" << std::flush;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
