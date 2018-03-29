#include "pkgi_downloader.hpp"

#include "pkgi_scoped_exit.hpp"

#include "pkgi_download.hpp"

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
    case Type::PsxGame:
        return "PSX game";
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

float Downloader::get_current_download_progress()
{
    return _progress.load();
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
            _progress = 0.0f;
            _cancel_current = false;

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
            LOG("error: %s", e.what());
        }
    }
}

void Downloader::do_download(const DownloadItem& item)
{
    ScopedExit scoped_exit_1([&]()
    {
        refresh(item.content);
    });

    ScopeProcessLock _;
    LOG("downloading %s", item.name.c_str());
    auto download = std::make_unique<Download>();
    download->update_progress_cb = [this](const Download& d) {
        _progress = float(d.download_offset) / float(d.download_size);
    };
    download->update_status = [](auto&&) {};
    download->is_canceled = [this] { return _cancel_current || _dying; };
    if (!download->pkgi_download(
                item.content.c_str(),
                item.url.c_str(),
                item.rif.empty() ? nullptr : item.rif.data(),
                item.digest.data()))
        return;
    LOG("download of %s completed!", item.name.c_str());
    switch (item.type)
    {
    case Game:
    case Dlc:
        if (!pkgi_install(item.content.c_str()))
            return;
        break;
    case Update:
        if (!pkgi_install_update(item.content.c_str()))
            return;
        break;
    case PsxGame:
        if (!pkgi_install_psxgame(item.content.c_str()))
            return;
        break;
    }
    LOG("install of %s completed!", item.name.c_str());
}
