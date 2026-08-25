#include "battlespades/assets/preload_service.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace battlespades::assets {
PreloadService::PreloadService(PreloadServiceConfig config) : config_{config} {
    if (config_.maximum_assets == 0U || config_.maximum_decode_in_flight == 0U ||
        config_.maximum_decoded_backlog == 0U ||
        config_.maximum_assets > std::numeric_limits<std::uint32_t>::max() ||
        config_.maximum_decode_in_flight > config_.maximum_decoded_backlog) {
        throw std::invalid_argument{"invalid preload service bounds"};
    }
}

bool PreloadService::begin(std::vector<PreloadAsset> manifest) {
    std::scoped_lock lock{mutex_};
    if (state_ == PreloadBatchState::active || manifest.size() > config_.maximum_assets) {
        return false;
    }

    std::unordered_set<std::string> ids;
    ids.reserve(manifest.size());
    std::uint64_t total_weight{};
    for (const auto& asset : manifest) {
        if (asset.id.empty() || asset.path.empty() || asset.weight == 0U ||
            !ids.insert(asset.id).second ||
            total_weight > std::numeric_limits<std::uint64_t>::max() - asset.weight) {
            return false;
        }
        total_weight += asset.weight;
    }

    ++generation_;
    jobs_.clear();
    jobs_.reserve(manifest.size());
    for (auto& asset : manifest) {
        jobs_.push_back(Job{std::move(asset), JobState::queued, {}});
    }
    state_ = jobs_.empty() ? PreloadBatchState::ready : PreloadBatchState::active;
    return true;
}

std::optional<PreloadClaim> PreloadService::claim_decode() {
    std::scoped_lock lock{mutex_};
    if (state_ != PreloadBatchState::active) {
        return std::nullopt;
    }
    const auto decoding = std::ranges::count_if(
        jobs_, [](const auto& job) { return job.state == JobState::decoding; });
    const auto reserved_backlog = std::ranges::count_if(jobs_, [](const auto& job) {
        return job.state == JobState::decoding || job.state == JobState::decoded ||
               job.state == JobState::uploading;
    });
    if (static_cast<std::size_t>(decoding) >= config_.maximum_decode_in_flight ||
        static_cast<std::size_t>(reserved_backlog) >= config_.maximum_decoded_backlog) {
        return std::nullopt;
    }

    for (std::size_t ordinal = 0U; ordinal < jobs_.size(); ++ordinal) {
        auto& job = jobs_[ordinal];
        if (job.state == JobState::queued) {
            job.state = JobState::decoding;
            return PreloadClaim{PreloadToken{generation_, static_cast<std::uint32_t>(ordinal)},
                                job.asset};
        }
    }
    return std::nullopt;
}

PreloadCompletionResult
PreloadService::complete_decode(PreloadToken token, bool succeeded, std::string error) {
    std::scoped_lock lock{mutex_};
    const auto result = validate(token, JobState::decoding);
    if (result != PreloadCompletionResult::accepted) {
        return result;
    }
    auto& job = jobs_[token.ordinal];
    if (succeeded) {
        job.state = JobState::decoded;
        job.error.clear();
    } else {
        job.state = JobState::failed;
        job.error = error.empty() ? "decode failed" : std::move(error);
    }
    update_batch_state();
    return PreloadCompletionResult::accepted;
}

std::optional<PreloadClaim> PreloadService::claim_upload() {
    std::scoped_lock lock{mutex_};
    if (state_ != PreloadBatchState::active) {
        return std::nullopt;
    }
    // Do not let thread timing reorder GPU/audio creation. The first unfinished
    // manifest item is the only upload candidate.
    for (std::size_t ordinal = 0U; ordinal < jobs_.size(); ++ordinal) {
        auto& job = jobs_[ordinal];
        if (terminal(job.state)) {
            continue;
        }
        if (job.state != JobState::decoded) {
            return std::nullopt;
        }
        job.state = JobState::uploading;
        return PreloadClaim{PreloadToken{generation_, static_cast<std::uint32_t>(ordinal)},
                            job.asset};
    }
    return std::nullopt;
}

PreloadCompletionResult
PreloadService::complete_upload(PreloadToken token, bool succeeded, std::string error) {
    std::scoped_lock lock{mutex_};
    const auto result = validate(token, JobState::uploading);
    if (result != PreloadCompletionResult::accepted) {
        return result;
    }
    auto& job = jobs_[token.ordinal];
    if (succeeded) {
        job.state = JobState::ready;
        job.error.clear();
    } else {
        job.state = JobState::failed;
        job.error = error.empty() ? "upload failed" : std::move(error);
    }
    update_batch_state();
    return PreloadCompletionResult::accepted;
}

void PreloadService::cancel() noexcept {
    std::scoped_lock lock{mutex_};
    if (state_ != PreloadBatchState::active) {
        return;
    }
    for (auto& job : jobs_) {
        if (!terminal(job.state)) {
            job.state = JobState::cancelled;
            job.error.clear();
        }
    }
    state_ = PreloadBatchState::cancelled;
}

PreloadSnapshot PreloadService::snapshot() const noexcept {
    std::scoped_lock lock{mutex_};
    PreloadSnapshot result;
    result.generation = generation_;
    result.state = state_;
    result.total_assets = jobs_.size();
    for (const auto& job : jobs_) {
        result.total_weight_units += job.asset.weight;
        switch (job.state) {
        case JobState::queued:
            ++result.queued_assets;
            break;
        case JobState::decoding:
            ++result.decoding_assets;
            break;
        case JobState::decoded:
            ++result.awaiting_upload_assets;
            break;
        case JobState::uploading:
            ++result.uploading_assets;
            break;
        case JobState::ready:
            ++result.ready_assets;
            result.completed_weight_units += job.asset.weight;
            break;
        case JobState::failed:
            ++result.failed_assets;
            result.required_failures += job.asset.required ? 1U : 0U;
            result.completed_weight_units += job.asset.weight;
            break;
        case JobState::cancelled:
            ++result.cancelled_assets;
            result.completed_weight_units += job.asset.weight;
            break;
        }
    }
    result.progress = result.total_weight_units == 0U
                          ? (state_ == PreloadBatchState::ready ? 1.0 : 0.0)
                          : static_cast<double>(result.completed_weight_units) /
                                static_cast<double>(result.total_weight_units);
    return result;
}

std::optional<std::string> PreloadService::error_for(PreloadToken token) const {
    std::scoped_lock lock{mutex_};
    if (token.generation != generation_ || token.ordinal >= jobs_.size() ||
        jobs_[token.ordinal].error.empty()) {
        return std::nullopt;
    }
    return jobs_[token.ordinal].error;
}

PreloadCompletionResult PreloadService::validate(PreloadToken token,
                                                 JobState expected) const noexcept {
    if (token.generation != generation_) {
        return PreloadCompletionResult::stale_generation;
    }
    if (token.ordinal >= jobs_.size()) {
        return PreloadCompletionResult::invalid_token;
    }
    if (jobs_[token.ordinal].state != expected) {
        return PreloadCompletionResult::invalid_stage;
    }
    return PreloadCompletionResult::accepted;
}

void PreloadService::update_batch_state() noexcept {
    if (state_ != PreloadBatchState::active ||
        !std::ranges::all_of(jobs_, [](const auto& job) { return terminal(job.state); })) {
        return;
    }
    const auto required_failure = std::ranges::any_of(
        jobs_, [](const auto& job) { return job.state == JobState::failed && job.asset.required; });
    const auto optional_failure = std::ranges::any_of(jobs_, [](const auto& job) {
        return job.state == JobState::failed && !job.asset.required;
    });
    const auto cancelled = std::ranges::any_of(
        jobs_, [](const auto& job) { return job.state == JobState::cancelled; });
    state_ = required_failure   ? PreloadBatchState::failed
             : cancelled        ? PreloadBatchState::cancelled
             : optional_failure ? PreloadBatchState::ready_with_warnings
                                : PreloadBatchState::ready;
}

bool PreloadService::terminal(JobState state) noexcept {
    return state == JobState::ready || state == JobState::failed || state == JobState::cancelled;
}

} // namespace battlespades::assets
