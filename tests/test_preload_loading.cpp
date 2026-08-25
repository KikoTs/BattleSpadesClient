#include "battlespades/assets/preload_service.hpp"
#include "battlespades/frontend/loading_presentation.hpp"
#include "battlespades/frontend/loading_screen.hpp"

#include <array>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace battlespades::assets;
using namespace battlespades::frontend;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

std::vector<PreloadAsset> manifest() {
    return {
        {"terrain", "png/high/terrain.png", PreloadAssetKind::texture, 3U, true},
        {"rifle", "kv6/rifle.kv6", PreloadAssetKind::model, 1U, true},
        {"ambient", "ambients/city.ogg", PreloadAssetKind::audio, 2U, false},
    };
}

void manifest_validation_and_queue_bounds_fail_closed() {
    PreloadService service{{2U, 1U, 1U}};
    expect(!service.begin(manifest()), "oversized manifest must be rejected atomically");
    expect(service.snapshot().state == PreloadBatchState::idle,
           "rejected manifest must not alter service state");
    expect(!service.begin({{"same", "a", PreloadAssetKind::texture, 1U, true},
                           {"same", "b", PreloadAssetKind::texture, 1U, true}}),
           "duplicate logical IDs must be rejected");
}

void decode_parallelism_is_bounded_and_upload_order_is_manifest_order() {
    PreloadService service{{8U, 2U, 3U}};
    expect(service.begin(manifest()), "valid manifest should begin");
    const auto first = service.claim_decode();
    const auto second = service.claim_decode();
    expect(first.has_value() && second.has_value() && !service.claim_decode().has_value(),
           "decode claims must stop at configured in-flight cap");

    expect(service.complete_decode(second->token, true) == PreloadCompletionResult::accepted,
           "second decode may finish first");
    expect(!service.claim_upload().has_value(),
           "upload must wait for earlier manifest item despite worker timing");
    expect(service.complete_decode(first->token, true) == PreloadCompletionResult::accepted,
           "first decode should finish");
    const auto upload = service.claim_upload();
    expect(upload.has_value() && upload->token.ordinal == 0U,
           "first upload must follow manifest order");
    expect(service.complete_upload(upload->token, true) == PreloadCompletionResult::accepted,
           "upload completion should be accepted once");
    expect(service.complete_upload(upload->token, true) == PreloadCompletionResult::invalid_stage,
           "duplicate completion must fail closed");
}

void weighted_progress_and_optional_failure_are_truthful() {
    PreloadService service;
    expect(service.begin(manifest()), "manifest should begin");
    while (const auto decode = service.claim_decode()) {
        expect(service.complete_decode(decode->token, true) == PreloadCompletionResult::accepted,
               "decode should complete");
    }
    while (const auto upload = service.claim_upload()) {
        const auto optional_failure = upload->asset.id == "ambient";
        expect(service.complete_upload(upload->token, !optional_failure) ==
                   PreloadCompletionResult::accepted,
               "upload should complete");
        while (const auto decode = service.claim_decode()) {
            expect(service.complete_decode(decode->token, true) ==
                       PreloadCompletionResult::accepted,
                   "remaining decode should complete");
        }
    }
    const auto snapshot = service.snapshot();
    expect(snapshot.state == PreloadBatchState::ready_with_warnings && snapshot.progress == 1.0,
           "optional failure completes batch with warning and terminal progress");
    const auto boot = boot_loading_snapshot(snapshot);
    expect(boot.filled_bullets == 36U, "complete boot manifest fills all retail bullets");
}

void loading_texture_selection_recovers_official_aliases_and_skin_variants() {
    const auto classic = select_loading_textures("City of Chicago", "CTF_TITLE", true, "");
    expect(classic.map_image_asset.find("cityofchicago") != std::string::npos &&
               classic.infographic_asset.find("classic_ctf") != std::string::npos,
           "official map alias and Classic CTF infographic must resolve");
    const auto mafia = select_loading_textures("Unknown UGC", "VIP_MODE_TITLE", false, "mafia");
    expect(mafia.map_image_asset.find("default") != std::string::npos &&
               mafia.infographic_asset.find("mafia_vip") != std::string::npos,
           "UGC fallback and Mafia VIP art must resolve");

    const auto compact =
        select_loading_textures("MayanJungle", "TDM_TITLE", false, "default");
    const auto filename = select_loading_textures(
        "maps/City_Of_Chicago.vxl", "TDM_TITLE", false, "default");
    expect(compact.map_image_asset.find("mayanjungle") != std::string::npos &&
               filename.map_image_asset.find("cityofchicago") != std::string::npos,
           "authoritative server map stems must replace the previous session's loading art");
}

void protocol_mode_ordinals_override_stale_browser_metadata() {
    constexpr std::array<std::string_view, 13U> expected_codes{
        "nor", "dem", "zom", "mh", "oc", "dia", "tdm",
        "vip", "ctf", "tc", "tut", "cctf", "ugc"};
    for (std::size_t mode_id{}; mode_id < expected_codes.size(); ++mode_id) {
        const auto presentation =
            resolve_protocol168_mode(static_cast<std::uint8_t>(mode_id), false);
        expect(presentation.code == expected_codes[mode_id],
               "every Protocol 168 gamemode ordinal must retain its presentation route");
    }

    const auto arena = resolve_protocol168_mode(0U, false);
    expect(arena.code == "nor" && arena.title_key == "ARENA",
           "BattleSpades Arena must retain its MODE_NORMAL extension row");
    const auto vip = resolve_protocol168_mode(7U, false);
    expect(vip.code == "vip" && vip.title_key == "VIP_MODE_TITLE",
           "InitialInfo MODE_VIP must select the VIP loading presentation");
    const auto classic = resolve_protocol168_mode(8U, true);
    expect(classic.code == "cctf" && classic.title_key == "CLASSIC_CTF_TITLE" &&
               classic.classic,
           "classic CTF is MODE_CTF plus the InitialInfo classic feature bit");
    const auto malformed = resolve_protocol168_mode(255U, true);
    expect(malformed.code == "tdm" && !malformed.classic,
           "unknown mode ordinals must fail closed to ordinary TDM presentation");
}

void match_loading_tracks_three_phases_tabs_ready_and_timeout() {
    MatchLoadingModel model;
    model.begin("City of Chicago", "CTF_TITLE", true, "");
    model.initial_info("City of Chicago", "CTF_TITLE", true, "", false);
    expect(model.snapshot().tabs.size() == 3U, "normal match has Map, Mode, Scores tabs");
    model.map_progress(1.0);
    model.syncing_map();
    model.begin_asset_preload();
    PreloadSnapshot assets;
    assets.state = PreloadBatchState::active;
    assets.progress = 0.5;
    model.set_preload_snapshot(assets);
    expect(model.snapshot().overall_progress > 0.8 && !model.snapshot().start_enabled,
           "asset preload occupies final third and gates Start");
    assets.state = PreloadBatchState::ready;
    assets.progress = 1.0;
    model.set_preload_snapshot(assets);
    expect(model.snapshot().overall_progress == 1.0 && model.snapshot().start_enabled,
           "ready assets complete the match loader");

    model.begin();
    model.tick(30.1);
    expect(model.snapshot().state == MatchLoadingState::timed_out,
           "30 seconds without progress must return timeout state");
}

void loading_presentations_emit_retail_splash_bullets_and_match_frame() {
    BootLoadingSnapshot boot;
    boot.progress = 0.5;
    boot.filled_bullets = 18U;
    const auto boot_draw = BootLoadingPresentation{}.build(boot);
    expect(boot_draw.size() == 56U,
           "boot draw must contain splash, title, 36 dark and 18 bright bullets");

    MatchLoadingModel model;
    model.begin("Atlantis", "TDM_TITLE", false, "");
    model.initial_info("Atlantis", "TDM_TITLE", false, "", false);
    const auto match_draw = MatchLoadingPresentation{}.build(model.snapshot());
    expect(match_draw.size() > 15U,
           "match loading draw must include frame, art, tabs, progress, and Start");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};
} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"manifest_validation_and_queue_bounds_fail_closed",
         manifest_validation_and_queue_bounds_fail_closed},
        {"decode_parallelism_is_bounded_and_upload_order_is_manifest_order",
         decode_parallelism_is_bounded_and_upload_order_is_manifest_order},
        {"weighted_progress_and_optional_failure_are_truthful",
         weighted_progress_and_optional_failure_are_truthful},
        {"loading_texture_selection_recovers_official_aliases_and_skin_variants",
         loading_texture_selection_recovers_official_aliases_and_skin_variants},
        {"protocol_mode_ordinals_override_stale_browser_metadata",
         protocol_mode_ordinals_override_stale_browser_metadata},
        {"match_loading_tracks_three_phases_tabs_ready_and_timeout",
         match_loading_tracks_three_phases_tabs_ready_and_timeout},
        {"loading_presentations_emit_retail_splash_bullets_and_match_frame",
         loading_presentations_emit_retail_splash_bullets_and_match_frame},
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
    return failures == 0U ? 0 : 1;
}
