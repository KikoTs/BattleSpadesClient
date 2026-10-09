#include "battlespades/frontend/workshop_session.hpp"
#include <stdexcept>
#include <algorithm>

namespace battlespades::frontend {
namespace {
using namespace network;
std::vector<WorkshopReference> references(const nlohmann::json& data) {
    if (data.value("schema_version",0)!=1 || !data.contains("subscriptions") || !data["subscriptions"].is_array() || data["subscriptions"].size()>256U)
        throw std::runtime_error("Unsupported Workshop subscription response.");
    std::vector<WorkshopReference> result;
    for (const auto& row:data["subscriptions"]) {
        WorkshopReference ref{row.value("source",std::string{}),row.value("item_id",std::string{})};
        if (!ref.valid()) throw std::runtime_error("Invalid Workshop subscription.");
        result.push_back(std::move(ref));
    }
    return result;
}
} // namespace
WorkshopSession::WorkshopSession(std::shared_ptr<network::RevivalIdentityService> identity,
    std::filesystem::path maps,std::filesystem::path cache):identity_(std::move(identity)),maps_(std::move(maps)),cache_(std::move(cache)),media_(cache_) {}
WorkshopSession::~WorkshopSession() { cancel(); }
void WorkshopSession::cancel() { stop_.request_stop(); }
void WorkshopSession::start(WorkshopMenuModel& model,WorkshopAction action) {
    if (action==WorkshopAction::cancel) { pending_browse_=false; cancel(); return; }
    if (action==WorkshopAction::none) return;
    if (worker_.valid()) {
        if (action==WorkshopAction::browse) { pending_browse_=true; cancel(); }
        return;
    }
    const auto cached=identity_->cached_account();
    const auto account=cached && identity_->has_online_session()?cached->public_id:std::string{};
    const auto* selected=model.selection();
    if ((action==WorkshopAction::subscribe || action==WorkshopAction::unsubscribe || action==WorkshopAction::download || action==WorkshopAction::remove) && !selected) return;
    if ((action==WorkshopAction::subscribe || action==WorkshopAction::unsubscribe || action==WorkshopAction::sync || (action==WorkshopAction::browse && model.source=="library")) && account.empty()) {
        model.message="Sign in to save and sync subscriptions. Public maps can still be downloaded."; ++model.revision; return;
    }
    const auto choice=selected?std::optional<network::PublicWorkshopItem>{*selected}:std::nullopt;
    account_=account; model.signed_in=!account.empty();
    const auto browse_key=model.source+":"+account+":"+model.query+":"+model.filters.sort+":"+model.filters.tag+":"+
        std::to_string(model.filters.days)+":"+std::to_string(model.page);
    if (action==WorkshopAction::browse) {
        model.image=0; media_.page({}); media_selection_.clear();
        if (const auto found=pages_.find(browse_key); found!=pages_.end() && found->second.expires>std::chrono::steady_clock::now()) {
            model.items=found->second.page.items; model.more=found->second.page.more; model.selected=0; model.busy=false;
            model.message="Choose a map. Screenshots load as you browse."; media_.page(model.items); ++model.revision; return;
        }
    }
    stop_=std::stop_source{};
    { std::lock_guard lock{progress_->mutex}; progress_->message.clear(); }
    model.busy=true; model.message=action==WorkshopAction::sync?"Syncing your Workshop library...":"Loading Workshop..."; ++model.revision;
    worker_=std::async(std::launch::async,[service=identity_,maps=maps_,cache=cache_,progress=progress_,stop=stop_.get_token(),
        source=model.source,query=model.query,page=model.page,filters=model.filters,account,action,choice,browse_key] {
        Outcome outcome; outcome.account=account; outcome.browse_key=browse_key;
        const auto update=[progress](std::string message) { std::lock_guard lock{progress->mutex}; progress->message=std::move(message); };
        const auto request=[&](WorkshopRequestKind kind,const WorkshopReference& ref=WorkshopReference{}) {
            const auto response=service->workshop_request({kind,ref,query,account,page*12U},stop);
            if (!response) throw std::runtime_error(response.error);
            return response.payload;
        };
        const auto resolve=[&](const WorkshopReference& ref) {
            auto item=ref.source=="steam"?PublicWorkshop::steam_details(ref.id,stop):PublicWorkshop::parse_archive(request(WorkshopRequestKind::details,ref).at("item"));
            if (item.reference.key()!=ref.key()) throw std::runtime_error("Workshop returned a different map.");
            return item;
        };
        const auto remember=[&](const std::vector<WorkshopReference>& refs) {
            outcome.subscriptions.emplace();
            for (const auto& ref:refs) outcome.subscriptions->insert(ref.key());
        };
        const auto install=[&](PublicWorkshopItem item) {
            if (PublicWorkshop::installed(item,maps)) { outcome.installed.insert(item.reference.key()); return; }
            PublicWorkshop::cache_preview(item,cache,stop);
            PublicWorkshop::install(item,maps,stop,update);
            outcome.installed.insert(item.reference.key()); ++outcome.changes;
        };
        try {
            if (action==WorkshopAction::browse) {
                outcome.page.emplace();
                if (source=="steam") outcome.page=PublicWorkshop::browse_steam(query,page,stop,filters);
                else if (source=="aosplay") {
                    const auto data=request(WorkshopRequestKind::catalog);
                    for (const auto& row:data.at("items")) {
                        if (outcome.page->items.size()==12U) break;
                        outcome.page->items.push_back(PublicWorkshop::parse_archive(row));
                    }
                    outcome.page->more=data.contains("next_offset") && !data["next_offset"].is_null();
                } else {
                    const auto refs=references(request(WorkshopRequestKind::subscriptions)); remember(refs);
                    for (std::size_t i=page*12U;i<refs.size() && i<page*12U+12U;++i) {
                        try { outcome.page->items.push_back(resolve(refs[i])); }
                        catch (const std::exception& error) {
                            if (stop.stop_requested()) throw;
                            // Keep unavailable entries visible and unsubscribable.
                            PublicWorkshopItem missing; missing.reference=refs[i]; missing.title="Unavailable item "+refs[i].id; missing.description=error.what();
                            outcome.page->items.push_back(std::move(missing));
                        }
                    }
                    outcome.page->more=refs.size()>(page+1U)*12U;
                }
                for (auto& item:outcome.page->items) {
                    if (PublicWorkshop::installed(item,maps)) outcome.installed.insert(item.reference.key());
                    else outcome.removed.insert(item.reference.key());
                }
                outcome.message=outcome.page->items.empty()?"No maps found. Try another search or paste a Steam Workshop link.":"Choose a map to download or subscribe. Installed maps appear in Create Match.";
            } else if (action==WorkshopAction::sync) {
                const auto refs=references(request(WorkshopRequestKind::subscriptions)); remember(refs);
                std::size_t failed{};
                for (const auto& ref:refs) {
                    if (stop.stop_requested()) throw std::runtime_error("Workshop sync cancelled.");
                    try { install(resolve(ref)); }
                    catch (const std::exception& error) { if (stop.stop_requested()) throw; ++failed; outcome.message=error.what(); }
                }
                outcome.message=failed?std::to_string(failed)+" map(s) could not update. "+outcome.message:"Workshop subscriptions are up to date.";
            } else if (choice) {
                if (action==WorkshopAction::subscribe || action==WorkshopAction::unsubscribe) {
                    remember(references(request(action==WorkshopAction::subscribe?WorkshopRequestKind::subscribe:WorkshopRequestKind::unsubscribe,choice->reference)));
                    outcome.message=action==WorkshopAction::unsubscribe?"Unsubscribed. Downloaded files are kept until you choose Remove files.":"Subscribed and downloaded. Updates sync with your account.";
                }
                if (action==WorkshopAction::download || action==WorkshopAction::subscribe) {
                    install(resolve(choice->reference));
                    if (action==WorkshopAction::download) outcome.message="Downloaded. Open Create Match > Subscribed Maps to play.";
                } else if (action==WorkshopAction::remove) {
                    PublicWorkshop::remove(choice->reference,maps); outcome.removed.insert(choice->reference.key()); ++outcome.changes;
                    outcome.message="Local map files removed.";
                }
            }
        } catch (const std::exception& error) { outcome.message=error.what(); outcome.page.reset(); }
        return outcome;
    });
}
void WorkshopSession::pump(WorkshopMenuModel& model) {
    using namespace std::chrono_literals;
    const auto cached=identity_->cached_account();
    const auto current=cached && identity_->has_online_session()?cached->public_id:std::string{};
    if (current!=account_) {
        cancel(); account_=current; model.subscriptions.clear(); model.signed_in=!current.empty(); ++model.revision;
        if (model.source=="library") { model.items.clear(); model.selected=0; model.more=false; }
        next_sync_={};
    }
    if (worker_.valid() && worker_.wait_for(0ms)==std::future_status::ready) {
        auto outcome=worker_.get(); model.busy=false;
        if (outcome.account==current && !pending_browse_) {
            if (outcome.page) {
                if (pages_.size()>=16U) pages_.erase(pages_.begin());
                pages_[outcome.browse_key]={*outcome.page,std::chrono::steady_clock::now()+60s};
                model.items=std::move(outcome.page->items); model.more=outcome.page->more; model.selected=0; model.image=0;
                media_.page(model.items); media_selection_.clear();
            }
            if (outcome.subscriptions) model.subscriptions=std::move(*outcome.subscriptions);
            model.installed.insert(outcome.installed.begin(),outcome.installed.end());
            for (const auto& key:outcome.removed) model.installed.erase(key);
            model.message=std::move(outcome.message);
        } else if (outcome.account!=current) model.message="Account changed. Refresh the Workshop.";
        model.generation+=outcome.changes; ++model.revision;
    } else if (worker_.valid()) {
        std::lock_guard lock{progress_->mutex};
        if (!progress_->message.empty() && model.message!=progress_->message) { model.message=progress_->message; ++model.revision; }
    }
    const auto now=std::chrono::steady_clock::now();
    for (auto& update:media_.poll()) {
        const auto item=std::ranges::find_if(model.items,[&](const auto& entry) { return entry.reference.key()==update.key; });
        if (item==model.items.end()) continue;
        if (!update.gallery) { item->preview_file=std::move(update.file); item->preview_loaded=true; }
        else {
            if (!update.urls.empty()) { item->preview_urls=std::move(update.urls); item->preview_files.resize(item->preview_urls.size()); }
            if (!update.file.empty() && update.index<item->preview_files.size()) {
                item->preview_files[update.index]=std::move(update.file);
                if (update.index==0) item->preview_file=item->preview_files[0];
            }
            item->gallery_loaded=update.done;
            if (!update.error.empty()) item->gallery_error=std::move(update.error);
        }
        ++model.revision;
    }
    const auto* selection=model.selection();
    const auto key=selection?selection->reference.key():std::string{};
    if (key!=media_selection_) {
        media_.select(nullptr); media_selection_=key; selection_since_=now;
    } else if (selection && now-selection_since_>=150ms) media_.select(selection);
    if (pending_browse_ && !worker_.valid()) { pending_browse_=false; start(model,WorkshopAction::browse); }
    if (!current.empty() && !worker_.valid() && now>=next_sync_) {
        next_sync_=now+5min; start(model,WorkshopAction::sync);
    }
}
} // namespace battlespades::frontend
