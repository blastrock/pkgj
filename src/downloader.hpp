#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <tuple>
#include <vector>

#include "thread.hpp"

enum Type
{
    Game,
    Update,
    Dlc,
    PsmGame,
    PsxGame,
    PspGame,
    CompPack,
};

struct DownloadItem
{
    Type type;
    std::string name;
    std::string content;
    std::string url;
    std::vector<uint8_t> rif;
    std::vector<uint8_t> digest;
    bool save_as_iso;
    std::string partition;
};

std::string type_to_string(Type type);

class Downloader
{
public:
    Downloader(const Downloader&) = delete;
    Downloader(Downloader&&) = delete;
    Downloader& operator=(const Downloader&) = delete;
    Downloader& operator=(Downloader&&) = delete;

    Downloader();
    ~Downloader();

    void add(const DownloadItem& d);
    void remove_from_queue(const std::string& contentid);
    bool is_in_queue(const std::string& titleid);
    std::optional<DownloadItem> get_current_download();
    std::tuple<uint64_t, uint64_t> get_current_download_progress();

    std::function<void(const std::string& content)> refresh;
    std::function<void(const std::string& error)> error;

private:
    using ScopeLock = std::lock_guard<Mutex>;

    Cond _cond;
    std::deque<DownloadItem> _queue;

    DownloadItem _current_download;
    bool _cancel_current = false;
    std::atomic<uint64_t> _download_offset = 0;
    std::atomic<uint64_t> _download_size = 0;

    Thread _thread;
    bool _dying = false;

    void run();
    void do_download(const DownloadItem& item);

    void do_download_package(const DownloadItem& item);
    void do_download_comppack(const DownloadItem& item);
};
