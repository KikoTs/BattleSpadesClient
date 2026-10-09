#pragma once
#include "battlespades/network/revival_identity.hpp"
#include "battlespades/frontend/workshop_media.hpp"
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <map>
#include <set>

namespace battlespades::frontend {
enum class WorkshopAction { none, browse, subscribe, unsubscribe, download, remove, sync, cancel };
struct WorkshopMenuModel final {
    std::vector<network::PublicWorkshopItem> items;
    std::set<std::string> subscriptions, installed;
    std::string source{"steam"}, query, message{"Browse community maps, or paste a Steam Workshop link."};
    std::size_t page{}, selected{};
    std::size_t image{};
    network::WorkshopBrowseOptions filters;
    bool details{};
    std::uint64_t revision{}, generation{};
    bool busy{}, more{}, signed_in{};
    [[nodiscard]] const network::PublicWorkshopItem* selection() const {
        return selected<items.size()?&items[selected]:nullptr;
    }
};
/** One bounded worker serializes downloads and publishes immutable UI outcomes. */
class WorkshopSession final {
public:
    WorkshopSession(std::shared_ptr<network::RevivalIdentityService> identity,
                    std::filesystem::path maps, std::filesystem::path cache);
    ~WorkshopSession();
    void start(WorkshopMenuModel& model, WorkshopAction action);
    void pump(WorkshopMenuModel& model);
    void cancel();
private:
    struct Outcome {
        std::string account, message;
        std::optional<network::WorkshopPage> page;
        std::optional<std::set<std::string>> subscriptions;
        std::set<std::string> installed, removed;
        std::uint64_t changes{};
        std::string browse_key;
    };
    struct Progress { std::mutex mutex; std::string message; };
    std::shared_ptr<network::RevivalIdentityService> identity_;
    std::filesystem::path maps_, cache_;
    std::future<Outcome> worker_;
    std::stop_source stop_;
    std::shared_ptr<Progress> progress_{std::make_shared<Progress>()};
    std::string account_;
    bool pending_browse_{};
    std::chrono::steady_clock::time_point next_sync_{};
    struct CachedPage { network::WorkshopPage page; std::chrono::steady_clock::time_point expires; };
    std::map<std::string,CachedPage> pages_;
    std::string media_selection_;
    std::chrono::steady_clock::time_point selection_since_{};
    WorkshopMediaLoader media_;
};
} // namespace battlespades::frontend
