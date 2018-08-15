#include "pkgi.hpp"

extern "C" {
#include "dialog.h"
#include "style.h"
#include "utils.h"
#include "zrif.h"
}
#include "comppackdb.hpp"
#include "config.hpp"
#include "db.hpp"
#include "download.hpp"
#include "downloader.hpp"
#include "menu.hpp"
#include "vitahttp.hpp"

#include <fmt/format.h>

#include <memory>

#include <cstddef>
#include <cstring>

#define PKGI_UPDATE_URL \
    "https://api.github.com/repos/blastrock/pkgj/releases/latest"

typedef enum {
    StateError,
    StateRefreshing,
    StateCPRefreshing,
    StateMain,
} State;

static State state = StateMain;
static Mode mode = ModeGames;

static uint32_t first_item;
static uint32_t selected_item;

static int search_active;

static const char* current_url = nullptr;

static Config config;
static Config config_temp;

static int font_height;
static int avail_height;
static int bottom_y;

static char search_text[256];
static char error_state[256];

static std::unique_ptr<TitleDatabase> db;
static std::unique_ptr<CompPackDatabase> comppack_db;

static void pkgi_open_db();

static const char* pkgi_get_ok_str(void)
{
    return pkgi_ok_button() == PKGI_BUTTON_X ? PKGI_UTF8_X : PKGI_UTF8_O;
}

static const char* pkgi_get_cancel_str(void)
{
    return pkgi_cancel_button() == PKGI_BUTTON_O ? PKGI_UTF8_O : PKGI_UTF8_X;
}

static void configure_db(
        TitleDatabase* db, const char* search, const Config* config)
{
    try
    {
        db->reload(
                config->filter,
                config->sort,
                config->order,
                search ? search : "");
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

static void pkgi_refresh_thread(void)
{
    LOG("starting update");
    const char* url = current_url;
    try
    {
        auto const http = std::make_unique<VitaHttp>();
        db->update(http.get(), url);
        first_item = 0;
        selected_item = 0;
        configure_db(db.get(), NULL, &config);
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

static void pkgi_refresh_comppack_thread()
{
    LOG("starting update comppack");
    try
    {
        auto const http = std::make_unique<VitaHttp>();
        if (mode == ModeUpdates)
            comppack_db->update(
                    http.get(), config.comppack_url + "entries_patch.txt");
        else
            comppack_db->update(
                    http.get(), config.comppack_url + "entries.txt");
    }
    catch (const std::exception& e)
    {
        pkgi_dialog_error(
                fmt::format("failed to refresh comppack db:\n{}", e.what())
                        .c_str());
    }
    state = StateMain;
}

static const char* pkgi_get_mode_partition()
{
    return mode == ModePspGames || mode == ModePsxGames
                   ? config.install_psp_psx_location.c_str()
                   : "ux0:";
}

static std::string modeToDbName(Mode mode)
{
    switch (mode)
    {
    case ModeGames:
        return "pkgj_games.db";
    case ModeDlcs:
        return "pkgj_dlcs.db";
    case ModeUpdates:
        return "pkgj_updates.db";
    case ModePsmGames:
        return "pkgj_psmgames.db";
    case ModePspGames:
        return "pkgj_pspgames.db";
    case ModePsxGames:
        return "pkgj_psxgames.db";
    }
    throw std::runtime_error(
            fmt::format("unknown mode: {}", static_cast<int>(mode)));
}

static void pkgi_start_download(Downloader& downloader)
{
    DbItem* item = db->get(selected_item);

    LOG("decoding zRIF");

    // Just use the maximum size to be safe
    uint8_t rif[PKGI_PSM_RIF_SIZE];
    char message[256];
    if (item->zrif.empty() ||
        pkgi_zrif_decode(item->zrif.c_str(), rif, message, sizeof(message)))
    {
        downloader.add(DownloadItem{
                static_cast<Type>(mode),
                item->name,
                item->content,
                item->url,
                item->zrif.empty()
                        ? std::vector<uint8_t>{}
                        : std::vector<uint8_t>(rif, rif + PKGI_PSM_RIF_SIZE),
                item->has_digest
                        ? std::vector<uint8_t>(
                                  item->digest.begin(), item->digest.end())
                        : std::vector<uint8_t>{},
                !config.install_psp_as_pbp,
                pkgi_get_mode_partition()});
    }
    else
    {
        pkgi_dialog_error(message);
    }

    item->presence = PresenceUnknown;
    state = StateMain;
}

static void pkgi_start_download_comppack(Downloader& downloader)
{
    DbItem* item = db->get(selected_item);

    // HACK: comppack are identified by their titleid instead of content id
    if (downloader.is_in_queue(item->titleid))
    {
        downloader.remove_from_queue(item->titleid);
        return;
    }

    if ((mode == ModeGames && item->presence != PresenceInstalled) ||
        (mode == ModeUpdates && item->presence != PresenceInstalled))
    {
        LOGF("{} is not installed", item->content);
        return;
    }

    const auto entry = comppack_db->get(item->titleid);
    if (!entry)
    {
        pkgi_dialog_error(
                fmt::format("No compatibility pack found for {}", item->titleid)
                        .c_str());
        return;
    }

    const auto app_version = fmt::format("{:0>5}", item->app_version);
    if (!item->app_version.empty() && entry->app_version != app_version)
    {
        pkgi_dialog_error(fmt::format(
                                  "No compatibility pack found for {}, version "
                                  "{}, got {}",
                                  item->titleid,
                                  app_version,
                                  entry->app_version)
                                  .c_str());
        return;
    }

    downloader.add(DownloadItem{CompPack,
                                item->name,
                                item->titleid,
                                config.comppack_url + entry->path,
                                std::vector<uint8_t>{},
                                std::vector<uint8_t>{},
                                false,
                                "ux0:"});

    state = StateMain;
}

static void pkgi_friendly_size(char* text, uint32_t textlen, int64_t size)
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

static std::string const& pkgi_get_url_from_mode(Mode mode)
{
    switch (mode)
    {
    case ModeGames:
        return config.games_url;
    case ModeUpdates:
        return config.updates_url;
    case ModeDlcs:
        return config.dlcs_url;
    case ModePsmGames:
        return config.psm_games_url;
    case ModePspGames:
        return config.psp_games_url;
    case ModePsxGames:
        return config.psx_games_url;
    }
    throw std::runtime_error(
            fmt::format("unknown mode: {}", static_cast<int>(mode)));
}

static void pkgi_set_mode(Mode set_mode)
{
    mode = set_mode;
    pkgi_open_db();
}

static void pkgi_refresh_list()
{
    state = StateRefreshing;
    current_url = pkgi_get_url_from_mode(mode).c_str();
    pkgi_start_thread("refresh_thread", &pkgi_refresh_thread);
}

static void pkgi_refresh_comppack()
{
    state = StateCPRefreshing;
    pkgi_start_thread("refresh_thread", &pkgi_refresh_comppack_thread);
}

static void pkgi_do_main(Downloader& downloader, pkgi_input* input)
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
                if (pkgi_is_installed(titleid))
                    item->presence = PresenceInstalled;
                else if (downloader.is_in_queue(item->content))
                    item->presence = PresenceInstalling;
                break;
            case ModePsmGames:
                if (pkgi_psm_is_installed(titleid))
                    item->presence = PresenceInstalled;
                else if (downloader.is_in_queue(item->content))
                    item->presence = PresenceInstalling;
                break;
            case ModePspGames:
                if (pkgi_psp_is_installed(
                            pkgi_get_mode_partition(), item->content.c_str()))
                    item->presence = PresenceInstalled;
                else if (downloader.is_in_queue(item->content))
                    item->presence = PresenceInstalling;
                break;
            case ModePsxGames:
                if (pkgi_psx_is_installed(
                            pkgi_get_mode_partition(), item->content.c_str()))
                    item->presence = PresenceInstalled;
                else if (downloader.is_in_queue(item->content))
                    item->presence = PresenceInstalling;
                break;
            case ModeDlcs:
                if (downloader.is_in_queue(item->content))
                    item->presence = PresenceInstalling;
                else if (pkgi_dlc_is_installed(item->content.c_str()))
                    item->presence = PresenceInstalled;
                else if (pkgi_is_installed(titleid))
                    item->presence = PresenceGamePresent;
                break;
            case ModeUpdates:
                if (downloader.is_in_queue(item->content))
                    item->presence = PresenceInstalling;
                else if (pkgi_update_is_installed(
                                 item->titleid, item->app_version))
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
        pkgi_draw_text(col_name, y, color, item->name.c_str());
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

        if (selected_item >= db->count())
            return;
        DbItem* item = db->get(selected_item);

        if (downloader.is_in_queue(item->content))
        {
            downloader.remove_from_queue(item->content);
            item->presence = PresenceUnknown;
            return;
        }

        switch (mode)
        {
        case ModeGames:
        case ModePsmGames:
        case ModePsxGames:
        case ModePspGames:
            if (item->presence == PresenceInstalled)
            {
                LOGF("[{}] {} - already installed", item->titleid, item->name);
                pkgi_dialog_error("Already installed");
                return;
            }
            break;
        case ModeDlcs:
        case ModeUpdates:
            if (item->presence == PresenceInstalled)
            {
                LOGF("[{}] {} - already installed", item->content, item->name);
                pkgi_dialog_error("Already installed");
                return;
            }
            if (item->presence != PresenceGamePresent)
            {
                LOGF("[{}] {} - game not installed", item->titleid, item->name);
                pkgi_dialog_error("Corresponding game not installed");
                return;
            }
            break;
        }
        LOGF("[{}] {} - starting to install", item->content, item->name);
        pkgi_start_download(downloader);
    }
    else if (input && (input->pressed & PKGI_BUTTON_LT))
    {
        if (mode != ModeGames && mode != ModeUpdates)
            return;

        input->pressed &= ~PKGI_BUTTON_LT;

        if (selected_item >= db->count())
            return;

        pkgi_start_download_comppack(downloader);
    }
    else if (input && (input->pressed & PKGI_BUTTON_T))
    {
        input->pressed &= ~PKGI_BUTTON_T;

        config_temp = config;
        int allow_refresh = !config.games_url.empty() << 0 |
                            !config.updates_url.empty() << 1 |
                            !config.dlcs_url.empty() << 2 |
                            !config.psx_games_url.empty() << 3 |
                            !config.psp_games_url.empty() << 4 |
                            !config.psm_games_url.empty() << 5;
        pkgi_menu_start(search_active, &config, allow_refresh);
    }
}

static void pkgi_do_refresh(void)
{
    char text[256];

    uint32_t updated;
    uint32_t total;
    db->get_update_status(&updated, &total);

    if (total == 0)
    {
        pkgi_snprintf(
                text,
                sizeof(text),
                "Refreshing... %.2f KB",
                (uint32_t)updated / 1024.f);
    }
    else
    {
        pkgi_snprintf(
                text,
                sizeof(text),
                "Refreshing... %u%%",
                updated * 100U / total);
    }

    int w = pkgi_text_width(text);
    pkgi_draw_text(
            (VITA_WIDTH - w) / 2, VITA_HEIGHT / 2, PKGI_COLOR_TEXT, text);
}

static void pkgi_do_refresh_comppack()
{
    const auto text = "Downloading compatibility packs database...";

    int w = pkgi_text_width(text);
    pkgi_draw_text(
            (VITA_WIDTH - w) / 2, VITA_HEIGHT / 2, PKGI_COLOR_TEXT, text);
}

static void pkgi_do_head(void)
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

static uint64_t last_progress_time;
static uint64_t last_progress_offset;
static uint64_t last_progress_speed;

static uint64_t get_speed(const uint64_t download_offset)
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

static void pkgi_do_tail(Downloader& downloader)
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

    // get free space of partition only if looking at psx or psp games else show
    // ux0:
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
    if (pkgi_menu_is_open())
    {
        bottom_text = fmt::format(
                "{} select  " PKGI_UTF8_T " close  {} cancel",
                pkgi_get_ok_str(),
                pkgi_get_cancel_str());
    }
    else
    {
        DbItem* item = db->get(selected_item);
        if ((mode == ModeGames || mode == ModeUpdates) && item &&
            item->presence == PresenceInstalled)
            bottom_text = fmt::format(
                    "L {} ",
                    downloader.is_in_queue(item->titleid)
                            ? "cancel comp pack"
                            : "install comp pack");
        if (item && item->presence == PresenceInstalling)
            bottom_text += fmt::format("{} cancel ", pkgi_get_ok_str());
        else if (item && item->presence != PresenceInstalled)
            bottom_text += fmt::format("{} install ", pkgi_get_ok_str());
        bottom_text += PKGI_UTF8_T " menu";
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

static void pkgi_do_error(void)
{
    pkgi_draw_text(
            (VITA_WIDTH - pkgi_text_width(error_state)) / 2,
            VITA_HEIGHT / 2,
            PKGI_COLOR_TEXT_ERROR,
            error_state);
}

static void reposition(void)
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

static void pkgi_reload()
{
    try
    {
        configure_db(db.get(), NULL, &config);
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

static void pkgi_open_db()
{
    try
    {
        first_item = 0;
        selected_item = 0;
        // can't allocate two databases at the same time, a database class is
        // 16MB at the moment, doesn't seem huge to me, but let's work around
        // that
        db = nullptr;
        db = std::make_unique<TitleDatabase>(
                mode,
                std::string(pkgi_get_config_folder()) + '/' +
                        modeToDbName(mode));

        if (mode == ModeUpdates)
            comppack_db = std::make_unique<CompPackDatabase>(
                    std::string(pkgi_get_config_folder()) +
                    "/comppack_updates.db");
        else
            comppack_db = std::make_unique<CompPackDatabase>(
                    std::string(pkgi_get_config_folder()) + "/comppack.db");
    }
    catch (const std::exception& e)
    {
        LOGF("error during database open: {}", e.what());
        throw formatEx<std::runtime_error>(
                "DB initialization error: %s\nTry to delete them?");
    }

    pkgi_reload();
}

int main()
{
    pkgi_start();

    try
    {
        if (!pkgi_is_unsafe_mode())
            throw std::runtime_error(
                    "pkgj requires unsafe mode to be enabled in Henkaku "
                    "settings!");

        Downloader downloader;

        downloader.refresh = [](const std::string& content) {
            // FIXME this runs on the wrong thread
            const auto item = db->get_by_content(content.c_str());
            if (!item)
            {
                LOGF("couldn't find {}", content);
                return;
            }
            item->presence = PresenceUnknown;
        };
        downloader.error = [](const std::string& error) {
            // FIXME this runs on the wrong thread
            pkgi_dialog_error(("Download failure: " + error).c_str());
        };

        LOG("started");

        config = pkgi_load_config();
        current_url = config.games_url.c_str();
        pkgi_dialog_init();

        font_height = pkgi_text_height("M");
        avail_height = VITA_HEIGHT - 3 * (font_height + PKGI_MAIN_HLINE_EXTRA);
        bottom_y = VITA_HEIGHT - 2 * font_height - PKGI_MAIN_ROW_PADDING;

        pkgi_open_db();

        pkgi_texture background = pkgi_load_png(background);

        pkgi_input input;
        while (pkgi_update(&input))
        {
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

            case StateCPRefreshing:
                pkgi_do_refresh_comppack();
                break;

            case StateMain:
                pkgi_do_main(
                        downloader,
                        pkgi_dialog_is_open() || pkgi_menu_is_open() ? NULL
                                                                     : &input);
                break;
            }

            pkgi_do_tail(downloader);

            if (pkgi_dialog_is_open())
            {
                pkgi_do_dialog(&input);
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
                    case MenuResultRefreshCompPack:
                        pkgi_refresh_comppack();
                        break;
                    case MenuResultShowGames:
                        pkgi_set_mode(ModeGames);
                        break;
                    case MenuResultShowUpdates:
                        pkgi_set_mode(ModeUpdates);
                        break;
                    case MenuResultShowDlcs:
                        pkgi_set_mode(ModeDlcs);
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
                    }
                }
            }

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
