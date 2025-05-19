#include "pkgi.hpp"

extern "C"
{
#include "style.h"
}
#include "bgdl.hpp"
#include "comppackdb.hpp"
#include "config.hpp"
#include "db.hpp"
#include "dialog.hpp"
#include "download.hpp"
#include "downloader.hpp"
#include "file.hpp"
#include "gameview.hpp"
#include "imgui.hpp"
#include "install.hpp"
#include "menu.hpp"
#include "psm.hpp"
#include "update.hpp"
#include "utils.hpp"
#include "vitahttp.hpp"
#include "zrif.hpp"

#include <vita2d.h>

#include <fmt/format.h>

#include <memory>
#include <set>

#include <psp2/io/stat.h>
#include <psp2common/npdrm.h>
#include <psp2/io/stat.h>

#include <cstddef>
#include <cstring>

#define PKGI_UPDATE_URL \
    "https://api.github.com/repos/blastrock/pkgj/releases/latest"

namespace
{
typedef enum
{
    StateError,
    StateRefreshing,
    StateMain,
} State;

State state = StateMain;
Mode mode = ModeGames;

uint32_t first_item;
uint32_t selected_item;

int search_active;
int selected_active;

Config config;
Config config_temp;

int font_height;
int avail_height;
int bottom_y;

char search_text[256];
char error_state[256];

std::vector<DbItem> items_list;

// used for multiple things actually
Mutex refresh_mutex("refresh_mutex");
std::string current_action;
std::unique_ptr<TitleDatabase> db;
std::unique_ptr<CompPackDatabase> comppack_db_games;
std::unique_ptr<CompPackDatabase> comppack_db_updates;

std::set<std::string> installed_games;
std::set<std::string> installed_themes;

std::unique_ptr<GameView> gameview;
bool need_refresh = true;
bool runtime_install_queued = false;
std::string content_to_refresh;
void pkgi_reload();

const char* pkgi_get_ok_str(void)
{
    return pkgi_ok_button() == PKGI_BUTTON_X ? PKGI_UTF8_X : PKGI_UTF8_O;
}

const char* pkgi_get_cancel_str(void)
{
    return pkgi_cancel_button() == PKGI_BUTTON_O ? PKGI_UTF8_O : PKGI_UTF8_X;
}

Type mode_to_type(Mode mode)
{
    switch (mode)
    {
    case ModeGames:
        return Game;
    case ModeDlcs:
        return Dlc;
    case ModePsmGames:
        return PsmGame;
    case ModePsxGames:
        return PsxGame;
    case ModePspGames:
        return PspGame;
    case ModePspDlcs:
        return PspDlc;
    case ModeDemos:
    case ModeThemes:
        throw formatEx<std::runtime_error>(
                "unsupported mode {}", static_cast<int>(mode));
    }
    throw formatEx<std::runtime_error>(
            "unknown mode {}", static_cast<int>(mode));
}

BgdlType mode_to_bgdl_type(Mode mode)
{
    switch (mode)
    {
    case ModePspGames:
    case ModePsxGames:
    case ModePspDlcs:
        return BgdlTypePsp;
    case ModePsmGames:
        return BgdlTypePsm;
    case ModeGames:
    case ModeDemos:
        return BgdlTypeGame;
    case ModeDlcs:
        return BgdlTypeDlc;
    case ModeThemes:
        return BgdlTypeTheme;
    default:
        throw formatEx<std::runtime_error>(
                "unsupported bgdl mode {}", static_cast<int>(mode));
    }
}

void configure_db(TitleDatabase* db, const char* search, const Config* config)
{
    try
    {
        db->reload(
                mode,
                mode == ModeGames || mode == ModeDlcs
                        ? config->filter
                        : config->filter & ~DbFilterInstalled,
                config->sort,
                config->order,
                search ? search : "",
                installed_games);
    }
    catch (const std::exception& e)
    {
        snprintf(
                error_state,
                sizeof(error_state),
                "can't reload list: %s",
                e.what());
        pkgi_dialog_error(error_state);
    }
}

std::string const& pkgi_get_url_from_mode(Mode mode)
{
    switch (mode)
    {
    case ModeGames:
        return config.games_url;
    case ModeDlcs:
        return config.dlcs_url;
    case ModeDemos:
        return config.demos_url;
    case ModeThemes:
        return config.themes_url;
    case ModePsmGames:
        return config.psm_games_url;
    case ModePspGames:
        return config.psp_games_url;
    case ModePspDlcs:
        return config.psp_dlcs_url;
    case ModePsxGames:
        return config.psx_games_url;
    }
    throw std::runtime_error(
            fmt::format("unknown mode: {}", static_cast<int>(mode)));
}

void pkgi_refresh_thread(void)
{
    LOG("starting update");
    try
    {
        auto mode_count = ModeCount + (config.comppack_url.empty() ? 0 : 2);

        ScopeProcessLock lock;
        for (int i = 0; i < ModeCount; ++i)
        {
            const auto mode = static_cast<Mode>(i);
            auto const url = pkgi_get_url_from_mode(mode);
            if (url.empty())
                continue;
            {
                std::lock_guard<Mutex> lock(refresh_mutex);
                current_action = fmt::format(
                        "Refreshing {} [{}/{}]",
                        pkgi_mode_to_string(mode),
                        i + 1,
                        mode_count);
            }
            auto const http = std::make_unique<VitaHttp>();
            db->update(mode, http.get(), url);
        }
        if (!config.comppack_url.empty())
        {
            {
                std::lock_guard<Mutex> lock(refresh_mutex);
                current_action = fmt::format(
                        "Refreshing games compatibility packs [{}/{}]",
                        mode_count - 1,
                        mode_count);
            }
            {
                auto const http = std::make_unique<VitaHttp>();
                comppack_db_games->update(
                        http.get(), config.comppack_url + "entries.txt");
            }
            {
                std::lock_guard<Mutex> lock(refresh_mutex);
                current_action = fmt::format(
                        "Refreshing updates compatibility packs [{}/{}]",
                        mode_count,
                        mode_count);
            }
            {
                auto const http = std::make_unique<VitaHttp>();
                comppack_db_updates->update(
                        http.get(), config.comppack_url + "entries_patch.txt");
            }
        }
        first_item = 0;
        selected_item = 0;
        configure_db(db.get(), search_active ? search_text : NULL, &config);
    }
    catch (const std::exception& e)
    {
        snprintf(
                error_state,
                sizeof(error_state),
                "can't get list: %s",
                e.what());
        pkgi_dialog_error(error_state);
    }
    state = StateMain;
}

const char* pkgi_get_mode_partition()
{
    return mode == ModePspGames || mode == ModePspDlcs || mode == ModePsxGames
                   ? config.install_psp_psx_location.c_str()
                   : "ux0:";
}

void pkgi_refresh_installed_packages()
{
    auto games = pkgi_get_installed_games();
    installed_games.clear();
    installed_games.insert(
            std::make_move_iterator(games.begin()),
            std::make_move_iterator(games.end()));

    auto themes = pkgi_get_installed_themes();
    installed_themes.clear();
    installed_themes.insert(
            std::make_move_iterator(themes.begin()),
            std::make_move_iterator(themes.end()));
}

bool pkgi_is_installed(const char* titleid)
{
    return installed_games.find(titleid) != installed_games.end();
}

bool pkgi_theme_is_installed(std::string contentid)
{
    if (contentid.size() < 19)
        return false;

    contentid.erase(16, 3);
    contentid.erase(0, 7);
    return installed_themes.find(contentid) != installed_themes.end();
}

void do_download(Downloader& downloader, DbItem* item)
{
    pkgi_start_download(downloader, *item);
    item->presence = PresenceUnknown;
}

void pkgi_install_package(Downloader& downloader, DbItem* item)
{
    if (item->presence == PresenceInstalled)
    {
        LOGF("[{}] {} - already installed", item->content, item->name);
        pkgi_dialog_question(
                fmt::format(
                        "{} is already installed."
                        "Would you like to redownload it?",
                        item->name)
                        .c_str(),
                {{"Redownload.",
                  [&downloader, item] { do_download(downloader, item); }},
                 {"Dont Redownload.", [] {}}});
        return;
    }

    do_download(downloader, item);
}

void pkgi_friendly_size(char* text, uint32_t textlen, int64_t size)
{
    if (size <= 0)
    {
        text[0] = 0;
    }
    else if (size < 1000LL)
    {
        pkgi_snprintf(text, textlen, "%u " PKGI_UTF8_B, (uint32_t)size);
    }
    else if (size < 1000LL * 1000)
    {
        pkgi_snprintf(text, textlen, "%.2f " PKGI_UTF8_KB, size / 1024.f);
    }
    else if (size < 1000LL * 1000 * 1000)
    {
        pkgi_snprintf(
                text, textlen, "%.2f " PKGI_UTF8_MB, size / 1024.f / 1024.f);
    }
    else
    {
        pkgi_snprintf(
                text,
                textlen,
                "%.2f " PKGI_UTF8_GB,
                size / 1024.f / 1024.f / 1024.f);
    }
}

void pkgi_set_mode(Mode set_mode)
{
    mode = set_mode;
    pkgi_reload();
    first_item = 0;
    selected_item = 0;
}

void pkgi_refresh_list()
{
    state = StateRefreshing;
    pkgi_start_thread("refresh_thread", &pkgi_refresh_thread);
}

void pkgi_do_main(Downloader& downloader, pkgi_input* input)
{
    int col_titleid = 0;
    int col_region = col_titleid + pkgi_text_width("PCSE00000") +
                     PKGI_MAIN_COLUMN_PADDING;
    int col_installed =
            col_region + pkgi_text_width("USA") + PKGI_MAIN_COLUMN_PADDING;
    int col_name = col_installed + pkgi_text_width(PKGI_UTF8_INSTALLED) +
                   PKGI_MAIN_COLUMN_PADDING;

    uint32_t db_count = db->count();

    if (input)
    {
        if (input->active & PKGI_BUTTON_UP)
        {
            if (selected_item == first_item && first_item > 0)
            {
                first_item--;
                selected_item = first_item;
            }
            else if (selected_item > 0)
            {
                selected_item--;
            }
            else if (selected_item == 0)
            {
                selected_item = db_count - 1;
                uint32_t max_items =
                        avail_height / (font_height + PKGI_MAIN_ROW_PADDING) -
                        1;
                first_item =
                        db_count > max_items ? db_count - max_items - 1 : 0;
            }
        }

        if (input->active & PKGI_BUTTON_DOWN)
        {
            uint32_t max_items =
                    avail_height / (font_height + PKGI_MAIN_ROW_PADDING) - 1;
            if (selected_item == db_count - 1)
            {
                selected_item = first_item = 0;
            }
            else if (selected_item == first_item + max_items)
            {
                first_item++;
                selected_item++;
            }
            else
            {
                selected_item++;
            }
        }

        if (input->active & PKGI_BUTTON_LEFT)
        {
            uint32_t max_items =
                    avail_height / (font_height + PKGI_MAIN_ROW_PADDING) - 1;
            if (first_item < max_items)
            {
                first_item = 0;
            }
            else
            {
                first_item -= max_items;
            }
            if (selected_item < max_items)
            {
                selected_item = 0;
            }
            else
            {
                selected_item -= max_items;
            }
        }

        if (input->active & PKGI_BUTTON_RIGHT)
        {
            uint32_t max_items =
                    avail_height / (font_height + PKGI_MAIN_ROW_PADDING) - 1;
            if (first_item + max_items < db_count - 1)
            {
                first_item += max_items;
                selected_item += max_items;
                if (selected_item >= db_count)
                {
                    selected_item = db_count - 1;
                }
            }
        }
    }

    int y = font_height + PKGI_MAIN_HLINE_EXTRA;
    int line_height = font_height + PKGI_MAIN_ROW_PADDING;
    for (uint32_t i = first_item; i < db_count; i++)
    {
        DbItem* item = db->get(i);

        uint32_t color = PKGI_COLOR_TEXT;

        const auto titleid = item->titleid.c_str();

        if (item->presence == PresenceUnknown)
        {
            switch (mode)
            {
            case ModeGames:
            case ModeDemos:
                if (pkgi_is_installed(titleid))
                    item->presence = PresenceInstalled;
                else if (downloader.is_in_queue(Game, item->content))
                    item->presence = PresenceInstalling;
                break;
            case ModePsmGames:
                if (pkgi_psm_is_installed(titleid))
                    item->presence = PresenceInstalled;
                else if (downloader.is_in_queue(PsmGame, item->content))
                    item->presence = PresenceInstalling;
                break;
            case ModePspDlcs:
                if (pkgi_psp_is_installed(
                            pkgi_get_mode_partition(), item->content.c_str()))
                    item->presence = PresenceGamePresent;
                else if (downloader.is_in_queue(PspGame, item->content))
                    item->presence = PresenceInstalling;
                break;
            case ModePspGames:
                if (pkgi_psp_is_installed(
                            pkgi_get_mode_partition(), item->content.c_str()))
                    item->presence = PresenceInstalled;
                else if (downloader.is_in_queue(PspGame, item->content))
                    item->presence = PresenceInstalling;
                break;
            case ModePsxGames:
                if (pkgi_psx_is_installed(
                            pkgi_get_mode_partition(), item->content.c_str()))
                    item->presence = PresenceInstalled;
                else if (downloader.is_in_queue(PsxGame, item->content))
                    item->presence = PresenceInstalling;
                break;
            case ModeDlcs:
                if (downloader.is_in_queue(Dlc, item->content))
                    item->presence = PresenceInstalling;
                else if (pkgi_dlc_is_installed(item->content.c_str()))
                    item->presence = PresenceInstalled;
                else if (pkgi_is_installed(titleid))
                    item->presence = PresenceGamePresent;
                break;
            case ModeThemes:
                if (pkgi_theme_is_installed(item->content))
                    item->presence = PresenceInstalled;
                else if (pkgi_is_installed(titleid))
                    item->presence = PresenceGamePresent;
                break;
            }

            if (item->presence == PresenceUnknown)
            {
                if (pkgi_is_incomplete(
                            pkgi_get_mode_partition(), item->content.c_str()))
                    item->presence = PresenceIncomplete;
                else
                    item->presence = PresenceMissing;
            }
        }

        char size_str[64];
        pkgi_friendly_size(size_str, sizeof(size_str), item->size);
        int sizew = pkgi_text_width(size_str);

        pkgi_clip_set(0, y, VITA_WIDTH, line_height);

        if (i == selected_item)
        {
            pkgi_draw_rect(
                    0,
                    y,
                    VITA_WIDTH,
                    font_height + PKGI_MAIN_ROW_PADDING - 1,
                    PKGI_COLOR_SELECTED_BACKGROUND);
        }

        pkgi_draw_text(col_titleid, y, color, titleid);
        const char* region;
        switch (pkgi_get_region(item->titleid))
        {
        case RegionASA:
            region = "ASA";
            break;
        case RegionEUR:
            region = "EUR";
            break;
        case RegionJPN:
            region = "JPN";
            break;
        case RegionINT:
            region = "INT";
            break;
        case RegionUSA:
            region = "USA";
            break;
        default:
            region = "???";
            break;
        }
        pkgi_draw_text(col_region, y, color, region);
        if (item->presence == PresenceIncomplete)
        {
            pkgi_draw_text(col_installed, y, color, PKGI_UTF8_PARTIAL);
        }
        else if (item->presence == PresenceInstalled)
        {
            pkgi_draw_text(col_installed, y, color, PKGI_UTF8_INSTALLED);
        }
        else if (item->presence == PresenceGamePresent)
        {
            pkgi_draw_text(
                    col_installed,
                    y,
                    PKGI_COLOR_GAME_PRESENT,
                    PKGI_UTF8_INSTALLED);
        }
        else if (item->presence == PresenceInstalling)
        {
            pkgi_draw_text(col_installed, y, color, PKGI_UTF8_INSTALLING);
        }
        pkgi_draw_text(
                VITA_WIDTH - PKGI_MAIN_SCROLL_WIDTH - PKGI_MAIN_SCROLL_PADDING -
                        sizew,
                y,
                color,
                size_str);
        pkgi_clip_remove();

        pkgi_clip_set(
                col_name,
                y,
                VITA_WIDTH - PKGI_MAIN_SCROLL_WIDTH - PKGI_MAIN_SCROLL_PADDING -
                        PKGI_MAIN_COLUMN_PADDING - sizew - col_name,
                line_height);

        selected_active = !items_list.empty();
        item->selected =
                std::find_if(
                        items_list.begin(),
                        items_list.end(),
                        [item](const DbItem& it)
                        { return item->url == it.url; }) != items_list.end();
        pkgi_draw_text(
                col_name,
                y,
                item->selected ? PKGI_COLOR_TEXT_SELECTED : PKGI_COLOR_TEXT,
                item->name.c_str());
        pkgi_clip_remove();

        y += font_height + PKGI_MAIN_ROW_PADDING;
        if (y > VITA_HEIGHT - (2 * font_height + PKGI_MAIN_HLINE_EXTRA))
        {
            break;
        }
        else if (
                y + font_height >
                VITA_HEIGHT - (2 * font_height + PKGI_MAIN_HLINE_EXTRA))
        {
            line_height =
                    (VITA_HEIGHT - (2 * font_height + PKGI_MAIN_HLINE_EXTRA)) -
                    (y + 1);
            if (line_height < PKGI_MAIN_ROW_PADDING)
            {
                break;
            }
        }
    }

    if (db_count == 0)
    {
        const char* text = "No items! Try to refresh.";

        int w = pkgi_text_width(text);
        pkgi_draw_text(
                (VITA_WIDTH - w) / 2, VITA_HEIGHT / 2, PKGI_COLOR_TEXT, text);
    }

    // scroll-bar
    if (db_count != 0)
    {
        uint32_t max_items =
                (avail_height + font_height + PKGI_MAIN_ROW_PADDING - 1) /
                        (font_height + PKGI_MAIN_ROW_PADDING) -
                1;
        if (max_items < db_count)
        {
            uint32_t min_height = PKGI_MAIN_SCROLL_MIN_HEIGHT;
            uint32_t height = max_items * avail_height / db_count;
            uint32_t start =
                    first_item *
                    (avail_height - (height < min_height ? min_height : 0)) /
                    db_count;
            height = max32(height, min_height);
            pkgi_draw_rect(
                    VITA_WIDTH - PKGI_MAIN_SCROLL_WIDTH - 1,
                    font_height + PKGI_MAIN_HLINE_EXTRA + start,
                    PKGI_MAIN_SCROLL_WIDTH,
                    height,
                    PKGI_COLOR_SCROLL_BAR);
        }
    }

    if (input && (input->pressed & pkgi_ok_button()))
    {
        input->pressed &= ~pkgi_ok_button();
        if (items_list.empty())
        {
            if (selected_item >= db->count())
                return;
            DbItem* item = db->get(selected_item);

            if (mode == ModeGames)
                gameview = std::make_unique<GameView>(
                        &config,
                        &downloader,
                        item,
                        comppack_db_games->get(item->titleid),
                        comppack_db_updates->get(item->titleid));
            else if (mode == ModeThemes || mode == ModeDemos)
            {
                pkgi_start_download(downloader, *item);
            }
            else if (mode == ModeDlcs)
            {
                if (downloader.is_in_queue(mode_to_type(mode), item->content))
                {
                    downloader.remove_from_queue(
                            mode_to_type(mode), item->content);
                    item->presence = PresenceUnknown;
                }
                else
                    pkgi_install_package(downloader, item);
            }
            else
            {
                if (downloader.is_in_queue(mode_to_type(mode), item->content))
                {
                    downloader.remove_from_queue(
                            mode_to_type(mode), item->content);
                    item->presence = PresenceUnknown;
                }
                else
                    pkgi_install_package(downloader, item);
            }
        }
        else
        {
            for (DbItem& item : items_list)
            {
                vita2d_start_drawing();

                ImGui::NewFrame();

                ImGui::SetNextWindowPos(
                        ImVec2{VITA_WIDTH / 2, VITA_HEIGHT / 2},
                        0,
                        ImVec2{.5f, .5f});

                ImGui::Begin(
                        "",
                        nullptr,
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoScrollWithMouse |
                                ImGuiWindowFlags_NoCollapse |
                                ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_NoTitleBar);

                ImGui::Text("Please wait...");
                ImGui::End();

                ImGui::EndFrame();
                ImGui::Render();
                pkgi_imgui_render(ImGui::GetDrawData());
                pkgi_swap();

                switch (item.mode)
                {
                case ModeGames:
                    gameview = std::make_unique<GameView>(
                            &config,
                            &downloader,
                            &item,
                            comppack_db_games->get(item.titleid),
                            comppack_db_updates->get(item.titleid));
                    if (gameview)
                    {
                        while (!gameview->is_closed())
                        {
                            pkgi_input input;
                            if (pkgi_update(&input))
                            {
                                ImGuiIO& io = ImGui::GetIO();
                                io.DeltaTime = 1.0f / 60.0f;
                                io.DisplaySize.x = VITA_WIDTH;
                                io.DisplaySize.y = VITA_HEIGHT;

                                io.AddKeyEvent(
                                        ImGuiKey_GamepadDpadUp,
                                        input.pressed & PKGI_BUTTON_UP);
                                io.AddKeyEvent(
                                        ImGuiKey_GamepadDpadDown,
                                        input.pressed & PKGI_BUTTON_DOWN);
                                io.AddKeyEvent(
                                        ImGuiKey_GamepadDpadLeft,
                                        input.pressed & PKGI_BUTTON_LEFT);
                                io.AddKeyEvent(
                                        ImGuiKey_GamepadDpadRight,
                                        input.pressed & PKGI_BUTTON_RIGHT);
                                io.AddKeyEvent(
                                        ImGuiKey_GamepadFaceDown,
                                        input.pressed & pkgi_ok_button());
                                if (input.pressed & pkgi_cancel_button())
                                    gameview->close();

                                input.active = 0;
                                input.pressed = 0;

                                ImGui::NewFrame();

                                gameview->render();

                                ImGui::EndFrame();
                                ImGui::Render();
                                pkgi_imgui_render(ImGui::GetDrawData());
                                pkgi_swap();
                            }
                        }
                    }
                    break;
                case ModeThemes:
                case ModeDemos:
                    pkgi_start_download(downloader, item);
                    break;
                case ModeDlcs:
                    if (downloader.is_in_queue(
                                mode_to_type(item.mode), item.content))
                    {
                        downloader.remove_from_queue(
                                mode_to_type(item.mode), item.content);
                        item.presence = PresenceUnknown;
                    }
                    else
                        pkgi_install_package(downloader, &item);
                    break;
                default:
                    if (downloader.is_in_queue(
                                mode_to_type(item.mode), item.content))
                    {
                        downloader.remove_from_queue(
                                mode_to_type(item.mode), item.content);
                        item.presence = PresenceUnknown;
                    }
                    else
                        pkgi_install_package(downloader, &item);
                    break;
                }
            }
            vita2d_start_drawing();
        }
    }
    else if (input && (input->pressed & PKGI_BUTTON_S))
    {
        input->pressed &= ~PKGI_BUTTON_S;
        DbItem* item = db->get(selected_item);
        if (item->selected)
        {
            items_list.erase(std::find_if(
                    items_list.begin(),
                    items_list.end(),
                    [item](const DbItem& it) { return item->url == it.url; }));
        }
        else if (items_list.size() < 32 - pkgi_list_dir_contents("ux0:bgdl/t").size())
        {
            items_list.push_back(*item);
        }
    }
    else if (input && (input->pressed & PKGI_BUTTON_T))
    {
        input->pressed &= ~PKGI_BUTTON_T;

        config_temp = config;
        int allow_refresh = !config.games_url.empty() << 0 |
                            !config.dlcs_url.empty() << 1 |
                            !config.demos_url.empty() << 6 |
                            !config.themes_url.empty() << 5 |
                            !config.psx_games_url.empty() << 2 |
                            !config.psp_games_url.empty() << 3 |
                            !config.psp_dlcs_url.empty() << 7 |
                            !config.psm_games_url.empty() << 4;
        pkgi_menu_start(search_active, selected_active, &config, allow_refresh);
    }
}

void pkgi_do_refresh(void)
{
    std::string text;

    uint32_t updated;
    uint32_t total;
    db->get_update_status(&updated, &total);

    if (total == 0)
        text = fmt::format("{}...", current_action);
    else
        text = fmt::format("{}... {}%", current_action, updated * 100 / total);

    int w = pkgi_text_width(text.c_str());
    pkgi_draw_text(
            (VITA_WIDTH - w) / 2,
            VITA_HEIGHT / 2,
            PKGI_COLOR_TEXT,
            text.c_str());
}

void pkgi_do_head(void)
{
    const char* version = PKGI_VERSION;

    char title[256];
    pkgi_snprintf(title, sizeof(title), "PKGj v%s", version);
    pkgi_draw_text(0, 0, PKGI_COLOR_TEXT_HEAD, title);

    pkgi_draw_rect(
            0,
            font_height,
            VITA_WIDTH,
            PKGI_MAIN_HLINE_HEIGHT,
            PKGI_COLOR_HLINE);

    int rightw;
    if (pkgi_battery_present())
    {
        char battery[256];
        pkgi_snprintf(
                battery,
                sizeof(battery),
                "Battery: %u%%",
                pkgi_bettery_get_level());

        uint32_t color;
        if (pkgi_battery_is_low())
        {
            color = PKGI_COLOR_BATTERY_LOW;
        }
        else if (pkgi_battery_is_charging())
        {
            color = PKGI_COLOR_BATTERY_CHARGING;
        }
        else
        {
            color = PKGI_COLOR_TEXT_HEAD;
        }

        rightw = pkgi_text_width(battery);
        pkgi_draw_text(
                VITA_WIDTH - PKGI_MAIN_HLINE_EXTRA - rightw, 0, color, battery);
    }
    else
    {
        rightw = 0;
    }

    char text[256];
    int left = pkgi_text_width(search_text) + PKGI_MAIN_TEXT_PADDING;
    int right = rightw + PKGI_MAIN_TEXT_PADDING;

    if (search_active)
        pkgi_snprintf(
                text,
                sizeof(text),
                "%s >> %s <<",
                pkgi_mode_to_string(mode).c_str(),
                search_text);
    else
        pkgi_snprintf(
                text, sizeof(text), "%s", pkgi_mode_to_string(mode).c_str());

    pkgi_clip_set(
            left,
            0,
            VITA_WIDTH - right - left,
            font_height + PKGI_MAIN_HLINE_EXTRA);
    pkgi_draw_text(
            (VITA_WIDTH - pkgi_text_width(text)) / 2,
            0,
            PKGI_COLOR_TEXT_TAIL,
            text);
    pkgi_clip_remove();
}

uint64_t last_progress_time;
uint64_t last_progress_offset;
uint64_t last_progress_speed;

uint64_t get_speed(const uint64_t download_offset)
{
    const uint64_t now = pkgi_time_msec();
    const uint64_t progress_time = now - last_progress_time;
    if (progress_time < 1000)
        return last_progress_speed;

    const uint64_t progress_data = download_offset - last_progress_offset;
    last_progress_speed = progress_data * 1000 / progress_time;
    last_progress_offset = download_offset;
    last_progress_time = now;

    return last_progress_speed;
}

void pkgi_do_tail(Downloader& downloader)
{
    char text[256];

    pkgi_draw_rect(
            0, bottom_y, VITA_WIDTH, PKGI_MAIN_HLINE_HEIGHT, PKGI_COLOR_HLINE);

    const auto current_download = downloader.get_current_download();

    uint64_t download_offset;
    uint64_t download_size;
    std::tie(download_offset, download_size) =
            downloader.get_current_download_progress();
    // avoid divide by 0
    if (download_size == 0)
        download_size = 1;

    pkgi_draw_rect(
            0,
            bottom_y + PKGI_MAIN_HLINE_HEIGHT,
            VITA_WIDTH * download_offset / download_size,
            font_height + PKGI_MAIN_ROW_PADDING - 1,
            PKGI_COLOR_PROGRESS_BACKGROUND);

    if (current_download)
    {
        const auto speed = get_speed(download_offset);
        std::string sspeed;

        if (speed > 1000 * 1024)
            sspeed = fmt::format("{:.3g} MB/s", speed / 1024.f / 1024.f);
        else if (speed > 1000)
            sspeed = fmt::format("{:.3g} KB/s", speed / 1024.f);
        else
            sspeed = fmt::format("{} B/s", speed);

        pkgi_snprintf(
                text,
                sizeof(text),
                "Downloading %s: %s (%s, %d%%)",
                type_to_string(current_download->type).c_str(),
                current_download->name.c_str(),
                sspeed.c_str(),
                static_cast<int>(download_offset * 100 / download_size));
    }
    else
        pkgi_snprintf(text, sizeof(text), "Idle");

    pkgi_draw_text(0, bottom_y, PKGI_COLOR_TEXT_TAIL, text);
    if (selected_active)
    {
        pkgi_snprintf(
                text,
                sizeof(text),
                "Selected items: %d/%d",
                items_list.size(),
                32 - pkgi_list_dir_contents("ux0:bgdl/t").size());
        pkgi_draw_text(
                (VITA_WIDTH - pkgi_text_width(text)) / 2,
                bottom_y,
                PKGI_COLOR_TEXT_TAIL,
                text);
    }
    const auto second_line = bottom_y + font_height + PKGI_MAIN_ROW_PADDING;

    uint32_t count = db->count();
    uint32_t total = db->total();

    if (count == total)
    {
        pkgi_snprintf(text, sizeof(text), "Count: %u", count);
    }
    else
    {
        pkgi_snprintf(text, sizeof(text), "Count: %u (%u)", count, total);
    }
    pkgi_draw_text(0, second_line, PKGI_COLOR_TEXT_TAIL, text);

    // get free space of partition only if looking at psx or psp games else
    // show ux0:
    char size[64];
    if (mode == ModePsxGames || mode == ModePspGames)
    {
        pkgi_friendly_size(
                size,
                sizeof(size),
                pkgi_get_free_space(pkgi_get_mode_partition()));
    }
    else
    {
        pkgi_friendly_size(size, sizeof(size), pkgi_get_free_space("ux0:"));
    }

    char free[64];
    pkgi_snprintf(free, sizeof(free), "Free: %s", size);

    int rightw = pkgi_text_width(free);
    pkgi_draw_text(
            VITA_WIDTH - PKGI_MAIN_HLINE_EXTRA - rightw,
            second_line,
            PKGI_COLOR_TEXT_TAIL,
            free);

    int left = pkgi_text_width(text) + PKGI_MAIN_TEXT_PADDING;
    int right = rightw + PKGI_MAIN_TEXT_PADDING;

    std::string bottom_text;
    if (gameview || pkgi_dialog_is_open())
    {
        bottom_text = fmt::format(
                "{} select {} close", pkgi_get_ok_str(), pkgi_get_cancel_str());
    }
    else if (pkgi_menu_is_open())
    {
        bottom_text = fmt::format(
                "{} select  " PKGI_UTF8_T " close  {} cancel",
                pkgi_get_ok_str(),
                pkgi_get_cancel_str());
    }
    else
    {
        if (mode == ModeGames)
            bottom_text += fmt::format("{} details ", pkgi_get_ok_str());
        else
        {
            DbItem* item = db->get(selected_item);
            if (item && item->presence == PresenceInstalling)
                bottom_text += fmt::format("{} cancel ", pkgi_get_ok_str());
            else if (item && item->presence != PresenceInstalled)
                bottom_text += fmt::format("{} install ", pkgi_get_ok_str());
        }
        bottom_text += PKGI_UTF8_T " menu ";
        bottom_text += PKGI_UTF8_S " select";
    }

    pkgi_clip_set(
            left,
            second_line,
            VITA_WIDTH - right - left,
            VITA_HEIGHT - second_line);
    pkgi_draw_text(
            (VITA_WIDTH - pkgi_text_width(bottom_text.c_str())) / 2,
            second_line,
            PKGI_COLOR_TEXT_TAIL,
            bottom_text.c_str());
    pkgi_clip_remove();
}

void pkgi_do_error(void)
{
    pkgi_draw_text(
            (VITA_WIDTH - pkgi_text_width(error_state)) / 2,
            VITA_HEIGHT / 2,
            PKGI_COLOR_TEXT_ERROR,
            error_state);
}

void reposition(void)
{
    uint32_t count = db->count();
    if (first_item + selected_item < count)
    {
        return;
    }

    uint32_t max_items = (avail_height + font_height + PKGI_MAIN_ROW_PADDING -
                          1) / (font_height + PKGI_MAIN_ROW_PADDING) -
                         1;
    if (count > max_items)
    {
        uint32_t delta = selected_item - first_item;
        first_item = count - max_items;
        selected_item = first_item + delta;
    }
    else
    {
        first_item = 0;
        selected_item = 0;
    }
}

void pkgi_reload()
{
    try
    {
        configure_db(db.get(), search_active ? search_text : NULL, &config);
    }
    catch (const std::exception& e)
    {
        LOGF("error during reload: {}", e.what());
        pkgi_dialog_error(
                fmt::format(
                        "failed to reload db: {}, try to refresh?", e.what())
                        .c_str());
    }
}

void pkgi_open_db()
{
    try
    {
        first_item = 0;
        selected_item = 0;
        db = std::make_unique<TitleDatabase>(pkgi_get_config_folder());

        comppack_db_games = std::make_unique<CompPackDatabase>(
                std::string(pkgi_get_config_folder()) + "/comppack.db");
        comppack_db_updates = std::make_unique<CompPackDatabase>(
                std::string(pkgi_get_config_folder()) + "/comppack_updates.db");
    }
    catch (const std::exception& e)
    {
        LOGF("error during database open: {}", e.what());
        throw formatEx<std::runtime_error>(
                "DB initialization error: %s\nTry to delete them?");
    }

    pkgi_reload();
}
}

void pkgi_create_psp_rif(std::string contentid, uint8_t* rif)
{
    SceNpDrmLicense license;
    memset(&license, 0x00, sizeof(SceNpDrmLicense));
    license.account_id = 0x0123456789ABCDEFLL;
    memset(license.ecdsa_signature, 0xFF, 0x28);
    strncpy(license.content_id, contentid.c_str(), 0x30);

    memcpy(rif, &license, PKGI_PSP_RIF_SIZE);
}

void pkgi_download_psm_runtime_if_needed()
{
    if (!pkgi_is_installed("PCSI00011") && !runtime_install_queued)
    {

        uint8_t rif[PKGI_PSM_RIF_SIZE];
        char message[256];
        pkgi_zrif_decode(
                PSM_RUNTIME_DRMFREE_LICENSE, rif, message, sizeof(message));

        pkgi_start_bgdl(
                BgdlTypeGame,
                "PlayStation Mobile Runtime Package",
                "http://ares.dl.playstation.net/psm-runtime/"
                "IP9100-PCSI00011_00-PSMRUNTIME000000.pkg",
                std::vector<uint8_t>(rif, rif + PKGI_PSM_RIF_SIZE));

        runtime_install_queued = true;
    }
}

void pkgi_start_download(Downloader& downloader, const DbItem& item)
{
    LOGF("[{}] {} - starting to install", item.content, item.name);

    sceIoMkdir("ux0:bgdl", 0777);

    try
    {
        // download PSM Runtime if a PSM game is requested to be installed ..
        if (mode == ModePsmGames)
            pkgi_download_psm_runtime_if_needed();
        // Just use the maximum size to be safe
        uint8_t rif[PKGI_PSM_RIF_SIZE];
        char message[256];
        if (item.zrif.empty() ||
            pkgi_zrif_decode(item.zrif.c_str(), rif, message, sizeof(message)))
        {
            if (mode == ModeGames || mode == ModeDlcs || mode == ModeDemos ||
                mode == ModeThemes || // Vita contents
                (MODE_IS_PSPEMU(mode) &&
                 pkgi_is_module_present("NoPspEmuDrm_kern")) || // Psp Contents
                (mode == ModePsmGames &&
                 pkgi_is_module_present("NoPsmDrm")) // Psm Contents
            )
            {

                if (MODE_IS_PSPEMU(mode))
                {
                    pkgi_create_psp_rif(item.content, rif);
                }

                pkgi_start_bgdl(
                        mode_to_bgdl_type(mode),
                        item.name,
                        item.url,
                        std::vector<uint8_t>(rif, rif + PKGI_PSM_RIF_SIZE));
                pkgi_dialog_message(
                        fmt::format(
                                "Installation of {} queued in LiveArea",
                                item.name)
                                .c_str());
            }
            else
            {
                downloader.add(DownloadItem{
                        mode_to_type(mode),
                        item.name,
                        item.content,
                        item.url,
                        item.zrif.empty()
                                ? std::vector<uint8_t>{}
                                : std::vector<uint8_t>(
                                          rif, rif + PKGI_PSM_RIF_SIZE),
                        item.has_digest ? std::vector<uint8_t>(
                                                  item.digest.begin(),
                                                  item.digest.end())
                                        : std::vector<uint8_t>{},
                        !config.install_psp_as_pbp,
                        pkgi_get_mode_partition(),
                        ""});
            }
        }
        else
        {
            pkgi_dialog_error(message);
        }
    }
    catch (const std::exception& e)
    {
        pkgi_dialog_error(
                fmt::format("Failed to install {}: {}", item.name, e.what())
                        .c_str());
    }
}

int main()
{
    pkgi_start();

    try
    {
        if (!pkgi_is_unsafe_mode())
            throw std::runtime_error(
                    "PKGj requires unsafe mode to be enabled in HENkaku "
                    "settings!");

        Downloader downloader;

        downloader.refresh = [](const std::string& content)
        {
            std::lock_guard<Mutex> lock(refresh_mutex);
            content_to_refresh = content;
            need_refresh = true;
        };
        downloader.error = [](const std::string& error)
        {
            // FIXME this runs on the wrong thread
            pkgi_dialog_error(("Download failure: " + error).c_str());
        };

        LOG("started");

        config = pkgi_load_config();
        pkgi_dialog_init();

        font_height = pkgi_text_height("M");
        avail_height = VITA_HEIGHT - 3 * (font_height + PKGI_MAIN_HLINE_EXTRA);
        bottom_y = VITA_HEIGHT - 2 * font_height - PKGI_MAIN_ROW_PADDING;

        pkgi_open_db();

        pkgi_texture background = pkgi_load_png(background);

        if (!config.no_version_check)
            start_update_thread();

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

        // Build and load the texture atlas into a texture
        uint32_t* pixels = NULL;
        int width, height;
        if (!io.Fonts->AddFontFromFileTTF(
                    "sa0:/data/font/pvf/ltn0.pvf",
                    20.0f,
                    0,
                    io.Fonts->GetGlyphRangesDefault()))
            throw std::runtime_error("failed to load ltn0.pvf");
        if (!io.Fonts->AddFontFromFileTTF(
                    "sa0:/data/font/pvf/jpn0.pvf",
                    20.0f,
                    0,
                    io.Fonts->GetGlyphRangesJapanese()))
            throw std::runtime_error("failed to load jpn0.pvf");
        io.Fonts->GetTexDataAsRGBA32((uint8_t**)&pixels, &width, &height);
        vita2d_texture* font_texture =
                vita2d_create_empty_texture(width, height);
        const auto stride = vita2d_texture_get_stride(font_texture) / 4;
        auto texture_data = (uint32_t*)vita2d_texture_get_datap(font_texture);

        for (auto y = 0; y < height; ++y)
            for (auto x = 0; x < width; ++x)
                texture_data[y * stride + x] = pixels[y * width + x];

        io.Fonts->TexID = font_texture;

        init_imgui();

        pkgi_input input;
        while (pkgi_update(&input))
        {
            ImGuiIO& io = ImGui::GetIO();
            io.DeltaTime = 1.0f / 60.0f;
            io.DisplaySize.x = VITA_WIDTH;
            io.DisplaySize.y = VITA_HEIGHT;

            if (gameview || pkgi_dialog_is_open())
            {
                io.AddKeyEvent(
                        ImGuiKey_GamepadDpadUp, input.pressed & PKGI_BUTTON_UP);
                io.AddKeyEvent(
                        ImGuiKey_GamepadDpadDown,
                        input.pressed & PKGI_BUTTON_DOWN);
                io.AddKeyEvent(
                        ImGuiKey_GamepadDpadLeft,
                        input.pressed & PKGI_BUTTON_LEFT);
                io.AddKeyEvent(
                        ImGuiKey_GamepadDpadRight,
                        input.pressed & PKGI_BUTTON_RIGHT);
                io.AddKeyEvent(
                        ImGuiKey_GamepadFaceDown,
                        input.pressed & pkgi_ok_button());
                if (input.pressed & pkgi_cancel_button() && gameview)
                    gameview->close();

                input.active = 0;
                input.pressed = 0;
            }

            if (need_refresh)
            {
                std::lock_guard<Mutex> lock(refresh_mutex);
                pkgi_refresh_installed_packages();
                if (!content_to_refresh.empty())
                {
                    const auto item =
                            db->get_by_content(content_to_refresh.c_str());
                    if (item)
                        item->presence = PresenceUnknown;
                    else
                        LOGF("couldn't find {} for refresh",
                             content_to_refresh);
                    content_to_refresh.clear();
                }
                if (gameview)
                    gameview->refresh();
                need_refresh = false;
            }

            ImGui::NewFrame();

            pkgi_draw_texture(background, 0, 0);

            pkgi_do_head();
            switch (state)
            {
            case StateError:
                pkgi_do_error();
                break;

            case StateRefreshing:
                pkgi_do_refresh();
                break;

            case StateMain:
                pkgi_do_main(
                        downloader,
                        pkgi_dialog_is_open() || pkgi_menu_is_open() ? NULL
                                                                     : &input);
                break;
            }

            pkgi_do_tail(downloader);

            if (gameview)
            {
                gameview->render();
                if (gameview->is_closed())
                    gameview = nullptr;
            }

            if (pkgi_dialog_is_open())
            {
                pkgi_do_dialog();
            }

            if (pkgi_dialog_input_update())
            {
                search_active = 1;
                pkgi_dialog_input_get_text(search_text, sizeof(search_text));
                configure_db(db.get(), search_text, &config);
                reposition();
            }

            if (pkgi_menu_is_open())
            {
                if (pkgi_do_menu(&input))
                {
                    Config new_config;
                    pkgi_menu_get(&new_config);
                    if (config_temp.sort != new_config.sort ||
                        config_temp.order != new_config.order ||
                        config_temp.filter != new_config.filter)
                    {
                        config_temp = new_config;
                        configure_db(
                                db.get(),
                                search_active ? search_text : NULL,
                                &config_temp);
                        reposition();
                    }
                }
                else
                {
                    MenuResult mres = pkgi_menu_result();
                    switch (mres)
                    {
                    case MenuResultSearch:
                        pkgi_dialog_input_text("Search", search_text);
                        break;
                    case MenuResultSearchClear:
                        search_active = 0;
                        search_text[0] = 0;
                        configure_db(db.get(), NULL, &config);
                        break;
                    case MenuResultCancel:
                        if (config_temp.sort != config.sort ||
                            config_temp.order != config.order ||
                            config_temp.filter != config.filter)
                        {
                            configure_db(
                                    db.get(),
                                    search_active ? search_text : NULL,
                                    &config);
                            reposition();
                        }
                        break;
                    case MenuResultAccept:
                        pkgi_menu_get(&config);
                        pkgi_save_config(config);
                        break;
                    case MenuResultRefresh:
                        pkgi_refresh_list();
                        break;
                    case MenuResultShowGames:
                        pkgi_set_mode(ModeGames);
                        break;
                    case MenuResultShowDlcs:
                        pkgi_set_mode(ModeDlcs);
                        break;
                    case MenuResultShowDemos:
                        pkgi_set_mode(ModeDemos);
                        break;
                    case MenuResultShowThemes:
                        pkgi_set_mode(ModeThemes);
                        break;
                    case MenuResultShowPsmGames:
                        pkgi_set_mode(ModePsmGames);
                        break;
                    case MenuResultShowPsxGames:
                        pkgi_set_mode(ModePsxGames);
                        break;
                    case MenuResultShowPspGames:
                        pkgi_set_mode(ModePspGames);
                        break;
                    case MenuResultShowPspDlcs:
                        pkgi_set_mode(ModePspDlcs);
                        break;
                    case MenuResultSelectedClear:
                        items_list.clear();
                        break;
                    }
                }
            }

            ImGui::EndFrame();
            ImGui::Render();

            pkgi_imgui_render(ImGui::GetDrawData());

            pkgi_swap();
        }
    }
    catch (const std::exception& e)
    {
        LOGF("Error in main: {}", e.what());
        state = StateError;
        pkgi_snprintf(
                error_state, sizeof(error_state), "Fatal error: %s", e.what());

        pkgi_input input;
        while (pkgi_update(&input))
        {
            pkgi_draw_rect(0, 0, VITA_WIDTH, VITA_HEIGHT, 0);
            pkgi_do_error();
            pkgi_swap();
        }
        pkgi_end();
    }

    LOG("finished");
    pkgi_end();
}
