#pragma once

#include "comppackdb.hpp"
#include "config.hpp"
#include "db.hpp"
#include "downloader.hpp"
#include "install.hpp"
#include "patchinfofetcher.hpp"
#include "imagefetcher.hpp"

#include <optional>

class GameView
{
public:
    GameView(
            const Config* config,
            Downloader* downloader,
            DbItem* item,
            std::optional<CompPackDatabase::Item> base_comppack,
            std::optional<CompPackDatabase::Item> patch_comppack);

    const DbItem* get_item() const
    {
        return _item;
    }

    void render();
    void refresh();

    bool is_closed() const
    {
        return _closed;
    }

    void close()
    {
        _closed = true;
    }

private:
    const Config* _config;
    Downloader* _downloader;

    DbItem* _item;
    std::optional<CompPackDatabase::Item> _base_comppack;
    std::optional<CompPackDatabase::Item> _patch_comppack;

    bool _refood_present;
    bool _0syscall6_present;
    std::string _game_version;
    CompPackVersion _comppack_versions;

    bool _closed{false};

    PatchInfoFetcher _patch_info_fetcher;
    ImageFetcher _image_fetcher;

    std::string get_min_system_version();
    void printDiagnostic();
    void start_download_package();
    void cancel_download_package();
    void start_download_patch(const PatchInfo& patch_info);
    void cancel_download_patch();
    void start_download_comppack(bool patch);
    void cancel_download_comppacks(bool patch);
};
