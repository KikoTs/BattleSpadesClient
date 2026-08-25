#include "battlespades/audio/openal_frontend_audio.hpp"
#include "battlespades/audio/server_audio_catalog.hpp"
#include "battlespades/audio/sound_groups.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/efx.h>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#pragma warning(disable : 4701)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#define STB_VORBIS_NO_STDIO
#include <stb_vorbis.c>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace battlespades::audio {
namespace {

constexpr std::uintmax_t maximum_compressed_bytes{64U * 1024U * 1024U};
constexpr std::size_t maximum_decoded_bytes{256U * 1024U * 1024U};

struct DecodedAudio final {
    std::vector<short> samples;
    int channels{};
    int sample_rate{};
};

struct AsyncDecodedAudio final {
    DecodedAudio audio;
    std::string error;
    bool succeeded{};
};

using audio::sound_group_stems;

/**
 * Retail HEARING_DISTANCE is 50 (shared/constants_audio.py:7): beyond it a
 * world cue is inaudible. Without an explicit reference distance OpenAL
 * defaults to 1.0, which makes everything fall off far too sharply.
 */
constexpr float retail_reference_distance{2.0F};
constexpr float retail_default_attenuation{0.15F};

struct VorbisCloser final {
    void operator()(stb_vorbis* decoder) const noexcept {
        if (decoder != nullptr) {
            stb_vorbis_close(decoder);
        }
    }
};

using VorbisPtr = std::unique_ptr<stb_vorbis, VorbisCloser>;

[[nodiscard]] std::string vorbis_error_text(int error) {
    switch (error) {
    case VORBIS__no_error:
        return "no decoder error";
    case VORBIS_need_more_data:
        return "truncated OGG stream";
    case VORBIS_invalid_api_mixing:
        return "invalid decoder API use";
    case VORBIS_outofmem:
        return "decoder allocation failed";
    case VORBIS_feature_not_supported:
        return "unsupported Vorbis feature";
    case VORBIS_too_many_channels:
        return "too many Vorbis channels";
    case VORBIS_file_open_failure:
        return "OGG file open failure";
    case VORBIS_seek_without_length:
        return "Vorbis stream length unavailable";
    case VORBIS_unexpected_eof:
        return "unexpected end of OGG stream";
    case VORBIS_seek_invalid:
        return "invalid Vorbis seek data";
    case VORBIS_invalid_setup:
        return "invalid Vorbis setup";
    case VORBIS_invalid_stream:
        return "invalid Vorbis stream";
    case VORBIS_missing_capture_pattern:
        return "missing OGG capture pattern";
    case VORBIS_invalid_stream_structure_version:
        return "unsupported OGG stream version";
    case VORBIS_continued_packet_flag_invalid:
        return "invalid continued-packet flag";
    case VORBIS_incorrect_stream_serial_number:
        return "incorrect OGG stream serial number";
    case VORBIS_invalid_first_page:
        return "invalid first OGG page";
    case VORBIS_bad_packet_type:
        return "invalid Vorbis packet type";
    case VORBIS_cant_find_last_page:
        return "cannot locate final OGG page";
    case VORBIS_seek_failed:
        return "Vorbis seek failed";
    case VORBIS_ogg_skeleton_not_supported:
        return "OGG skeleton stream is unsupported";
    default:
        return "unknown Vorbis decoder error " + std::to_string(error);
    }
}

[[nodiscard]] std::string openal_error_text(ALenum error) {
    switch (error) {
    case AL_NO_ERROR:
        return "no OpenAL error";
    case AL_INVALID_NAME:
        return "invalid OpenAL object name";
    case AL_INVALID_ENUM:
        return "invalid OpenAL enum";
    case AL_INVALID_VALUE:
        return "invalid OpenAL value";
    case AL_INVALID_OPERATION:
        return "invalid OpenAL operation";
    case AL_OUT_OF_MEMORY:
        return "OpenAL out of memory";
    default:
        return "unknown OpenAL error " + std::to_string(error);
    }
}

void clear_openal_error() noexcept {
    while (alGetError() != AL_NO_ERROR) {
    }
}

[[nodiscard]] bool finite_position(SoundPosition position) noexcept {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

[[nodiscard]] bool valid_audio_stem(std::string_view stem) noexcept {
    if (stem.empty() || stem.size() > 96U) {
        return false;
    }
    return std::ranges::all_of(stem, [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '_' || character == '-';
    });
}

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
    const auto encoded = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

[[nodiscard]] bool
read_ogg(const std::filesystem::path& path, DecodedAudio& decoded, std::string& error) {
    static_assert(sizeof(short) == 2U, "stb_vorbis PCM output requires 16-bit short");

    std::error_code filesystem_error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error) {
        error = "cannot stat audio asset '" + path_utf8(path) + "': " + filesystem_error.message();
        return false;
    }
    if (file_size == 0U || file_size > maximum_compressed_bytes ||
        file_size > static_cast<std::uintmax_t>(std::numeric_limits<int>::max())) {
        error = "audio asset has invalid compressed size: '" + path_utf8(path) + "'";
        return false;
    }

    std::ifstream input{path, std::ios::binary};
    if (!input) {
        error = "cannot open audio asset '" + path_utf8(path) + "'";
        return false;
    }

    const auto byte_count = static_cast<std::size_t>(file_size);
    std::vector<unsigned char> encoded(byte_count);
    input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(byte_count));
    if (!input || input.gcount() != static_cast<std::streamsize>(byte_count)) {
        error = "short read from audio asset '" + path_utf8(path) + "'";
        return false;
    }

    int decoder_error{};
    VorbisPtr decoder{stb_vorbis_open_memory(
        encoded.data(), static_cast<int>(encoded.size()), &decoder_error, nullptr)};
    if (decoder == nullptr) {
        error = "cannot decode '" + path_utf8(path) + "': " + vorbis_error_text(decoder_error);
        return false;
    }

    const stb_vorbis_info info = stb_vorbis_get_info(decoder.get());
    if ((info.channels != 1 && info.channels != 2) || info.sample_rate == 0U ||
        info.sample_rate > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
        error = "unsupported channel count or sample rate in '" + path_utf8(path) + "'";
        return false;
    }

    const unsigned int frame_count = stb_vorbis_stream_length_in_samples(decoder.get());
    if (frame_count == 0U) {
        error = "empty or unseekable Vorbis stream: '" + path_utf8(path) + "'";
        return false;
    }

    const auto channels = static_cast<std::size_t>(info.channels);
    const auto frames = static_cast<std::size_t>(frame_count);
    if (frames > maximum_decoded_bytes / sizeof(short) / channels) {
        error = "decoded audio exceeds safety limit: '" + path_utf8(path) + "'";
        return false;
    }

    const std::size_t sample_count = frames * channels;
    if (sample_count > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        error = "decoded audio exceeds OpenAL size limit: '" + path_utf8(path) + "'";
        return false;
    }

    decoded.samples.assign(sample_count, short{});
    std::size_t decoded_frames{};
    while (decoded_frames < frames) {
        const std::size_t sample_offset = decoded_frames * channels;
        const std::size_t remaining_samples = sample_count - sample_offset;
        const int result =
            stb_vorbis_get_samples_short_interleaved(decoder.get(),
                                                     info.channels,
                                                     decoded.samples.data() + sample_offset,
                                                     static_cast<int>(remaining_samples));
        if (result <= 0) {
            break;
        }
        decoded_frames += static_cast<std::size_t>(result);
    }

    if (decoded_frames == 0U) {
        error = "Vorbis stream produced no PCM samples: '" + path_utf8(path) + "'";
        return false;
    }
    const int decode_error = stb_vorbis_get_error(decoder.get());
    if (decode_error != VORBIS__no_error) {
        error = "Vorbis decoding failed for '" + path_utf8(path) +
                "': " + vorbis_error_text(decode_error);
        return false;
    }

    decoded.samples.resize(decoded_frames * channels);
    decoded.channels = info.channels;
    decoded.sample_rate = static_cast<int>(info.sample_rate);
    return true;
}

} // namespace

struct OpenAlFrontendAudio::Impl final {
    struct Buffer final {
        SoundHandle handle{};
        ALuint id{};
        float duration_seconds{};
    };

    struct Voice final {
        ALuint source{};
        std::uint64_t sequence{};
        bool active{};
        /**
         * Protected from eviction while it is still playing.
         *
         * Voice lines run for seconds where a cue runs for a fraction of one, so
         * oldest-first stealing picks the spawn line every time and cuts it off
         * mid-word the moment a gun fires.
         */
        bool protected_voice{};
    };

    /**
     * A loop whose lifetime is owned by Protocol 168's sound packets.
     *
     * These
     * sources are separate from local weapon loops so a map ambience
     * emitter cannot evict a
     * minigun or other held-tool sound.
     */
    struct ServerLoopSlot final {
        ALuint source{};
        std::uint8_t loop_id{};
        SoundPosition position{};
        float requested_gain{};
        bool relative{};
        bool active{};
    };

    struct PendingNamedBuffer final {
        SoundHandle handle{};
        std::filesystem::path path;
        std::future<AsyncDecodedAudio> decode;
    };

    struct NamedBufferRequest final {
        SoundHandle handle{};
        std::string canonical;
        bool pending{};
    };

    struct PendingMusicRequest final {
        std::string canonical;
        float start_offset{};
    };

    struct PendingServerLoopRequest final {
        std::string canonical;
        SoundPosition position{};
        float gain{};
        bool relative{};
        float attenuation{};
        float start_offset{};
    };

    explicit Impl(OpenAlFrontendAudioConfig requested_config)
        : config{std::move(requested_config)} {}

    [[nodiscard]] bool on_owner_thread() const noexcept {
        return owner_thread == std::this_thread::get_id();
    }

    [[nodiscard]] float resolved_spatial_gain(
        SoundPosition position,
        float requested_gain,
        bool relative,
        SpatialSoundProfile profile = SpatialSoundProfile::ordinary) const noexcept {
        float multiplier{1.0F};
        if (!relative && spatial_gain_resolver != nullptr) {
            multiplier = spatial_gain_resolver(spatial_gain_context, listener_position, position);
            if (!std::isfinite(multiplier)) {
                multiplier = 1.0F;
            }
            multiplier = profiled_spatial_transmission(profile, multiplier);
        }
        return std::clamp(requested_gain * multiplier, 0.0F, 4.0F);
    }

    [[nodiscard]] ALuint buffer_for(SoundHandle handle) const noexcept {
        const auto found =
            std::find_if(buffers.begin(), buffers.end(), [handle](const Buffer& item) {
                return item.handle == handle;
            });
        return found == buffers.end() ? 0U : found->id;
    }

    [[nodiscard]] float duration_for(SoundHandle handle) const noexcept {
        const auto found =
            std::find_if(buffers.begin(), buffers.end(), [handle](const Buffer& item) {
                return item.handle == handle;
            });
        return found == buffers.end() ? 0.0F : found->duration_seconds;
    }

    [[nodiscard]] bool
    upload_buffer(SoundHandle handle, const std::filesystem::path& path, DecodedAudio decoded) {
        ALuint buffer{};
        clear_openal_error();
        alGenBuffers(1, &buffer);
        ALenum error = alGetError();
        if (error != AL_NO_ERROR || buffer == 0U) {
            last_error = "cannot create OpenAL buffer for '" + path_utf8(path) +
                         "': " + openal_error_text(error);
            return false;
        }

        const ALenum format = decoded.channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
        const std::size_t byte_count = decoded.samples.size() * sizeof(short);
        alBufferData(buffer,
                     format,
                     decoded.samples.data(),
                     static_cast<ALsizei>(byte_count),
                     static_cast<ALsizei>(decoded.sample_rate));
        error = alGetError();
        if (error != AL_NO_ERROR) {
            alDeleteBuffers(1, &buffer);
            last_error = "cannot upload OpenAL buffer for '" + path_utf8(path) +
                         "': " + openal_error_text(error);
            return false;
        }

        const auto frames = decoded.samples.size() / static_cast<std::size_t>(decoded.channels);
        const float duration = static_cast<float>(frames) / static_cast<float>(decoded.sample_rate);
        buffers.push_back(Buffer{.handle = handle, .id = buffer, .duration_seconds = duration});
        return true;
    }

    [[nodiscard]] bool load_buffer(SoundHandle handle, const std::filesystem::path& path) {
        DecodedAudio decoded;
        if (!read_ogg(path, decoded, last_error)) {
            return false;
        }
        return upload_buffer(handle, path, std::move(decoded));
    }

    [[nodiscard]] SoundHandle load_named_buffer(std::string_view directory, std::string_view stem) {
        if (!valid_audio_stem(stem) ||
            (directory != "sounds" && directory != "ambients" && directory != "music")) {
            last_error = "unsafe named audio resource";
            return 0U;
        }
        const auto path =
            (config.asset_root / std::string{directory} / (std::string{stem} + ".ogg"))
                .lexically_normal();
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) {
            last_error = "named audio resource is unavailable: " + path_utf8(path);
            return 0U;
        }
        const auto canonical = path_utf8(path);
        if (const auto found = optional_sound_cache.find(canonical);
            found != optional_sound_cache.end()) {
            return found->second;
        }
        const SoundHandle handle = next_optional_handle++;
        if (!load_buffer(handle, path)) {
            return 0U;
        }
        optional_sound_cache.emplace(canonical, handle);
        return handle;
    }

    /**
     * Begin file I/O and Vorbis decoding away from the OpenAL owner thread.
     *
     *
     * Only the final alBufferData upload is pumped by tick(). Long server
     * music tracks
     * therefore cannot freeze the first playable frame.
     */
    [[nodiscard]] NamedBufferRequest request_named_buffer_async(std::string_view directory,
                                                                std::string_view stem) {
        if (!valid_audio_stem(stem) || (directory != "ambients" && directory != "music")) {
            last_error = "unsafe asynchronous audio resource";
            return {};
        }
        const auto path =
            (config.asset_root / std::string{directory} / (std::string{stem} + ".ogg"))
                .lexically_normal();
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) {
            last_error = "named audio resource is unavailable: " + path_utf8(path);
            return {};
        }
        const auto canonical = path_utf8(path);
        if (const auto found = optional_sound_cache.find(canonical);
            found != optional_sound_cache.end()) {
            return {.handle = found->second, .canonical = canonical, .pending = false};
        }
        if (pending_named_buffers.contains(canonical)) {
            return {.canonical = canonical, .pending = true};
        }
        const SoundHandle handle = next_optional_handle++;
        PendingNamedBuffer pending;
        pending.handle = handle;
        pending.path = path;
        pending.decode = std::async(std::launch::async, [path]() {
            AsyncDecodedAudio result;
            try {
                result.succeeded = read_ogg(path, result.audio, result.error);
            } catch (const std::exception& error) {
                result.error = std::string{"audio decode worker failed: "} + error.what();
            } catch (...) {
                result.error = "audio decode worker failed with an unknown exception";
            }
            return result;
        });
        pending_named_buffers.emplace(canonical, std::move(pending));
        return {.canonical = canonical, .pending = true};
    }

    void pump_async_named_buffers() {
        for (auto iterator = pending_named_buffers.begin();
             iterator != pending_named_buffers.end();) {
            if (iterator->second.decode.wait_for(std::chrono::seconds{0}) !=
                std::future_status::ready) {
                ++iterator;
                continue;
            }
            auto result = iterator->second.decode.get();
            const auto canonical = iterator->first;
            const auto handle = iterator->second.handle;
            const auto path = iterator->second.path;
            bool uploaded{};
            if (result.succeeded) {
                uploaded = upload_buffer(handle, path, std::move(result.audio));
            } else {
                last_error = std::move(result.error);
            }
            if (uploaded) {
                optional_sound_cache.emplace(canonical, handle);
            }
            iterator = pending_named_buffers.erase(iterator);

            if (pending_music.has_value() && pending_music->canonical == canonical) {
                const auto request = *pending_music;
                pending_music.reset();
                if (uploaded) {
                    static_cast<void>(play_music(handle, request.start_offset));
                }
            }
            for (auto loop = pending_server_loops.begin(); loop != pending_server_loops.end();) {
                if (loop->second.canonical != canonical) {
                    ++loop;
                    continue;
                }
                const auto loop_id = loop->first;
                const auto request = loop->second;
                loop = pending_server_loops.erase(loop);
                if (uploaded) {
                    static_cast<void>(start_server_loop(loop_id,
                                                        handle,
                                                        request.position,
                                                        request.gain,
                                                        request.relative,
                                                        request.attenuation,
                                                        request.start_offset));
                }
            }
        }
    }

    void
    apply_start_offset(ALuint source, SoundHandle handle, float requested_offset) const noexcept {
        if (!std::isfinite(requested_offset) || requested_offset <= 0.0F) {
            return;
        }
        const float duration = duration_for(handle);
        if (duration <= 0.0F) {
            return;
        }
        const float offset = std::fmod(requested_offset, duration);
        if (offset > 0.0F) {
            alSourcef(source, AL_SEC_OFFSET, offset);
        }
    }

    [[nodiscard]] std::vector<SoundHandle>
    load_optional_group(std::string_view group, const std::filesystem::path& sound_root) {
        std::vector<SoundHandle> handles;
        for (const auto& stem : sound_group_stems(group)) {
            const auto path = sound_root / (stem + ".ogg");
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error) {
                continue;
            }
            const auto canonical = path_utf8(path.lexically_normal());
            if (const auto found = optional_sound_cache.find(canonical);
                found != optional_sound_cache.end()) {
                handles.push_back(found->second);
                continue;
            }
            const SoundHandle handle = next_optional_handle++;
            if (load_buffer(handle, path)) {
                optional_sound_cache.emplace(canonical, handle);
                handles.push_back(handle);
            }
        }
        return handles;
    }

    [[nodiscard]] bool create_sources() {
        clear_openal_error();
        alGenSources(1, &music_source);
        ALenum error = alGetError();
        if (error != AL_NO_ERROR || music_source == 0U) {
            last_error = "cannot allocate OpenAL music source: " + openal_error_text(error);
            return false;
        }

        alGenSources(1, &fade_source);
        error = alGetError();
        if (error != AL_NO_ERROR || fade_source == 0U) {
            last_error = "cannot allocate OpenAL music fade source: " + openal_error_text(error);
            return false;
        }

        alGenSources(1, &ambience_source);
        error = alGetError();
        if (error != AL_NO_ERROR || ambience_source == 0U) {
            last_error = "cannot allocate OpenAL ambience source: " + openal_error_text(error);
            return false;
        }

        for (auto& slot : loop_slots) {
            alGenSources(1, &slot.source);
            error = alGetError();
            if (error != AL_NO_ERROR || slot.source == 0U) {
                last_error = "cannot allocate OpenAL loop source: " + openal_error_text(error);
                return false;
            }
            slot.active = false;
            slot.voice = invalid_loop_voice;
        }
        for (auto& slot : server_loop_slots) {
            alGenSources(1, &slot.source);
            error = alGetError();
            if (error != AL_NO_ERROR || slot.source == 0U) {
                last_error =
                    "cannot allocate Protocol 168 loop source: " + openal_error_text(error);
                return false;
            }
        }

        voices.reserve(config.max_one_shot_voices);
        for (std::size_t index{}; index < config.max_one_shot_voices; ++index) {
            ALuint source{};
            alGenSources(1, &source);
            error = alGetError();
            if (error != AL_NO_ERROR || source == 0U) {
                last_error =
                    "cannot allocate bounded OpenAL voice pool: " + openal_error_text(error);
                return false;
            }
            voices.push_back(Voice{.source = source});
        }
        return true;
    }

    /**
     * Restore retail's in-world reverb send when OpenAL Soft exposes EFX.
     *
     * EFX
     * is optional on every platform. Failure therefore leaves the dry
     * mixer operational
     * instead of making audio startup (or the game) fail.
     */
    void initialise_world_reverb() noexcept {
        if (device == nullptr || alcIsExtensionPresent(device, "ALC_EXT_EFX") == ALC_FALSE) {
            return;
        }
        gen_effects = reinterpret_cast<LPALGENEFFECTS>(alGetProcAddress("alGenEffects"));
        delete_effects = reinterpret_cast<LPALDELETEEFFECTS>(alGetProcAddress("alDeleteEffects"));
        effect_i = reinterpret_cast<LPALEFFECTI>(alGetProcAddress("alEffecti"));
        effect_f = reinterpret_cast<LPALEFFECTF>(alGetProcAddress("alEffectf"));
        gen_effect_slots = reinterpret_cast<LPALGENAUXILIARYEFFECTSLOTS>(
            alGetProcAddress("alGenAuxiliaryEffectSlots"));
        delete_effect_slots = reinterpret_cast<LPALDELETEAUXILIARYEFFECTSLOTS>(
            alGetProcAddress("alDeleteAuxiliaryEffectSlots"));
        effect_slot_i =
            reinterpret_cast<LPALAUXILIARYEFFECTSLOTI>(alGetProcAddress("alAuxiliaryEffectSloti"));
        if (gen_effects == nullptr || delete_effects == nullptr || effect_i == nullptr ||
            effect_f == nullptr || gen_effect_slots == nullptr || delete_effect_slots == nullptr ||
            effect_slot_i == nullptr) {
            return;
        }

        clear_openal_error();
        gen_effects(1, &world_reverb_effect);
        if (world_reverb_effect == 0U || alGetError() != AL_NO_ERROR) {
            world_reverb_effect = 0U;
            return;
        }
        effect_i(world_reverb_effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
        // MediaManager.set_global_reverb defaults recovered from audio.py.
        effect_f(world_reverb_effect, AL_REVERB_GAIN, 0.32F);
        effect_f(world_reverb_effect, AL_REVERB_DECAY_TIME, 1.49F);
        effect_f(world_reverb_effect, AL_REVERB_GAINHF, 0.89F);
        gen_effect_slots(1, &world_reverb_slot);
        if (world_reverb_slot != 0U) {
            effect_slot_i(
                world_reverb_slot, AL_EFFECTSLOT_EFFECT, static_cast<ALint>(world_reverb_effect));
        }
        if (world_reverb_slot == 0U || alGetError() != AL_NO_ERROR) {
            if (world_reverb_slot != 0U) {
                delete_effect_slots(1, &world_reverb_slot);
                world_reverb_slot = 0U;
            }
            delete_effects(1, &world_reverb_effect);
            world_reverb_effect = 0U;
            clear_openal_error();
        }
    }

    /**
     * Release the optional EFX objects while this instance's context is current.
     *
     * The terrain gain resolver is runtime policy, not an OpenAL resource owner.
     * Keeping teardown here prevents changing/refreshing occlusion from silently
     * deleting the reverb bus used by every positional retail sound.
     */
    void release_world_reverb() noexcept {
        if (world_reverb_slot != 0U && delete_effect_slots != nullptr) {
            delete_effect_slots(1, &world_reverb_slot);
            world_reverb_slot = 0U;
        }
        if (world_reverb_effect != 0U && delete_effects != nullptr) {
            delete_effects(1, &world_reverb_effect);
            world_reverb_effect = 0U;
        }
    }

    /** Attach only positional gameplay sources to the retail reverb bus. */
    void apply_world_reverb(ALuint source, bool relative) const noexcept {
        if (source == 0U || world_reverb_slot == 0U) {
            return;
        }
        alSource3i(source,
                   AL_AUXILIARY_SEND_FILTER,
                   relative ? AL_EFFECTSLOT_NULL : static_cast<ALint>(world_reverb_slot),
                   0,
                   AL_FILTER_NULL);
    }

    [[nodiscard]] bool play_music(SoundHandle track, float start_offset = 0.0F) {
        if (context == nullptr || !on_owner_thread()) {
            last_error = context == nullptr ? "OpenAL frontend audio is not started"
                                            : "OpenAL frontend audio used from non-owner thread";
            return false;
        }

        const ALuint buffer = buffer_for(track);
        if (buffer == 0U || music_source == 0U) {
            last_error = "music buffer or source is unavailable for track " + std::to_string(track);
            return false;
        }

        clear_openal_error();
        // Retail dedupe: a replay of the already-playing track is a no-op.
        if (music_requested && music_track == track) {
            ALint state{};
            alGetSourcei(music_source, AL_SOURCE_STATE, &state);
            if (state == AL_PLAYING) {
                return true;
            }
        }
        // Retail crossfade: the current track keeps playing on a secondary
        // source and fades at the recovered 1/6.5 volume per second.
        if (music_requested && fade_source != 0U) {
            ALint state{};
            alGetSourcei(music_source, AL_SOURCE_STATE, &state);
            if (state == AL_PLAYING) {
                alSourceStop(fade_source);
                alSourcei(fade_source, AL_BUFFER, 0);
                std::swap(music_source, fade_source);
                fade_gain = config.music_gain;
                fade_active = true;
            }
        }
        alSourceStop(music_source);
        alSourcei(music_source, AL_BUFFER, 0);
        alSourcei(music_source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcei(music_source, AL_LOOPING, AL_TRUE);
        alSourcef(music_source, AL_ROLLOFF_FACTOR, 0.0F);
        alSourcef(music_source, AL_GAIN, config.music_gain);
        alSource3f(music_source, AL_POSITION, 0.0F, 0.0F, 0.0F);
        alSourcei(music_source, AL_BUFFER, static_cast<ALint>(buffer));
        apply_start_offset(music_source, track, start_offset);
        alSourcePlay(music_source);
        const ALenum error = alGetError();
        if (error != AL_NO_ERROR) {
            last_error = "cannot start looping music track: " + openal_error_text(error);
            music_requested = false;
            return false;
        }

        music_requested = true;
        music_track = track;
        return true;
    }

    void stop_music() noexcept {
        music_requested = false;
        if (music_source != 0U && context != nullptr) {
            alSourceStop(music_source);
            alSourcei(music_source, AL_BUFFER, 0);
        }
        if (fade_active && fade_source != 0U && context != nullptr) {
            alSourceStop(fade_source);
            alSourcei(fade_source, AL_BUFFER, 0);
        }
        fade_active = false;
    }

    /** Global ambient bed: a dedicated looping non-positional source. */
    [[nodiscard]] bool play_ambience(SoundHandle bed, float gain, float start_offset = 0.0F) {
        if (context == nullptr || !on_owner_thread()) {
            last_error = context == nullptr ? "OpenAL frontend audio is not started"
                                            : "OpenAL frontend audio used from non-owner thread";
            return false;
        }
        const ALuint buffer = buffer_for(bed);
        if (buffer == 0U || ambience_source == 0U) {
            last_error = "ambience buffer or source is unavailable for bed " + std::to_string(bed);
            return false;
        }

        clear_openal_error();
        alSourceStop(ambience_source);
        alSourcei(ambience_source, AL_BUFFER, 0);
        alSourcei(ambience_source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcei(ambience_source, AL_LOOPING, AL_TRUE);
        alSourcef(ambience_source, AL_ROLLOFF_FACTOR, 0.0F);
        // The recovered ambient bed fades in at 0.03 volume per tick.
        ambience_target_gain = std::clamp(gain, 0.0F, 4.0F);
        ambience_gain = 0.0F;
        alSourcef(ambience_source, AL_GAIN, ambience_gain);
        alSource3f(ambience_source, AL_POSITION, 0.0F, 0.0F, 0.0F);
        alSourcei(ambience_source, AL_BUFFER, static_cast<ALint>(buffer));
        apply_start_offset(ambience_source, bed, start_offset);
        alSourcePlay(ambience_source);
        const ALenum error = alGetError();
        if (error != AL_NO_ERROR) {
            last_error = "cannot start looping ambience: " + openal_error_text(error);
            ambience_active = false;
            return false;
        }
        ambience_active = true;
        return true;
    }

    void stop_ambience() noexcept {
        ambience_active = false;
        if (ambience_source != 0U && context != nullptr) {
            alSourceStop(ambience_source);
            alSourcei(ambience_source, AL_BUFFER, 0);
        }
    }

    [[nodiscard]] ServerLoopSlot* find_server_loop(std::uint8_t loop_id) noexcept {
        for (auto& slot : server_loop_slots) {
            if (slot.active && slot.loop_id == loop_id) {
                return &slot;
            }
        }
        return nullptr;
    }

    void refresh_server_loop_gain(ServerLoopSlot& slot) noexcept {
        const bool audible =
            slot.relative || within_retail_hearing_distance(listener_position, slot.position);
        alSourcef(slot.source,
                  AL_GAIN,
                  audible ? resolved_spatial_gain(slot.position, slot.requested_gain, slot.relative)
                          : 0.0F);
    }

    void stop_server_loop(ServerLoopSlot& slot) noexcept {
        if (slot.source != 0U && context != nullptr) {
            alSourceStop(slot.source);
            alSourcei(slot.source, AL_BUFFER, 0);
        }
        slot.active = false;
        slot.loop_id = 0U;
        slot.position = {};
        slot.requested_gain = 0.0F;
        slot.relative = false;
    }

    [[nodiscard]] bool start_server_loop(std::uint8_t loop_id,
                                         SoundHandle handle,
                                         SoundPosition position,
                                         float gain,
                                         bool relative,
                                         float attenuation,
                                         float start_offset) {
        if (context == nullptr || !on_owner_thread()) {
            last_error = context == nullptr ? "OpenAL frontend audio is not started"
                                            : "OpenAL frontend audio used from non-owner thread";
            return false;
        }
        if (!finite_position(position) || !std::isfinite(gain) || !std::isfinite(attenuation) ||
            !std::isfinite(start_offset) || gain < 0.0F || attenuation < 0.0F) {
            last_error = "invalid Protocol 168 loop parameters";
            return false;
        }
        const ALuint buffer = buffer_for(handle);
        if (buffer == 0U) {
            last_error = "Protocol 168 loop buffer is unavailable";
            return false;
        }
        ServerLoopSlot* slot = find_server_loop(loop_id);
        if (slot == nullptr) {
            const auto available =
                std::ranges::find(server_loop_slots, false, &ServerLoopSlot::active);
            if (available == server_loop_slots.end()) {
                last_error = "Protocol 168 loop source pool is full";
                return false;
            }
            slot = &*available;
        } else {
            stop_server_loop(*slot);
        }

        clear_openal_error();
        slot->active = true;
        slot->loop_id = loop_id;
        slot->position = position;
        slot->requested_gain = std::clamp(gain, 0.0F, 4.0F);
        slot->relative = relative;
        alSourceStop(slot->source);
        alSourcei(slot->source, AL_BUFFER, 0);
        alSourcei(slot->source, AL_LOOPING, AL_TRUE);
        alSourcei(slot->source, AL_SOURCE_RELATIVE, relative ? AL_TRUE : AL_FALSE);
        alSourcef(slot->source, AL_PITCH, 1.0F);
        alSourcef(slot->source, AL_ROLLOFF_FACTOR, relative ? 0.0F : attenuation);
        alSourcef(slot->source, AL_REFERENCE_DISTANCE, relative ? 1.0F : retail_reference_distance);
        alSourcef(slot->source, AL_MAX_DISTANCE, retail_hearing_distance);
        alSource3f(slot->source,
                   AL_POSITION,
                   relative ? 0.0F : position.x,
                   relative ? 0.0F : position.y,
                   relative ? 0.0F : position.z);
        alSource3f(slot->source, AL_VELOCITY, 0.0F, 0.0F, 0.0F);
        apply_world_reverb(slot->source, relative);
        alSourcei(slot->source, AL_BUFFER, static_cast<ALint>(buffer));
        refresh_server_loop_gain(*slot);
        apply_start_offset(slot->source, handle, start_offset);
        alSourcePlay(slot->source);
        const ALenum error = alGetError();
        if (error != AL_NO_ERROR) {
            stop_server_loop(*slot);
            last_error = "cannot start Protocol 168 loop: " + openal_error_text(error);
            return false;
        }
        return true;
    }

    void reclaim_finished_voices() noexcept {
        for (Voice& voice : voices) {
            if (!voice.active) {
                continue;
            }
            ALint state{};
            alGetSourcei(voice.source, AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED || state == AL_INITIAL) {
                alSourcei(voice.source, AL_BUFFER, 0);
                voice.active = false;
                voice.protected_voice = false;
            }
        }
    }

    [[nodiscard]] Voice* acquire_voice() noexcept {
        reclaim_finished_voices();
        const auto available = std::find_if(
            voices.begin(), voices.end(), [](const Voice& voice) { return !voice.active; });
        if (available != voices.end()) {
            return &*available;
        }

        // UI feedback must remain immediate under a cue flood. Steal only the
        // oldest one-shot; the dedicated music source is never affected.
        //
        // Protected voices are skipped: a spoken line is long and would always
        // be the oldest, so plain oldest-first stealing silences exactly the
        // sound the player most wanted to hear. If EVERY voice is protected we
        // fall back to stealing the oldest anyway rather than dropping the new
        // cue, because going deaf is worse than clipping one line.
        const auto oldest_unprotected = std::min_element(
            voices.begin(), voices.end(), [](const Voice& left, const Voice& right) {
                if (left.protected_voice != right.protected_voice) {
                    return !left.protected_voice;
                }
                return left.sequence < right.sequence;
            });
        return oldest_unprotected == voices.end() ? nullptr : &*oldest_unprotected;
    }

    void play(SoundHandle handle,
              SoundPosition position,
              float gain,
              bool relative,
              bool protect = false,
              float start_offset = 0.0F,
              float attenuation = retail_default_attenuation,
              SpatialSoundProfile profile = SpatialSoundProfile::ordinary,
              float pitch = 1.0F) {
        if (context == nullptr || !on_owner_thread()) {
            last_error = context == nullptr ? "OpenAL frontend audio is not started"
                                            : "OpenAL frontend audio used from non-owner thread";
            return;
        }
        if (!finite_position(position) || !std::isfinite(gain) || !std::isfinite(start_offset) ||
            !std::isfinite(attenuation) || !std::isfinite(pitch) || gain < 0.0F ||
            attenuation < 0.0F || pitch <= 0.0F) {
            last_error = "invalid OpenAL one-shot position or gain";
            return;
        }
        // Retail rejects the cue before allocating a player. AL_MAX_DISTANCE
        // only clamps attenuation and otherwise leaks every distant dig/build
        // event into the listener at a permanent low volume.
        if (!relative && !within_retail_hearing_distance(listener_position, position)) {
            return;
        }

        const ALuint buffer = buffer_for(handle);
        if (buffer == 0U || handle == OpenAlFrontendAudio::main_menu_music ||
            handle == OpenAlFrontendAudio::tutorial_music ||
            handle == OpenAlFrontendAudio::training_ambience) {
            last_error = "unknown or non-one-shot frontend sound handle " + std::to_string(handle);
            return;
        }

        Voice* const voice = acquire_voice();
        if (voice == nullptr) {
            last_error = "OpenAL one-shot voice pool is empty";
            return;
        }

        clear_openal_error();
        voice->protected_voice = protect;
        alSourceStop(voice->source);
        alSourcei(voice->source, AL_BUFFER, 0);
        alSourcei(voice->source, AL_LOOPING, AL_FALSE);
        alSourcei(voice->source, AL_SOURCE_RELATIVE, relative ? AL_TRUE : AL_FALSE);
        alSourcef(voice->source, AL_PITCH, pitch);
        alSourcef(voice->source, AL_GAIN, resolved_spatial_gain(position, gain, relative, profile));
        alSourcef(voice->source, AL_ROLLOFF_FACTOR, relative ? 0.0F : attenuation);
        // Retail drops a world cue entirely past HEARING_DISTANCE. Leaving
        // these at the OpenAL defaults (reference 1, max FLT_MAX) attenuates
        // everything by 1/distance from one unit out, which is far too sharp.
        alSourcef(
            voice->source, AL_REFERENCE_DISTANCE, relative ? 1.0F : retail_reference_distance);
        alSourcef(voice->source, AL_MAX_DISTANCE, retail_hearing_distance);
        alSource3f(voice->source,
                   AL_POSITION,
                   relative ? 0.0F : position.x,
                   relative ? 0.0F : position.y,
                   relative ? 0.0F : position.z);
        alSource3f(voice->source, AL_VELOCITY, 0.0F, 0.0F, 0.0F);
        apply_world_reverb(voice->source, relative);
        alSourcei(voice->source, AL_BUFFER, static_cast<ALint>(buffer));
        apply_start_offset(voice->source, handle, start_offset);
        alSourcePlay(voice->source);
        const ALenum error = alGetError();
        if (error != AL_NO_ERROR) {
            voice->active = false;
            last_error = "cannot play OpenAL one-shot: " + openal_error_text(error);
            return;
        }

        ++next_sequence;
        voice->sequence = next_sequence;
        voice->active = true;
    }

    OpenAlFrontendAudioConfig config;
    ALCdevice* device{};
    ALCcontext* context{};
    ALuint music_source{};
    ALuint fade_source{};
    ALuint ambience_source{};
    ALuint world_reverb_effect{};
    ALuint world_reverb_slot{};
    LPALGENEFFECTS gen_effects{};
    LPALDELETEEFFECTS delete_effects{};
    LPALEFFECTI effect_i{};
    LPALEFFECTF effect_f{};
    LPALGENAUXILIARYEFFECTSLOTS gen_effect_slots{};
    LPALDELETEAUXILIARYEFFECTSLOTS delete_effect_slots{};
    LPALAUXILIARYEFFECTSLOTI effect_slot_i{};
    float fade_gain{};
    bool fade_active{};
    float ambience_gain{};
    float ambience_target_gain{};
    bool ambience_active{};
    std::vector<Buffer> buffers;
    std::vector<Voice> voices;
    std::string last_error;
    std::thread::id owner_thread{};
    std::uint64_t next_sequence{};
    std::uint64_t next_pitch_sequence{};
    SoundPosition listener_position{};
    void* spatial_gain_context{};
    SpatialGainResolver spatial_gain_resolver{};
    float master_volume{1.0F};
    bool music_requested{};
    SoundHandle music_track{OpenAlFrontendAudio::main_menu_music};
    static constexpr std::size_t tool_count{65U};
    std::array<std::vector<SoundHandle>, tool_count> weapon_shoot_groups;
    std::array<std::vector<SoundHandle>, tool_count> weapon_reload_groups;
    std::array<std::vector<SoundHandle>, tool_count> weapon_reload_done_groups;
    using CueGroups =
        std::array<std::vector<SoundHandle>, static_cast<std::size_t>(WeaponCue::count)>;
    std::array<CueGroups, tool_count> weapon_sound_sets;
    std::map<std::string, SoundHandle, std::less<>> optional_sound_cache;
    std::map<std::string, PendingNamedBuffer, std::less<>> pending_named_buffers;
    std::optional<PendingMusicRequest> pending_music;
    std::map<std::uint8_t, PendingServerLoopRequest> pending_server_loops;
    SoundHandle next_optional_handle{1'000U};

    /**
     * Sustained loop sources, held apart from the one-shot pool.
     *
     * Four is enough for every simultaneous retail loop a single player can
     * drive (held trigger, minigun spin, a tool loop and one projectile).
     */
    // Automatic weapons, minigun spin and nearby replicated jetpacks can all
    // coexist. Four sources made later effects silently disappear in a normal
    // 12-player match; this remains a fixed, bounded pool.
    static constexpr std::size_t loop_source_count{24U};
    struct LoopSlot final {
        ALuint source{};
        LoopVoice voice{};
        SoundPosition position{};
        float requested_gain{};
        SpatialSoundProfile profile{SpatialSoundProfile::ordinary};
        bool relative{};
        bool active{};
    };
    std::array<LoopSlot, loop_source_count> loop_slots{};
    LoopVoice next_loop_voice{1U};

    /**
     * Packet-owned loops are isolated from weapon loops.
     *
     * A map may carry a
     * global bed plus several local emitters while the local
     * player simultaneously owns a
     * firing and spin loop. Sharing four weapon
     * sources made ambience disappear under combat
     * load.
     */
    static constexpr std::size_t server_loop_source_count{16U};
    std::array<ServerLoopSlot, server_loop_source_count> server_loop_slots{};

    void refresh_weapon_loop_gain(LoopSlot& slot) noexcept {
        if (!slot.active) {
            return;
        }
        const bool audible =
            slot.relative || within_retail_hearing_distance(listener_position, slot.position);
        alSourcef(slot.source,
                  AL_GAIN,
                  audible ? resolved_spatial_gain(
                                slot.position, slot.requested_gain, slot.relative, slot.profile)
                          : 0.0F);
    }

    [[nodiscard]] LoopSlot* find_loop(LoopVoice voice) noexcept {
        if (voice == invalid_loop_voice) {
            return nullptr;
        }
        for (auto& slot : loop_slots) {
            if (slot.active && slot.voice == voice) {
                return &slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const std::vector<SoundHandle>* cue_group(std::uint8_t tool_id,
                                                            WeaponCue cue) const noexcept {
        const auto index = static_cast<std::size_t>(cue);
        if (tool_id >= weapon_sound_sets.size() ||
            index >= static_cast<std::size_t>(WeaponCue::count)) {
            return nullptr;
        }
        const auto& group = weapon_sound_sets[tool_id][index];
        return group.empty() ? nullptr : &group;
    }
};

bool valid_openal_frontend_audio_config(const OpenAlFrontendAudioConfig& config) noexcept {
    constexpr std::size_t maximum_voice_count{64U};
    return !config.asset_root.empty() && config.max_one_shot_voices > 0U &&
           config.max_one_shot_voices <= maximum_voice_count && std::isfinite(config.music_gain) &&
           config.music_gain >= 0.0F && config.music_gain <= 4.0F &&
           std::isfinite(config.cue_gain) && config.cue_gain >= 0.0F && config.cue_gain <= 4.0F;
}

OpenAlFrontendAudio::OpenAlFrontendAudio(OpenAlFrontendAudioConfig config)
    : impl_{std::make_unique<Impl>(std::move(config))} {}

OpenAlFrontendAudio::~OpenAlFrontendAudio() {
    stop();
}

std::string_view OpenAlFrontendAudio::name() const noexcept {
    return "openal-frontend-audio";
}

bool OpenAlFrontendAudio::start() {
    if (impl_->device != nullptr || impl_->context != nullptr) {
        impl_->last_error = "OpenAL frontend audio is already started";
        return false;
    }
    if (!valid_openal_frontend_audio_config(impl_->config)) {
        impl_->last_error = "invalid OpenAL frontend audio configuration";
        return false;
    }
    if (alcGetCurrentContext() != nullptr) {
        impl_->last_error = "another OpenAL context is already current on the main thread";
        return false;
    }

    try {
        impl_->last_error.clear();
        impl_->owner_thread = std::this_thread::get_id();
        impl_->device = alcOpenDevice(nullptr);
        if (impl_->device == nullptr) {
            impl_->last_error = "OpenAL Soft could not open the default playback device";
            impl_->owner_thread = {};
            return false;
        }

        impl_->context = alcCreateContext(impl_->device, nullptr);
        if (impl_->context == nullptr) {
            impl_->last_error = "OpenAL Soft could not create a playback context";
            stop();
            return false;
        }
        if (alcMakeContextCurrent(impl_->context) == ALC_FALSE) {
            impl_->last_error = "OpenAL Soft could not make its playback context current";
            stop();
            return false;
        }

        clear_openal_error();
        alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
        alListener3f(AL_POSITION, 0.0F, 0.0F, 0.0F);
        alListener3f(AL_VELOCITY, 0.0F, 0.0F, 0.0F);
        constexpr std::array<ALfloat, 6U> initial_orientation{-1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F};
        alListenerfv(AL_ORIENTATION, initial_orientation.data());
        alListenerf(AL_GAIN, impl_->master_volume);
        const ALenum listener_error = alGetError();
        if (listener_error != AL_NO_ERROR) {
            impl_->last_error =
                "cannot configure OpenAL listener: " + openal_error_text(listener_error);
            stop();
            return false;
        }

        // Reserving all fixed caches before creating OpenAL objects ensures an
        // allocation exception cannot orphan a generated buffer or source.
        impl_->buffers.reserve(512U);
        const auto& root = impl_->config.asset_root;
        if (!impl_->load_buffer(main_menu_music, root / "music" / "mainmenu.ogg") ||
            !impl_->load_buffer(menu_confirm_sound, root / "sounds" / "menu_confirmA.ogg") ||
            !impl_->load_buffer(menu_back_sound, root / "sounds" / "menu_backA.ogg") ||
            !impl_->load_buffer(menu_scroll_sound, root / "sounds" / "menu_scrollA.ogg") ||
            !impl_->load_buffer(tutorial_appear_sound, root / "sounds" / "tutorial_app.ogg") ||
            !impl_->load_buffer(tutorial_disappear_sound,
                                root / "sounds" / "tutorial_disapp.ogg") ||
            !impl_->load_buffer(tutorial_music, root / "music" / "tutorial_music_001.ogg") ||
            !impl_->load_buffer(training_ambience, root / "ambients" / "amb_rural.ogg") ||
            !impl_->load_buffer(pistol_shoot_sound, root / "sounds" / "pistolshoot.ogg") ||
            !impl_->load_buffer(pistol_reload_sound, root / "sounds" / "pistolreload.ogg") ||
            !impl_->load_buffer(dig_hit_sound, root / "sounds" / "hitground.ogg") ||
            !impl_->load_buffer(block_build_sound, root / "sounds" / "build.ogg") ||
            !impl_->load_buffer(tool_switch_sound, root / "sounds" / "switch.ogg") ||
            !impl_->load_buffer(dig_swing_sound, root / "sounds" / "woosh.ogg") ||
            !impl_->load_buffer(bullet_hit_sound_1, root / "sounds" / "bullet_hit_001.ogg") ||
            !impl_->load_buffer(bullet_hit_sound_2, root / "sounds" / "bullet_hit_002.ogg") ||
            !impl_->load_buffer(bullet_hit_sound_3, root / "sounds" / "bullet_hit_003.ogg") ||
            !impl_->load_buffer(bullet_hit_sound_4, root / "sounds" / "bullet_hit_004.ogg") ||
            !impl_->load_buffer(block_debris_sound, root / "sounds" / "block_debris.ogg") ||
            !impl_->load_buffer(pickaxe_hit_sound, root / "sounds" / "hitground_pickaxe.ogg") ||
            !impl_->load_buffer(super_spade_hit_sound, root / "sounds" / "hitground_super.ogg") ||
            !impl_->load_buffer(zombie_hit_sound, root / "sounds" / "hitground_zombie.ogg") ||
            !impl_->load_buffer(crowbar_damage_sound,
                                root / "sounds" / "hitground_crowbar_damage.ogg") ||
            !impl_->load_buffer(crowbar_break_sound,
                                root / "sounds" / "hitground_crowbar_break.ogg") ||
            !impl_->load_buffer(knife_damage_sound,
                                root / "sounds" / "hitground_knife_damage.ogg") ||
            !impl_->load_buffer(knife_break_sound, root / "sounds" / "hitground_knife_break.ogg") ||
            !impl_->load_buffer(machete_damage_sound,
                                root / "sounds" / "hitground_machete_damage.ogg") ||
            !impl_->load_buffer(machete_break_sound,
                                root / "sounds" / "hitground_machete_break.ogg") ||
            !impl_->load_buffer(riotstick_damage_sound,
                                root / "sounds" / "hitground_riotstick_damage.ogg") ||
            !impl_->load_buffer(riotstick_break_sound,
                                root / "sounds" / "hitground_riotstick_break.ogg") ||
            !impl_->load_buffer(riotshield_damage_sound,
                                root / "sounds" / "hitground_riotshield_damage.ogg") ||
            !impl_->load_buffer(riotshield_break_sound,
                                root / "sounds" / "hitground_riotshield_break.ogg") ||
            !impl_->load_buffer(explosion_sound, root / "sounds" / "explode.ogg") ||
            !impl_->load_buffer(grenade_pin_sound, root / "sounds" / "pin.ogg") ||
            !impl_->load_buffer(molotov_throw_sound, root / "sounds" / "molotov_throw.ogg") ||
            !impl_->load_buffer(molotov_impact_sound,
                                root / "sounds" / "molotov_land_explode.ogg") ||
            // Movement foley. Every stem below was confirmed present on disk.
            !impl_->load_buffer(footstep_sound_1, root / "sounds" / "footstep_001.ogg") ||
            !impl_->load_buffer(footstep_sound_2, root / "sounds" / "footstep_002.ogg") ||
            !impl_->load_buffer(footstep_sound_3, root / "sounds" / "footstep_003.ogg") ||
            !impl_->load_buffer(footstep_sound_4, root / "sounds" / "footstep_004.ogg") ||
            !impl_->load_buffer(wade_sound_1, root / "sounds" / "wade_001.ogg") ||
            !impl_->load_buffer(wade_sound_2, root / "sounds" / "wade_002.ogg") ||
            !impl_->load_buffer(wade_sound_3, root / "sounds" / "wade_003.ogg") ||
            !impl_->load_buffer(wade_sound_4, root / "sounds" / "wade_004.ogg") ||
            !impl_->load_buffer(jump_sound, root / "sounds" / "jump.ogg") ||
            !impl_->load_buffer(water_jump_sound, root / "sounds" / "waterjump.ogg") ||
            !impl_->load_buffer(land_sound, root / "sounds" / "land.ogg") ||
            !impl_->load_buffer(water_land_sound, root / "sounds" / "waterland.ogg") ||
            !impl_->load_buffer(fall_hurt_sound, root / "sounds" / "fallhurt.ogg") ||
            !impl_->load_buffer(zoom_in_sound, root / "sounds" / "zoom_in.ogg") ||
            !impl_->load_buffer(zoom_out_sound, root / "sounds" / "zoom_out.ogg") ||
            !impl_->load_buffer(respawn_beep1_sound, root / "sounds" / "beep1.ogg") ||
            !impl_->load_buffer(respawn_beep2_sound, root / "sounds" / "beep2.ogg") ||
            !impl_->create_sources()) {
            stop();
            return false;
        }
        impl_->initialise_world_reverb();

        // The generated 65-tool table is the common audio contract for the
        // debug lab and future Protocol 168 sessions. Optional groups are
        // predecoded here so a shot never performs gameplay-thread file I/O.
        for (const auto& weapon : world::weapon_catalog()) {
            impl_->weapon_shoot_groups[weapon.tool_id] =
                impl_->load_optional_group(weapon.shoot_sound, root / "sounds");
            impl_->weapon_reload_groups[weapon.tool_id] =
                impl_->load_optional_group(weapon.reload_sound, root / "sounds");
            impl_->weapon_reload_done_groups[weapon.tool_id] =
                impl_->load_optional_group(weapon.reload_done_sound, root / "sounds");
            // Retail keeps these in method bodies rather than class
            // attributes; the generator recovers them into WeaponSoundSet.
            // An empty role means retail is genuinely silent there.
            auto& set = impl_->weapon_sound_sets[weapon.tool_id];
            const auto bind = [&](WeaponCue cue, std::string_view group) {
                set[static_cast<std::size_t>(cue)] =
                    impl_->load_optional_group(group, root / "sounds");
            };
            bind(WeaponCue::fire_loop, weapon.sounds.fire_loop);
            bind(WeaponCue::fire_tail, weapon.sounds.fire_tail);
            bind(WeaponCue::spin_loop, weapon.sounds.spin_loop);
            bind(WeaponCue::melee_miss, weapon.sounds.melee_miss);
            bind(WeaponCue::melee_hit_block, weapon.sounds.melee_hit_block);
            bind(WeaponCue::melee_hit_player, weapon.sounds.melee_hit_player);
            bind(WeaponCue::empty_fire, weapon.sounds.empty_fire);
            bind(WeaponCue::pin, weapon.sounds.pin);
            bind(WeaponCue::throw_release, weapon.sounds.throw_release);
            bind(WeaponCue::tool_loop_start, weapon.sounds.tool_loop_start);
            bind(WeaponCue::tool_loop, weapon.sounds.tool_loop);
            bind(WeaponCue::tool_loop_stop, weapon.sounds.tool_loop_stop);
            bind(WeaponCue::tool_extra, weapon.sounds.tool_extra);
        }

        if (impl_->config.play_music_on_start && !impl_->play_music(main_menu_music)) {
            stop();
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        try {
            impl_->last_error =
                "OpenAL frontend startup exception: " + std::string{exception.what()};
        } catch (...) {
        }
        stop();
        return false;
    } catch (...) {
        try {
            impl_->last_error = "unknown OpenAL frontend startup exception";
        } catch (...) {
        }
        stop();
        return false;
    }
}

void OpenAlFrontendAudio::play_bullet_impact(std::uint8_t variant,
                                             SoundPosition position,
                                             float gain) {
    constexpr std::array<SoundHandle, 4U> samples{
        bullet_hit_sound_1, bullet_hit_sound_2, bullet_hit_sound_3, bullet_hit_sound_4};
    play_one_shot(samples[variant % samples.size()], position, gain);
}

void OpenAlFrontendAudio::play_weapon_shoot(std::uint8_t tool_id,
                                            std::uint8_t variant,
                                            SoundPosition position,
                                            float gain,
                                            bool head_relative,
                                            SpatialSoundProfile profile) {
    if (tool_id >= impl_->weapon_shoot_groups.size()) {
        return;
    }
    const auto& group = impl_->weapon_shoot_groups[tool_id];
    if (!group.empty()) {
        const auto* weapon = world::find_weapon_definition(tool_id);
        const auto pitch_bounds =
            weapon == nullptr ? std::array<float, 2U>{} : weapon->shoot_sound_pitch;
        ++impl_->next_pitch_sequence;
        const auto pitch_draw = static_cast<std::uint32_t>(
            (impl_->next_pitch_sequence * 0x9E3779B97F4A7C15ULL) ^
            (static_cast<std::uint64_t>(tool_id) << 16U) ^ variant);
        impl_->play(group[variant % group.size()],
                    head_relative ? SoundPosition{} : position,
                    gain,
                    head_relative,
                    false,
                    0.0F,
                    spatial_rolloff(profile),
                    profile,
                    retail_sound_pitch_ratio(pitch_bounds[0U], pitch_bounds[1U], pitch_draw));
    }
}

void OpenAlFrontendAudio::play_weapon_reload(std::uint8_t tool_id,
                                             std::uint8_t variant,
                                             SoundPosition position,
                                             float gain,
                                             bool head_relative) {
    if (tool_id >= impl_->weapon_reload_groups.size()) {
        return;
    }
    const auto& group = impl_->weapon_reload_groups[tool_id];
    if (!group.empty()) {
        const auto* weapon = world::find_weapon_definition(tool_id);
        const auto pitch_bounds =
            weapon == nullptr ? std::array<float, 2U>{} : weapon->reload_sound_pitch;
        ++impl_->next_pitch_sequence;
        const auto pitch_draw = static_cast<std::uint32_t>(
            (impl_->next_pitch_sequence * 0x9E3779B97F4A7C15ULL) ^
            (static_cast<std::uint64_t>(tool_id) << 16U) ^ variant);
        impl_->play(group[variant % group.size()],
                    head_relative ? SoundPosition{} : position,
                    gain,
                    head_relative,
                    false,
                    0.0F,
                    retail_default_attenuation,
                    SpatialSoundProfile::ordinary,
                    retail_sound_pitch_ratio(pitch_bounds[0U], pitch_bounds[1U], pitch_draw));
    }
}

void OpenAlFrontendAudio::play_weapon_reload_done(std::uint8_t tool_id,
                                                  std::uint8_t variant,
                                                  SoundPosition position,
                                                  float gain,
                                                  bool head_relative) {
    if (tool_id >= impl_->weapon_reload_done_groups.size()) {
        return;
    }
    const auto& group = impl_->weapon_reload_done_groups[tool_id];
    if (!group.empty()) {
        const auto* weapon = world::find_weapon_definition(tool_id);
        const auto pitch_bounds =
            weapon == nullptr ? std::array<float, 2U>{} : weapon->reload_done_sound_pitch;
        ++impl_->next_pitch_sequence;
        const auto pitch_draw = static_cast<std::uint32_t>(
            (impl_->next_pitch_sequence * 0x9E3779B97F4A7C15ULL) ^
            (static_cast<std::uint64_t>(tool_id) << 16U) ^ variant);
        impl_->play(group[variant % group.size()],
                    head_relative ? SoundPosition{} : position,
                    gain,
                    head_relative,
                    false,
                    0.0F,
                    retail_default_attenuation,
                    SpatialSoundProfile::ordinary,
                    retail_sound_pitch_ratio(pitch_bounds[0U], pitch_bounds[1U], pitch_draw));
    }
}

void OpenAlFrontendAudio::play_weapon_cue(std::uint8_t tool_id,
                                          WeaponCue cue,
                                          std::uint8_t variant,
                                          SoundPosition position,
                                          float gain,
                                          bool head_relative,
                                          SpatialSoundProfile profile) {
    const auto* group = impl_->cue_group(tool_id, cue);
    if (group == nullptr) {
        // Retail is deliberately silent in this role for this tool.
        return;
    }
    impl_->play((*group)[variant % group->size()],
                head_relative ? SoundPosition{} : position,
                gain,
                head_relative,
                false,
                0.0F,
                spatial_rolloff(profile),
                profile);
}

bool OpenAlFrontendAudio::has_weapon_cue(std::uint8_t tool_id, WeaponCue cue) const noexcept {
    return impl_->cue_group(tool_id, cue) != nullptr;
}

LoopVoice OpenAlFrontendAudio::start_weapon_loop(std::uint8_t tool_id,
                                                 WeaponCue cue,
                                                 SoundPosition position,
                                                 float gain,
                                                 bool head_relative,
                                                 SpatialSoundProfile profile) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return invalid_loop_voice;
    }
    const auto* group = impl_->cue_group(tool_id, cue);
    if (group == nullptr) {
        return invalid_loop_voice;
    }
    const ALuint buffer = impl_->buffer_for(group->front());
    if (buffer == 0U) {
        return invalid_loop_voice;
    }
    const auto found =
        std::ranges::find_if(impl_->loop_slots, [](const auto& entry) { return !entry.active; });
    if (found == impl_->loop_slots.end()) {
        impl_->last_error = "OpenAL loop source pool is empty";
        return invalid_loop_voice;
    }
    auto* const slot = &*found;

    clear_openal_error();
    alSourceStop(slot->source);
    alSourcei(slot->source, AL_BUFFER, 0);
    alSourcei(slot->source, AL_LOOPING, AL_TRUE);
    alSourcei(slot->source, AL_SOURCE_RELATIVE, head_relative ? AL_TRUE : AL_FALSE);
    alSourcef(slot->source, AL_PITCH, 1.0F);
    alSourcef(slot->source, AL_ROLLOFF_FACTOR, head_relative ? 0.0F : spatial_rolloff(profile));
    alSourcef(slot->source,
              AL_GAIN,
              impl_->resolved_spatial_gain(position, gain, head_relative, profile));
    alSourcef(slot->source, AL_REFERENCE_DISTANCE, retail_reference_distance);
    alSourcef(slot->source, AL_MAX_DISTANCE, retail_hearing_distance);
    const auto source_position = head_relative ? SoundPosition{} : position;
    alSource3f(slot->source, AL_POSITION, source_position.x, source_position.y, source_position.z);
    impl_->apply_world_reverb(slot->source, head_relative);
    alSourcei(slot->source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcePlay(slot->source);
    if (alGetError() != AL_NO_ERROR) {
        return invalid_loop_voice;
    }
    slot->active = true;
    slot->voice = impl_->next_loop_voice++;
    slot->position = position;
    slot->requested_gain = std::clamp(gain, 0.0F, 4.0F);
    slot->profile = profile;
    slot->relative = head_relative;
    return slot->voice;
}

LoopVoice OpenAlFrontendAudio::start_named_voice_loop(std::string_view stem,
                                                      SoundPosition position,
                                                      float gain,
                                                      bool head_relative,
                                                      SpatialSoundProfile profile) {
    if (impl_->context == nullptr || !impl_->on_owner_thread() || stem.empty() ||
        !finite_position(position)) {
        return invalid_loop_voice;
    }
    const SoundHandle handle = impl_->load_named_buffer("sounds", stem);
    const ALuint buffer = impl_->buffer_for(handle);
    if (buffer == 0U) {
        return invalid_loop_voice;
    }
    const auto found =
        std::ranges::find_if(impl_->loop_slots, [](const auto& entry) { return !entry.active; });
    if (found == impl_->loop_slots.end()) {
        impl_->last_error = "OpenAL loop source pool is empty";
        return invalid_loop_voice;
    }
    auto* const slot = &*found;

    clear_openal_error();
    alSourceStop(slot->source);
    alSourcei(slot->source, AL_BUFFER, 0);
    alSourcei(slot->source, AL_LOOPING, AL_TRUE);
    alSourcei(slot->source, AL_SOURCE_RELATIVE, head_relative ? AL_TRUE : AL_FALSE);
    alSourcef(slot->source, AL_PITCH, 1.0F);
    alSourcef(slot->source, AL_ROLLOFF_FACTOR, head_relative ? 0.0F : spatial_rolloff(profile));
    alSourcef(slot->source,
              AL_GAIN,
              impl_->resolved_spatial_gain(position, gain, head_relative, profile));
    alSourcef(slot->source, AL_REFERENCE_DISTANCE, retail_reference_distance);
    alSourcef(slot->source, AL_MAX_DISTANCE, retail_hearing_distance);
    const auto source_position = head_relative ? SoundPosition{} : position;
    alSource3f(slot->source, AL_POSITION, source_position.x, source_position.y, source_position.z);
    impl_->apply_world_reverb(slot->source, head_relative);
    alSourcei(slot->source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcePlay(slot->source);
    if (alGetError() != AL_NO_ERROR) {
        return invalid_loop_voice;
    }
    slot->active = true;
    slot->voice = impl_->next_loop_voice++;
    slot->position = position;
    slot->requested_gain = std::clamp(gain, 0.0F, 4.0F);
    slot->profile = profile;
    slot->relative = head_relative;
    return slot->voice;
}

void OpenAlFrontendAudio::update_weapon_loop(LoopVoice voice,
                                             SoundPosition position,
                                             float pitch,
                                             float gain) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return;
    }
    auto* slot = impl_->find_loop(voice);
    if (slot == nullptr) {
        return;
    }
    slot->position = position;
    slot->requested_gain = std::clamp(gain, 0.0F, 4.0F);
    if (!slot->relative) {
        alSource3f(slot->source, AL_POSITION, position.x, position.y, position.z);
    }
    // The floor is 0.01, not 0.25: the minigun's spin loop is pitched by its
    // barrel speed as a raw playback ratio, so a 0.25 floor would clip the
    // entire lower half of the spin-up -- exactly the build-up that makes the
    // weapon sound like it is winding on. It must stay strictly positive, since
    // OpenAL rejects a pitch of zero with AL_INVALID_VALUE and silently keeps
    // whatever pitch the source had.
    alSourcef(slot->source, AL_PITCH, std::clamp(pitch, 0.01F, 4.0F));
    impl_->refresh_weapon_loop_gain(*slot);
}

void OpenAlFrontendAudio::stop_weapon_loop(LoopVoice voice,
                                           std::uint8_t tool_id,
                                           WeaponCue tail,
                                           SoundPosition position,
                                           float gain,
                                           bool head_relative,
                                           SpatialSoundProfile profile) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return;
    }
    if (auto* slot = impl_->find_loop(voice); slot != nullptr) {
        alSourceStop(slot->source);
        alSourcei(slot->source, AL_BUFFER, 0);
        slot->active = false;
        slot->voice = invalid_loop_voice;
        slot->position = {};
        slot->requested_gain = 0.0F;
        slot->profile = SpatialSoundProfile::ordinary;
        slot->relative = false;
    }
    // Retail closes the loop and immediately plays the tail as a one-shot.
    play_weapon_cue(tool_id, tail, 0U, position, gain, head_relative, profile);
}

void OpenAlFrontendAudio::stop_weapon_loop(LoopVoice voice) noexcept {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return;
    }
    auto* slot = impl_->find_loop(voice);
    if (slot == nullptr) {
        return;
    }
    alSourceStop(slot->source);
    alSourcei(slot->source, AL_BUFFER, 0);
    slot->active = false;
    slot->voice = invalid_loop_voice;
    slot->position = {};
    slot->requested_gain = 0.0F;
    slot->profile = SpatialSoundProfile::ordinary;
    slot->relative = false;
}

std::size_t OpenAlFrontendAudio::active_loop_voices() const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(impl_->loop_slots, [](const auto& slot) { return slot.active; }));
}

core::TickDecision OpenAlFrontendAudio::tick(const core::TickContext& context) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        impl_->last_error = impl_->context == nullptr
                                ? "OpenAL frontend audio is not started"
                                : "OpenAL frontend audio ticked from non-owner thread";
        return core::TickDecision::stop;
    }

    clear_openal_error();
    impl_->pump_async_named_buffers();
    impl_->reclaim_finished_voices();
    if (impl_->music_requested) {
        ALint state{};
        alGetSourcei(impl_->music_source, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING && !impl_->play_music(impl_->music_track)) {
            return core::TickDecision::continue_running;
        }
    }

    // Recovered fade rates: outgoing music loses 1/6.5 volume per second,
    // the ambient bed gains 0.03 per 60 Hz tick toward its target.
    if (impl_->fade_active && impl_->fade_source != 0U) {
        impl_->fade_gain -=
            static_cast<float>(std::chrono::duration<double>{context.fixed_delta}.count()) / 6.5F;
        if (impl_->fade_gain <= 0.0F) {
            alSourceStop(impl_->fade_source);
            alSourcei(impl_->fade_source, AL_BUFFER, 0);
            impl_->fade_active = false;
        } else {
            alSourcef(impl_->fade_source, AL_GAIN, impl_->fade_gain);
        }
    }
    if (impl_->ambience_active && impl_->ambience_gain < impl_->ambience_target_gain) {
        impl_->ambience_gain = std::min(impl_->ambience_target_gain, impl_->ambience_gain + 0.03F);
        alSourcef(impl_->ambience_source, AL_GAIN, impl_->ambience_gain);
    }

    const ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        impl_->last_error = "OpenAL frontend maintenance failed: " + openal_error_text(error);
    }
    return core::TickDecision::continue_running;
}

void OpenAlFrontendAudio::stop() noexcept {
    if (impl_ == nullptr) {
        return;
    }

    ALCcontext* displaced_context{};
    if (impl_->context != nullptr && alcGetCurrentContext() != impl_->context) {
        displaced_context = alcGetCurrentContext();
        static_cast<void>(alcMakeContextCurrent(impl_->context));
    }

    if (impl_->context != nullptr) {
        impl_->stop_music();
        for (auto& voice : impl_->voices) {
            if (voice.source != 0U) {
                alSourceStop(voice.source);
                alSourcei(voice.source, AL_BUFFER, 0);
                alDeleteSources(1, &voice.source);
                voice.source = 0U;
                voice.active = false;
                voice.protected_voice = false;
            }
        }
        if (impl_->music_source != 0U) {
            alDeleteSources(1, &impl_->music_source);
            impl_->music_source = 0U;
        }
        if (impl_->fade_source != 0U) {
            alDeleteSources(1, &impl_->fade_source);
            impl_->fade_source = 0U;
            impl_->fade_active = false;
        }
        if (impl_->ambience_source != 0U) {
            impl_->stop_ambience();
            alDeleteSources(1, &impl_->ambience_source);
            impl_->ambience_source = 0U;
        }
        for (auto& slot : impl_->loop_slots) {
            if (slot.source != 0U) {
                alSourceStop(slot.source);
                alSourcei(slot.source, AL_BUFFER, 0);
                alDeleteSources(1, &slot.source);
                slot.source = 0U;
            }
            slot.active = false;
            slot.voice = invalid_loop_voice;
        }
        for (auto& slot : impl_->server_loop_slots) {
            if (slot.source != 0U) {
                impl_->stop_server_loop(slot);
                alDeleteSources(1, &slot.source);
                slot.source = 0U;
            }
        }
        // EFX objects belong to the OpenAL context and must be destroyed before
        // the context. Resolver installation/removal must never touch them.
        impl_->release_world_reverb();
        for (auto& buffer : impl_->buffers) {
            if (buffer.id != 0U) {
                alDeleteBuffers(1, &buffer.id);
                buffer.id = 0U;
            }
        }
    }

    impl_->voices.clear();
    impl_->buffers.clear();
    impl_->next_sequence = 0U;
    impl_->next_pitch_sequence = 0U;
    impl_->listener_position = {};
    impl_->music_requested = false;

    // Every buffer these name has just been deleted. Leaving them populated
    // makes a restart hand out dangling handles and reuse stale ids, so the
    // whole optional-sound bookkeeping resets with the buffers it describes.
    impl_->optional_sound_cache.clear();
    impl_->next_optional_handle = 1'000U;
    impl_->next_loop_voice = 1U;
    for (auto& group : impl_->weapon_shoot_groups)
        group.clear();
    for (auto& group : impl_->weapon_reload_groups)
        group.clear();
    for (auto& group : impl_->weapon_reload_done_groups)
        group.clear();
    for (auto& set : impl_->weapon_sound_sets) {
        for (auto& group : set)
            group.clear();
    }

    if (impl_->context != nullptr) {
        if (alcGetCurrentContext() == impl_->context) {
            static_cast<void>(alcMakeContextCurrent(nullptr));
        }
        alcDestroyContext(impl_->context);
        impl_->context = nullptr;
    }
    if (impl_->device != nullptr) {
        static_cast<void>(alcCloseDevice(impl_->device));
        impl_->device = nullptr;
    }
    if (displaced_context != nullptr) {
        static_cast<void>(alcMakeContextCurrent(displaced_context));
    }
    impl_->owner_thread = {};
}

void OpenAlFrontendAudio::set_listener(SoundPosition position) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        impl_->last_error = impl_->context == nullptr
                                ? "OpenAL frontend audio is not started"
                                : "OpenAL listener changed from non-owner thread";
        return;
    }
    if (!finite_position(position)) {
        impl_->last_error = "invalid OpenAL listener position";
        return;
    }

    clear_openal_error();
    alListener3f(AL_POSITION, position.x, position.y, position.z);
    impl_->listener_position = position;
    for (auto& slot : impl_->loop_slots) {
        impl_->refresh_weapon_loop_gain(slot);
    }
    for (auto& slot : impl_->server_loop_slots) {
        if (slot.active) {
            impl_->refresh_server_loop_gain(slot);
        }
    }
    const ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        impl_->last_error = "cannot change OpenAL listener: " + openal_error_text(error);
    }
}

void OpenAlFrontendAudio::set_listener_pose(SoundPosition position,
                                            const std::array<float, 3U>& forward,
                                            const std::array<float, 3U>& up) {
    const auto finite_vector = [](const std::array<float, 3U>& value) {
        return std::ranges::all_of(value, [](float component) { return std::isfinite(component); });
    };
    const auto length_squared = [](const std::array<float, 3U>& value) {
        return value[0U] * value[0U] + value[1U] * value[1U] + value[2U] * value[2U];
    };
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        impl_->last_error = impl_->context == nullptr
                                ? "OpenAL frontend audio is not started"
                                : "OpenAL listener changed from non-owner thread";
        return;
    }
    if (!finite_position(position) || !finite_vector(forward) || !finite_vector(up) ||
        length_squared(forward) < 0.5F || length_squared(up) < 0.5F) {
        impl_->last_error = "invalid OpenAL listener pose";
        return;
    }
    const std::array<ALfloat, 6U> orientation{
        forward[0U], forward[1U], forward[2U], up[0U], up[1U], up[2U]};
    clear_openal_error();
    alListener3f(AL_POSITION, position.x, position.y, position.z);
    alListenerfv(AL_ORIENTATION, orientation.data());
    impl_->listener_position = position;
    for (auto& slot : impl_->loop_slots) {
        impl_->refresh_weapon_loop_gain(slot);
    }
    for (auto& slot : impl_->server_loop_slots) {
        if (slot.active) {
            impl_->refresh_server_loop_gain(slot);
        }
    }
    const ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        impl_->last_error = "cannot change OpenAL listener pose: " + openal_error_text(error);
    }
}

void OpenAlFrontendAudio::set_spatial_gain_resolver(void* context,
                                                    SpatialGainResolver resolver) noexcept {
    impl_->spatial_gain_context = resolver != nullptr ? context : nullptr;
    impl_->spatial_gain_resolver = resolver;
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return;
    }
    for (auto& slot : impl_->loop_slots) {
        impl_->refresh_weapon_loop_gain(slot);
    }
    for (auto& slot : impl_->server_loop_slots) {
        if (slot.active) {
            impl_->refresh_server_loop_gain(slot);
        }
    }
}

void OpenAlFrontendAudio::play_one_shot(SoundHandle sound, SoundPosition position, float gain) {
    impl_->play(sound, position, gain, false);
}

void OpenAlFrontendAudio::play_head_relative_one_shot(SoundHandle sound, float gain) {
    impl_->play(sound, SoundPosition{}, gain, true);
}

bool OpenAlFrontendAudio::play_named_one_shot(std::string_view stem,
                                              SoundPosition position,
                                              float gain,
                                              bool head_relative,
                                              float start_offset,
                                              float attenuation,
                                              bool protect_voice,
                                              float pitch) {
    if (impl_->context == nullptr || !impl_->on_owner_thread() || stem.empty()) {
        return false;
    }
    if (!head_relative && !within_retail_hearing_distance(impl_->listener_position, position)) {
        return true;
    }
    const SoundHandle handle = impl_->load_named_buffer("sounds", stem);
    if (handle == 0U) {
        return false;
    }
    impl_->play(handle,
                head_relative ? SoundPosition{} : position,
                gain,
                head_relative,
                protect_voice,
                start_offset,
                attenuation,
                SpatialSoundProfile::ordinary,
                pitch);
    return true;
}

bool OpenAlFrontendAudio::play_named_music(std::string_view stem, float start_offset) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return false;
    }
    const auto request = impl_->request_named_buffer_async("music", stem);
    if (request.handle != 0U) {
        impl_->pending_music.reset();
        return impl_->play_music(request.handle, start_offset);
    }
    if (request.pending) {
        // Last packet wins, matching the synchronous source replacement path.
        impl_->pending_music = Impl::PendingMusicRequest{request.canonical, start_offset};
        return true;
    }
    return false;
}

bool OpenAlFrontendAudio::preload_named_ambience(std::string_view stem) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return false;
    }
    return impl_->load_named_buffer("ambients", stem) != 0U;
}

bool OpenAlFrontendAudio::preload_named_ambience_async(std::string_view stem) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return false;
    }
    const auto request = impl_->request_named_buffer_async("ambients", stem);
    return request.handle != 0U || request.pending;
}

bool OpenAlFrontendAudio::play_named_ambience(std::string_view stem,
                                              float volume,
                                              float start_offset) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return false;
    }
    const SoundHandle handle = impl_->load_named_buffer("ambients", stem);
    return handle != 0U && impl_->play_ambience(handle, volume, start_offset);
}

bool OpenAlFrontendAudio::play_named_ambient_one_shot(std::string_view stem,
                                                      SoundPosition position,
                                                      float volume,
                                                      bool head_relative,
                                                      float attenuation,
                                                      float start_offset) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return false;
    }
    if (!head_relative && !within_retail_hearing_distance(impl_->listener_position, position)) {
        return true;
    }
    const SoundHandle handle = impl_->load_named_buffer("ambients", stem);
    if (handle == 0U) {
        return false;
    }
    impl_->play(handle,
                head_relative ? SoundPosition{} : position,
                volume,
                head_relative,
                true,
                start_offset,
                attenuation);
    return true;
}

bool OpenAlFrontendAudio::start_named_loop(std::uint8_t loop_id,
                                           std::string_view stem,
                                           bool ambient_asset,
                                           SoundPosition position,
                                           float gain,
                                           bool head_relative,
                                           float attenuation,
                                           float start_offset) {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return false;
    }
    if (ambient_asset) {
        const auto request = impl_->request_named_buffer_async("ambients", stem);
        if (request.handle != 0U) {
            impl_->pending_server_loops.erase(loop_id);
            return impl_->start_server_loop(
                loop_id, request.handle, position, gain, head_relative, attenuation, start_offset);
        }
        if (request.pending) {
            impl_->pending_server_loops[loop_id] = Impl::PendingServerLoopRequest{
                request.canonical, position, gain, head_relative, attenuation, start_offset};
            return true;
        }
        return false;
    }
    const SoundHandle handle = impl_->load_named_buffer("sounds", stem);
    return handle != 0U &&
           impl_->start_server_loop(
               loop_id, handle, position, gain, head_relative, attenuation, start_offset);
}

void OpenAlFrontendAudio::update_named_loop(std::uint8_t loop_id, SoundPosition position) {
    if (impl_->context == nullptr || !impl_->on_owner_thread() || !finite_position(position)) {
        return;
    }
    auto* const slot = impl_->find_server_loop(loop_id);
    if (slot == nullptr || slot->relative) {
        return;
    }
    clear_openal_error();
    slot->position = position;
    alSource3f(slot->source, AL_POSITION, position.x, position.y, position.z);
    impl_->refresh_server_loop_gain(*slot);
    const ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        impl_->last_error = "cannot reposition Protocol 168 loop: " + openal_error_text(error);
    }
}

void OpenAlFrontendAudio::stop_named_loop(std::uint8_t loop_id) noexcept {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return;
    }
    impl_->pending_server_loops.erase(loop_id);
    if (auto* const slot = impl_->find_server_loop(loop_id); slot != nullptr) {
        impl_->stop_server_loop(*slot);
    }
}

void OpenAlFrontendAudio::stop_named_loops() noexcept {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return;
    }
    impl_->pending_server_loops.clear();
    for (auto& slot : impl_->server_loop_slots) {
        if (slot.active) {
            impl_->stop_server_loop(slot);
        }
    }
}

void OpenAlFrontendAudio::stop_all() noexcept {
    if (impl_->context == nullptr || !impl_->on_owner_thread()) {
        return;
    }

    impl_->stop_music();
    impl_->stop_ambience();
    stop_named_loops();
    for (auto& slot : impl_->loop_slots) {
        if (slot.active) {
            alSourceStop(slot.source);
            alSourcei(slot.source, AL_BUFFER, 0);
            slot.active = false;
            slot.voice = invalid_loop_voice;
        }
    }
    for (auto& voice : impl_->voices) {
        alSourceStop(voice.source);
        alSourcei(voice.source, AL_BUFFER, 0);
        voice.active = false;
        voice.protected_voice = false;
    }
}

void OpenAlFrontendAudio::play_menu_confirm() {
    impl_->play(menu_confirm_sound, {}, impl_->config.cue_gain, true);
}

void OpenAlFrontendAudio::play_menu_back() {
    impl_->play(menu_back_sound, {}, impl_->config.cue_gain, true);
}

void OpenAlFrontendAudio::play_menu_scroll() {
    impl_->play(menu_scroll_sound, {}, impl_->config.cue_gain, true);
}

bool OpenAlFrontendAudio::play_secondary_menu_music() {
    return play_named_music("secondary_menu_bed_001");
}

void OpenAlFrontendAudio::play_zoom_toggle(bool enabled) {
    impl_->play(enabled ? zoom_in_sound : zoom_out_sound, {}, impl_->config.cue_gain, true, true);
}

void OpenAlFrontendAudio::play_respawn_countdown_beep(bool final_beat) {
    // Character.media.play('beep1'/'beep2') is a protected 2D/UI cue.
    impl_->play(final_beat ? respawn_beep1_sound : respawn_beep2_sound,
                {}, impl_->config.cue_gain, true, true);
}

void OpenAlFrontendAudio::play_tutorial_appear() {
    impl_->play(tutorial_appear_sound, {}, impl_->config.cue_gain, true);
}

void OpenAlFrontendAudio::play_tutorial_disappear() {
    impl_->play(tutorial_disappear_sound, {}, impl_->config.cue_gain, true);
}

bool OpenAlFrontendAudio::set_master_volume(float volume) {
    if (!std::isfinite(volume) || volume < 0.0F || volume > 1.0F) {
        impl_->last_error = "master volume must be finite and within 0..1";
        return false;
    }
    if (impl_->context != nullptr && !impl_->on_owner_thread()) {
        impl_->last_error = "OpenAL master volume changed from a non-owner thread";
        return false;
    }

    if (impl_->context != nullptr) {
        clear_openal_error();
        alListenerf(AL_GAIN, volume);
        const ALenum error = alGetError();
        if (error != AL_NO_ERROR) {
            impl_->last_error = "cannot apply OpenAL master volume: " + openal_error_text(error);
            return false;
        }
    }
    impl_->master_volume = volume;
    impl_->last_error.clear();
    return true;
}

bool OpenAlFrontendAudio::set_music_volume(float volume) {
    if (!std::isfinite(volume) || volume < 0.0F || volume > 1.0F) {
        impl_->last_error = "music volume must be finite and within 0..1";
        return false;
    }
    if (impl_->context != nullptr && !impl_->on_owner_thread()) {
        impl_->last_error = "OpenAL music volume changed from a non-owner thread";
        return false;
    }

    if (impl_->context != nullptr && impl_->music_source != 0U) {
        clear_openal_error();
        alSourcef(impl_->music_source, AL_GAIN, volume);
        const ALenum error = alGetError();
        if (error != AL_NO_ERROR) {
            impl_->last_error = "cannot apply OpenAL music volume: " + openal_error_text(error);
            return false;
        }
    }
    impl_->config.music_gain = volume;
    impl_->last_error.clear();
    return true;
}

float OpenAlFrontendAudio::master_volume() const noexcept {
    return impl_->master_volume;
}

float OpenAlFrontendAudio::music_volume() const noexcept {
    return impl_->config.music_gain;
}

bool OpenAlFrontendAudio::start_menu_music() {
    impl_->pending_music.reset();
    return impl_->play_music(main_menu_music);
}

void OpenAlFrontendAudio::stop_menu_music() noexcept {
    if (impl_->context != nullptr && impl_->on_owner_thread()) {
        impl_->pending_music.reset();
        impl_->stop_music();
    }
}

bool OpenAlFrontendAudio::play_tutorial_music() {
    impl_->pending_music.reset();
    return impl_->play_music(tutorial_music);
}

bool OpenAlFrontendAudio::play_training_ambience(float volume) {
    return impl_->play_ambience(training_ambience, volume);
}

void OpenAlFrontendAudio::stop_ambience() noexcept {
    if (impl_->context != nullptr && impl_->on_owner_thread()) {
        impl_->stop_ambience();
    }
}

bool OpenAlFrontendAudio::is_started() const noexcept {
    return impl_->context != nullptr;
}

std::size_t OpenAlFrontendAudio::active_one_shot_voices() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(impl_->voices.begin(), impl_->voices.end(), [](const Impl::Voice& voice) {
            return voice.active;
        }));
}

std::string_view OpenAlFrontendAudio::last_error() const noexcept {
    return impl_->last_error;
}

} // namespace battlespades::audio
