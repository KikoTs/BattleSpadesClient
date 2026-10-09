#pragma once
#include "battlespades/network/public_workshop.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <map>
#include <chrono>

namespace battlespades::frontend {
/** Four cancellable media workers; catalog results never wait for images. */
class WorkshopMediaLoader final {
public:
    struct Update {
        std::string key, file, error;
        std::vector<std::string> urls;
        std::size_t index{};
        bool gallery{}, done{};
        std::uint64_t generation{};
    };
    using Thumbnail = std::function<std::string(network::PublicWorkshopItem, const std::filesystem::path&, std::stop_token)>;
    using Gallery = std::function<std::vector<std::string>(std::string_view, std::stop_token)>;
    explicit WorkshopMediaLoader(std::filesystem::path cache, Thumbnail thumbnail = {}, Gallery gallery = {});
    ~WorkshopMediaLoader();
    void page(const std::vector<network::PublicWorkshopItem>& items);
    void select(const network::PublicWorkshopItem* item);
    [[nodiscard]] std::vector<Update> poll();
private:
    struct Job { network::PublicWorkshopItem item; std::stop_token stop; std::uint64_t generation{}; bool gallery{}; };
    void run(std::stop_token stop);
    void publish(Update update, std::stop_token stop);
    std::filesystem::path cache_;
    Thumbnail thumbnail_;
    Gallery gallery_;
    std::mutex mutex_;
    std::condition_variable_any changed_;
    std::deque<Job> jobs_;
    std::deque<Update> completed_;
    std::map<std::string,std::pair<std::chrono::steady_clock::time_point,std::vector<std::string>>> galleries_;
    std::stop_source page_stop_, selection_stop_;
    std::uint64_t generation_{};
    std::string selected_;
    // Last member: joined before the queues and callbacks are destroyed.
    std::vector<std::jthread> workers_;
};
}
