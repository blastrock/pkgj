#include "gameview.hpp"

#include <fmt/format.h>

#include "dialog.hpp"
#include "file.hpp"
#include "imgui.hpp"
extern "C"
{
#include "style.h"
}

namespace
{
constexpr unsigned GameViewWidth = VITA_WIDTH * 0.8;
constexpr unsigned GameViewHeight = VITA_HEIGHT * 0.8;
}

GameView::GameView(
        const Config* config,
        Downloader* downloader,
        DbItem* item,
        std::optional<CompPackDatabase::Item> base_comppack,
        std::optional<CompPackDatabase::Item> patch_comppack)
    : _config(config)
    , _downloader(downloader)
    , _item(item)
    , _base_comppack(base_comppack)
    , _patch_comppack(patch_comppack)
    , _patch_info_fetcher(item->titleid)
{
    refresh();
}

void GameView::render()
{
    ImGui::SetNextWindowPos(
            ImVec2((VITA_WIDTH - GameViewWidth) / 2,
                   (VITA_HEIGHT - GameViewHeight) / 2));
    ImGui::SetNextWindowSize(ImVec2(GameViewWidth, GameViewHeight), 0);

    ImGui::Begin(
            fmt::format("{} ({})###gameview", _item->name, _item->titleid)
                    .c_str(),
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse |
                    ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoInputs);

    ImGui::PushTextWrapPos(0.f);
    ImGui::Text(fmt::format("Firmware version: {}", pkgi_get_system_version())
                        .c_str());
    ImGui::Text(
            fmt::format(
                    "Required firmware version: {}", get_min_system_version())
                    .c_str());

    ImGui::Text(" ");

    ImGui::Text(fmt::format(
                        "Installed game version: {}",
                        _game_version.empty() ? "not installed" : _game_version)
                        .c_str());
    if (_comppack_versions.present && _comppack_versions.base.empty() &&
        _comppack_versions.patch.empty())
    {
        ImGui::Text("Installed compatibility pack: unknown version");
    }
    else
    {
        ImGui::Text(fmt::format(
                            "Installed base compatibility pack: {}",
                            _comppack_versions.base.empty() ? "no" : "yes")
                            .c_str());
        ImGui::Text(fmt::format(
                            "Installed patch compatibility pack version: {}",
                            _comppack_versions.patch.empty()
                                    ? "none"
                                    : _comppack_versions.patch)
                            .c_str());
    }

    ImGui::Text(" ");

    printDiagnostic();

    ImGui::Text(" ");

    ImGui::PopTextWrapPos();

    if (_patch_info_fetcher.get_status() == PatchInfoFetcher::Status::Found)
    {
        if (ImGui::Button("Install game and patch###installgame"))
            start_download_package();
    }
    else
    {
        if (ImGui::Button("Install game###installgame"))
            start_download_package();
    }

    switch (_patch_info_fetcher.get_status())
    {
    case PatchInfoFetcher::Status::Fetching:
        ImGui::Button("Checking for patch...###installpatch");
        break;
    case PatchInfoFetcher::Status::NoUpdate:
        ImGui::Button("No patch found###installpatch");
        break;
    case PatchInfoFetcher::Status::Found:
    {
        const auto patch_info = _patch_info_fetcher.get_patch_info();
        if (!_downloader->is_in_queue(Patch, _item->titleid))
        {
            if (ImGui::Button(fmt::format(
                                      "Install patch {}###installpatch",
                                      patch_info->version)
                                      .c_str()))
                start_download_patch(*patch_info);
        }
        else
        {
            if (ImGui::Button("Cancel patch installation###installpatch"))
                cancel_download_patch();
        }
        break;
    }
    case PatchInfoFetcher::Status::Error:
        ImGui::Button("Failed to fetch patch information###installpatch");
        break;
    }

    if (_base_comppack)
    {
        if (!_downloader->is_in_queue(CompPackBase, _item->titleid))
        {
            if (ImGui::Button("Install base compatibility "
                              "pack###installbasecomppack"))
                start_download_comppack(false);
        }
        else
        {
            if (ImGui::Button("Cancel base compatibility pack "
                              "installation###installbasecomppack"))
                cancel_download_comppacks(false);
        }
    }
    if (_patch_comppack)
    {
        if (!_downloader->is_in_queue(CompPackPatch, _item->titleid))
        {
            if (ImGui::Button(fmt::format(
                                      "Install compatibility pack "
                                      "{}###installpatchcommppack",
                                      _patch_comppack->app_version)
                                      .c_str()))
                start_download_comppack(true);
        }
        else
        {
            if (ImGui::Button("Cancel patch compatibility pack "
                              "installation###installpatchcommppack"))
                cancel_download_comppacks(true);
        }
    }

    if (ImGui::Button("Close"))
        _closed = true;

    ImGui::End();
}

static const auto Red = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
static const auto Yellow = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
static const auto Green = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);

void GameView::printDiagnostic()
{
    bool ok = true;
    auto const printError = [&](auto const& str) {
        ok = false;
        ImGui::TextColored(Red, str);
    };

    auto const systemVersion = pkgi_get_system_version();
    auto const minSystemVersion = get_min_system_version();

    ImGui::Text("Diagnostic:");

    if (systemVersion < minSystemVersion)
    {
        if (!_comppack_versions.present)
        {
            if (!_refood_present)
                printError(
                        "- Your firmware is too old to play this game, you "
                        "must install the compatibility pack or reF00D");
            else
                ImGui::Text(
                        "- This game will work thanks to reF00D, install the "
                        "compatibility packs for faster game boot times");
        }
    }
    else
    {
        ImGui::Text(
                "- Your firmware is recent enough, no need for compatibility "
                "packs");
    }

    if (_comppack_versions.present && _comppack_versions.base.empty() &&
        _comppack_versions.patch.empty())
    {
        ImGui::TextColored(
                Yellow,
                "- A compatibility pack is installed but not by PKGj, please "
                "make sure it matches the installed version or reinstall it "
                "with PKGj");
        ok = false;
    }

    if (_comppack_versions.base.empty() && !_comppack_versions.patch.empty())
        printError(
                "- You have installed an update compatibility pack without "
                "installing the base pack, install the base pack first and "
                "reinstall the update compatibility pack.");

    std::string comppack_version;
    if (!_comppack_versions.patch.empty())
        comppack_version = _comppack_versions.patch;
    else if (!_comppack_versions.base.empty())
        comppack_version = _comppack_versions.base;

    if (_item->presence == PresenceInstalled && !comppack_version.empty() &&
        comppack_version < _game_version)
        printError(
                "- The version of the game does not match the installed "
                "compatibility pack. If you have updated the game, also "
                "install the update compatibility pack.");

    if (_item->presence == PresenceInstalled &&
        comppack_version > _game_version)
        printError(
                "- The version of the game does not match the installed "
                "compatibility pack. Downgrade to the base compatibility "
                "pack or update the game through the Live Area.");

    if (_item->presence != PresenceInstalled)
    {
        ImGui::Text("- Game not installed");
        ok = false;
    }

    if (ok)
        ImGui::TextColored(Green, "All green");
}

std::string GameView::get_min_system_version()
{
    auto const patchInfo = _patch_info_fetcher.get_patch_info();
    if (patchInfo)
        return patchInfo->fw_version;
    else
        return _item->fw_version;
}

void GameView::refresh()
{
    LOGF("refreshing gameview");
    _refood_present = pkgi_file_exists("ur0:tai/keys.bin");
    _game_version = pkgi_get_game_version(_item->titleid);
    _comppack_versions = pkgi_get_comppack_versions(_item->titleid);
}

void GameView::start_download_package()
{
    if (_item->presence == PresenceInstalled)
    {
        LOGF("[{}] {} - already installed", _item->titleid, _item->name);
        pkgi_dialog_error("Already installed");
        return;
    }

    pkgi_start_download(*_downloader, *_item);

    _item->presence = PresenceUnknown;
}

void GameView::cancel_download_package()
{
    _downloader->remove_from_queue(Game, _item->content);
    _item->presence = PresenceUnknown;
}

void GameView::start_download_patch(const PatchInfo& patch_info)
{
    _downloader->add(DownloadItem{Patch,
                                  _item->name,
                                  _item->titleid,
                                  patch_info.url,
                                  std::vector<uint8_t>{},
                                  // TODO sha1 check
                                  std::vector<uint8_t>{},
                                  false,
                                  "ux0:",
                                  ""});
}

void GameView::cancel_download_patch()
{
    _downloader->remove_from_queue(Patch, _item->titleid);
}

void GameView::start_download_comppack(bool patch)
{
    const auto& entry = patch ? _patch_comppack : _base_comppack;

    _downloader->add(DownloadItem{patch ? CompPackPatch : CompPackBase,
                                  _item->name,
                                  _item->titleid,
                                  _config->comppack_url + entry->path,
                                  std::vector<uint8_t>{},
                                  std::vector<uint8_t>{},
                                  false,
                                  "ux0:",
                                  entry->app_version});
}

void GameView::cancel_download_comppacks(bool patch)
{
    _downloader->remove_from_queue(
            patch ? CompPackPatch : CompPackBase, _item->titleid);
}
