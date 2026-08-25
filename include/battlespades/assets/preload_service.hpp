#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace battlespades::assets {

enum class PreloadAssetKind : std::uint8_t {
    texture,
    font,
    audio,
    model,
    shader,
    map,
    other,
};

struct PreloadAsset final {
    std::string id;
    std::string path;
    PreloadAssetKind kind{PreloadAssetKind::other};
    std::uint32_t weight{1U};
    bool required{true};
};

struct PreloadToken final {
    std::uint64_t generation{};
    std::uint32_t ordinal{};

    [[nodiscard]] friend constexpr bool operator==(const PreloadToken&,
                                                   const PreloadToken&) = default;
};

struct PreloadClaim final {
    PreloadToken token{};
    PreloadAsset asset;
};

enum class PreloadBatchState : std::uint8_t {
    idle,
    active,
    ready,
    ready_with_warnings,
    failed,
    cancelled,
};

enum class PreloadCompletionResult : std::uint8_t {
    accepted,
    stale_generation,
    invalid_token,
    invalid_stage,
};

struct PreloadSnapshot final {
    std::uint64_t generation{};
    PreloadBatchState state{PreloadBatchState::idle};
    std::size_t total_assets{};
    std::size_t queued_assets{};
    std::size_t decoding_assets{};
    std::size_t awaiting_upload_assets{};
    std::size_t uploading_assets{};
    std::size_t ready_assets{};
    std::size_t failed_assets{};
    std::size_t cancelled_assets{};
    std::size_t required_failures{};
    std::uint64_t completed_weight_units{};
    std::uint64_t total_weight_units{};
    double progress{};
};

struct PreloadServiceConfig final {
    std::size_t maximum_assets{4'096U};
    std::size_t maximum_decode_in_flight{4U};
    std::size_t maximum_decoded_backlog{64U};
};

/**
 * Bounded, thread-safe, deterministic two-stage preload coordinator.
 *
 * Decode workers call claim_decode/complete_decode. The render thread calls
 * claim_upload/complete_upload. Payload ownership remains in the backend and
 * is addressed by token, keeping this service independent of graphics/audio
 * libraries. Upload claims retain manifest order even when decode completion
 * order differs, which makes progress and failure behavior reproducible.
 */
class PreloadService final {
public:
    explicit PreloadService(PreloadServiceConfig config = {});

    /** Atomically validates and begins a new manifest. */
    [[nodiscard]] bool begin(std::vector<PreloadAsset> manifest);
    [[nodiscard]] std::optional<PreloadClaim> claim_decode();
    [[nodiscard]] PreloadCompletionResult
    complete_decode(PreloadToken token, bool succeeded, std::string error = {});
    [[nodiscard]] std::optional<PreloadClaim> claim_upload();
    [[nodiscard]] PreloadCompletionResult
    complete_upload(PreloadToken token, bool succeeded, std::string error = {});
    void cancel() noexcept;

    [[nodiscard]] PreloadSnapshot snapshot() const noexcept;
    [[nodiscard]] std::optional<std::string> error_for(PreloadToken token) const;

private:
    enum class JobState : std::uint8_t {
        queued,
        decoding,
        decoded,
        uploading,
        ready,
        failed,
        cancelled,
    };

    struct Job final {
        PreloadAsset asset;
        JobState state{JobState::queued};
        std::string error;
    };

    [[nodiscard]] PreloadCompletionResult validate(PreloadToken token,
                                                   JobState expected) const noexcept;
    [[nodiscard]] static bool terminal(JobState state) noexcept;
    void update_batch_state() noexcept;

    PreloadServiceConfig config_;
    mutable std::mutex mutex_;
    std::vector<Job> jobs_;
    std::uint64_t generation_{};
    PreloadBatchState state_{PreloadBatchState::idle};
};

} // namespace battlespades::assets
