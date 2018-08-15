#include "downloader.hpp"

#include <boost/scope_exit.hpp>

#include "download.hpp"
#include "filedownload.hpp"
#include "vitahttp.hpp"

#include <fmt/format.h>

std::string type_to_string(Type type)
{
    switch (type)
    {
    case Type::Game:
        return "game";
    case Type::Update:
        return "update";
    case Type::Dlc:
        return "DLC";
    case Type::PsmGame:
        return "PSM game";
    case Type::PsxGame:
        return "PSX game";
    case Type::PspGame:
        return "PSP game";
    case Type::CompPack:
        return "Comp Pack";
    }
    return "unknown";
}

Downloader::Downloader()
    : _cond("downloader_cond"), _thread("downloader_thread", [this] { run(); })
{
    LOG("new downloader");
}

Downloader::~Downloader()
{
    LOG("destroying downloader");
    _dying = true;
    _cond.notify_one();
    _thread.join();
    LOG("downloader destroyed");
}

void Downloader::add(const DownloadItem& d)
{
    LOG("adding download %s", d.name.c_str());
    {
        ScopeLock _(_cond.get_mutex());
        _queue.push_back(d);
    }
    _cond.notify_one();
}

bool Downloader::is_in_queue(const std::string& contentid)
{
    ScopeLock _(_cond.get_mutex());
    if (contentid == _current_download.content)
        return true;

    for (const auto& item : _queue)
        if (item.content == contentid)
            return true;
    return false;
}

std::optional<DownloadItem> Downloader::get_current_download()
{
    ScopeLock _(_cond.get_mutex());
    if (_current_download.content.empty())
        return std::nullopt;
    return _current_download;
}

std::tuple<uint64_t, uint64_t> Downloader::get_current_download_progress()
{
    return {_download_offset.load(), _download_size.load()};
}

void Downloader::remove_from_queue(const std::string& contentid)
{
    ScopeLock _(_cond.get_mutex());
    if (contentid == _current_download.content)
        _cancel_current = true;
    else
        _queue.erase(
                std::remove_if(
                        _queue.begin(),
                        _queue.end(),
                        [&](auto const& item) {
                            return item.content == contentid;
                        }),
                _queue.end());
}

void Downloader::run()
{
    while (true)
    {
        DownloadItem item;
        {
            ScopeLock _(_cond.get_mutex());

            _current_download = {};
            _cancel_current = false;
            _download_offset = 0;
            _download_size = 0;

            if (_dying)
                return;
            else if (!_queue.empty())
            {
                item = _current_download = _queue.front();
                _queue.pop_front();
            }
            else
                _cond.wait();
        }

        try
        {
            if (!item.content.empty())
                do_download(item);
        }
        catch (const std::exception& e)
        {
            LOG("download error: %s", e.what());
            error(e.what());
        }
    }
}

void Downloader::do_download_package(const DownloadItem& item)
{
    BOOST_SCOPE_EXIT_ALL(&)
    {
        refresh(item.content);
    };

    ScopeProcessLock _;
    LOG("downloading %s", item.name.c_str());
    auto download = std::make_unique<Download>(std::make_unique<VitaHttp>());
    download->save_as_iso = item.save_as_iso;
    download->update_progress_cb = [this](uint64_t download_offset,
                                          uint64_t download_size) {
        _download_offset = download_offset;
        _download_size = download_size;
    };
    download->update_status = [](auto&&) {};
    download->is_canceled = [this] { return _cancel_current || _dying; };
    if (!download->pkgi_download(
                item.partition.c_str(),
                item.content.c_str(),
                item.url.c_str(),
                item.rif.empty() ? nullptr : item.rif.data(),
                item.digest.empty() ? nullptr : item.digest.data()))
        return;
    LOG("download of %s completed!", item.name.c_str());
    switch (item.type)
    {
    case Game:
    case Dlc:
        pkgi_install(item.content.c_str());
        break;
    case Update:
        pkgi_install_update(item.content.c_str());
        break;
    case PspGame:
        if (item.save_as_iso)
            pkgi_install_pspgame_as_iso(
                    item.partition.c_str(), item.content.c_str());
        else
            pkgi_install_pspgame(item.partition.c_str(), item.content.c_str());
        break;
    case PsmGame:
        pkgi_install_psmgame(item.content.c_str());
        break;
    case PsxGame:
        pkgi_install_pspgame(item.partition.c_str(), item.content.c_str());
        break;
    case CompPack:
        throw std::runtime_error(
                "assertion failure: can't handle comppack in download_package");
    }
    pkgi_rm(fmt::format("{}pkgi/{}.resume", item.partition, item.content)
                    .c_str());
    pkgi_delete_dir(fmt::format("{}pkgi/{}", item.partition, item.content));
    LOG("install of %s completed!", item.name.c_str());
}

void Downloader::do_download_comppack(const DownloadItem& item)
{
    ScopeProcessLock _;
    LOGF("downloading comppack {}", item.url);
    auto download =
            std::make_unique<FileDownload>(std::make_unique<VitaHttp>());

    download->update_progress_cb = [this](uint64_t download_offset,
                                          uint64_t download_size) {
        _download_offset = download_offset;
        _download_size = download_size;
    };
    download->is_canceled = [this] { return _cancel_current || _dying; };

    download->download(
            item.partition.c_str(), item.content.c_str(), item.url.c_str());
    LOGF("download of comppack {} completed!", item.url);
    pkgi_install_comppack(item.content.c_str());
    pkgi_rm(fmt::format("{}pkgi/{}-comp.ppk", item.partition, item.content)
                    .c_str());
    LOG("install of %s completed!", item.name.c_str());
}

void Downloader::do_download(const DownloadItem& item)
{
    if (item.type == CompPack)
        do_download_comppack(item);
    else
        do_download_package(item);
}
