extern "C" {
#include "pkgi.h"
#include "pkgi_db.h"
#include "pkgi_dialog.h"
#include "pkgi_menu.h"
#include "pkgi_style.h"
#include "pkgi_utils.h"
#include "pkgi_zrif.h"
}
#include "pkgi_config.hpp"
#include "pkgi_download.hpp"
#include "pkgi_downloader.hpp"
#include <string.h>

#include <memory>

#include <stddef.h>

#define PKGI_UPDATE_URL \
    "https://api.github.com/repos/blastrock/pkgj/releases/latest"

typedef enum {
    StateError,
    StateRefreshing,
    StateUpdateDone,
    StateMain,
} State;

static State state;
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

static const char* pkgi_get_ok_str(void)
{
    return pkgi_ok_button() == PKGI_BUTTON_X ? PKGI_UTF8_X : PKGI_UTF8_O;
}

static const char* pkgi_get_cancel_str(void)
{
    return pkgi_cancel_button() == PKGI_BUTTON_O ? PKGI_UTF8_O : PKGI_UTF8_X;
}

static void pkgi_refresh_thread(void)
{
    LOG("starting update");
    const char* url = current_url;
    try
    {
        pkgi_db_update(url, mode);
        first_item = 0;
        selected_item = 0;
        state = StateUpdateDone;
    }
    catch (const std::exception& e)
    {
        state = StateError;
        snprintf(
                error_state,
                sizeof(error_state),
                "can't get list: %s",
                e.what());
    }
}

static void pkgi_start_download(Downloader& downloader)
{
    DbItem* item = pkgi_db_get(selected_item);

    LOG("decoding zRIF");

    uint8_t rif[PKGI_RIF_SIZE];
    char message[256];
    if (item->zrif == NULL ||
        pkgi_zrif_decode(item->zrif, rif, message, sizeof(message)))
    {
        downloader.add(DownloadItem{
                static_cast<Type>(pkgi_db_get_mode()),
                item->name,
                item->content,
                item->url,
                item->zrif == NULL
                        ? std::vector<uint8_t>{}
                        : std::vector<uint8_t>(rif, rif + PKGI_RIF_SIZE),
                item->digest == NULL
                        ? std::vector<uint8_t>{}
                        : std::vector<uint8_t>(
                                  item->digest,
                                  item->digest + SHA256_DIGEST_SIZE)});
    }
    else
    {
        pkgi_dialog_error(message);
    }

    item->presence = PresenceUnknown;
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

void RefreshGames(const char* url, Mode SetMode ){
    current_url = url;
    state = StateRefreshing;
    mode = SetMode;
    pkgi_start_thread("refresh_thread", &pkgi_refresh_thread);
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

    uint32_t db_count = pkgi_db_count();

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
	    else if (input->pressed & PKGI_BUTTON_SELECT)
	    {
			const auto current_download = downloader.get_current_download();
			if(!current_download){

		   		if(strcmp(pkgi_get_partition(),"ux0:") == 0){
		   			pkgi_set_partition_uma0();
		   		} else if(strcmp(pkgi_get_partition(),"ur0:") == 0){
		   			pkgi_set_partition_ux0();
		   		} else if(strcmp(pkgi_get_partition(),"uma0:") == 0){
		   			pkgi_set_partition_ur0();
		   		} else{
		   			pkgi_set_partition_ux0();
		   		}

				if(strcmp(current_url,config.psx_games_url.c_str())== 0){
					RefreshGames(config.psx_games_url.c_str(),ModePsxGames);
				}else if(strcmp(current_url,config.psp_games_url.c_str())== 0){
					RefreshGames(config.psp_games_url.c_str(),ModePspGames);
				}
			}// end !current_download
	   		else{
	   			LOG("Cant change partition Download in progress");
	   		}

	   }// end if (input->pressed & PKGI_BUTTON_SELECT)
    }

    int y = font_height + PKGI_MAIN_HLINE_EXTRA;
    int line_height = font_height + PKGI_MAIN_ROW_PADDING;
    for (uint32_t i = first_item; i < db_count; i++)
    {
        DbItem* item = pkgi_db_get(i);

        uint32_t color = PKGI_COLOR_TEXT;

        char titleid[10];
        pkgi_memcpy(titleid, item->content + 7, 9);
        titleid[9] = 0;

        if (item->presence == PresenceUnknown)
        {
            if (pkgi_db_get_mode() == ModeGames)
            {
                if (pkgi_is_installed(titleid))
                    item->presence = PresenceInstalled;
                else if (downloader.is_in_queue(item->content))
                    item->presence = PresenceInstalling;
                else if (pkgi_is_incomplete(item->content))
                    item->presence = PresenceIncomplete;
                else
                    item->presence = PresenceMissing;
            }
            else if (pkgi_db_get_mode() == ModePspGames)
            {
                if (pkgi_psp_is_installed(item->content))
                    item->presence = PresenceInstalled;
                else if (downloader.is_in_queue(item->content))
                    item->presence = PresenceInstalling;
                else if (pkgi_is_incomplete(item->content))
                    item->presence = PresenceIncomplete;
                else
                    item->presence = PresenceMissing;
            }
            else if (pkgi_db_get_mode() == ModePsxGames)
            {
                if (pkgi_psx_is_installed(item->content))
                    item->presence = PresenceInstalled;
                else if (downloader.is_in_queue(item->content))
                    item->presence = PresenceInstalling;
                else if (pkgi_is_incomplete(item->content))
                    item->presence = PresenceIncomplete;
                else
                    item->presence = PresenceMissing;
            }
            else
            {
                if (downloader.is_in_queue(item->content))
                    item->presence = PresenceInstalling;
                else if (
                        pkgi_db_get_mode() == ModeDlcs &&
                        pkgi_dlc_is_installed(item->content))
                    item->presence = PresenceInstalled;
                else if (pkgi_is_installed(titleid))
                    item->presence = PresenceGamePresent;
                else if (pkgi_is_incomplete(item->content))
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
        switch (pkgi_get_region(item->content))
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
        pkgi_draw_text(col_name, y, color, item->name);
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
        const char* text = "No items!";

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

        DbItem* item = pkgi_db_get(selected_item);

        if (downloader.is_in_queue(item->content))
        {
            downloader.remove_from_queue(item->content);
            item->presence = PresenceUnknown;
            return;
        }

        switch (pkgi_db_get_mode())
        {
        case ModeGames:
        case ModePsxGames:
        case ModePspGames:
            if (item->presence == PresenceInstalled)
            {
                LOG("[%.9s] %s - alreay installed",
                    item->content + 7,
                    item->name);
                pkgi_dialog_error("Already installed");
                return;
            }
            break;
        case ModeDlcs:
            if (item->presence == PresenceInstalled)
            {
                LOG("[%s] %s - alreay installed", item->content, item->name);
                pkgi_dialog_error("Already installed");
                return;
            }
            // fallthrough
        case ModeUpdates:
            if (item->presence != PresenceGamePresent)
            {
                LOG("[%.9s] %s - game not installed",
                    item->content + 7,
                    item->name);
                pkgi_dialog_error("Corresponding game not installed");
                return;
            }
            break;
        }
        LOG("[%s] %s - starting to install", item->content, item->name);
        pkgi_start_download(downloader);
    }
    else if (input && (input->pressed & PKGI_BUTTON_T))
    {
        input->pressed &= ~PKGI_BUTTON_T;

        config_temp = config;
        int allow_refresh = !config.games_url.empty() << 0 |
                            !config.updates_url.empty() << 1 |
                            !config.dlcs_url.empty() << 2 |
                            !config.psx_games_url.empty() << 3 |
                            !config.psp_games_url.empty() << 4;
        pkgi_menu_start(search_active, &config, allow_refresh);
    }
}

static void pkgi_do_refresh(void)
{
    char text[256];

    uint32_t updated;
    uint32_t total;
    pkgi_db_get_update_status(&updated, &total);

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

    if (search_active)
    {
        char text[256];
        int left = pkgi_text_width(search_text) + PKGI_MAIN_TEXT_PADDING;
        int right = rightw + PKGI_MAIN_TEXT_PADDING;

        pkgi_snprintf(text, sizeof(text), ">> %s <<", search_text);

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
}

static void pkgi_do_tail(Downloader& downloader)
{
    char text[256];

    pkgi_draw_rect(
            0, bottom_y, VITA_WIDTH, PKGI_MAIN_HLINE_HEIGHT, PKGI_COLOR_HLINE);

    const auto current_download = downloader.get_current_download();

    pkgi_draw_rect(
            0,
            bottom_y + PKGI_MAIN_HLINE_HEIGHT,
            int(VITA_WIDTH * downloader.get_current_download_progress()),
            font_height + PKGI_MAIN_ROW_PADDING - 1,
            PKGI_COLOR_PROGRESS_BACKGROUND);

    if (current_download)
        pkgi_snprintf(
                text,
                sizeof(text),
                "Downloading %s: %s",
                type_to_string(current_download->type).c_str(),
                current_download->name.c_str());
    else
        pkgi_snprintf(text, sizeof(text), "Idle");

    pkgi_draw_text(0, bottom_y, PKGI_COLOR_TEXT_TAIL, text);

    const auto second_line = bottom_y + font_height + PKGI_MAIN_ROW_PADDING;

    uint32_t count = pkgi_db_count();
    uint32_t total = pkgi_db_total();

    if (count == total)
    {
        pkgi_snprintf(text, sizeof(text), "Count: %u", count);
    }
    else
    {
        pkgi_snprintf(text, sizeof(text), "Count: %u (%u)", count, total);
    }
    pkgi_draw_text(0, second_line, PKGI_COLOR_TEXT_TAIL, text);

	//get free space of partition only if looking at psx or psp games else show ux
    char size[64];
	if(strcmp(current_url,config.psx_games_url.c_str())== 0 || strcmp(current_url,config.psp_games_url.c_str())== 0){
		pkgi_friendly_size(size, sizeof(size), pkgi_get_free_space(pkgi_get_partition()));
	}else{
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


	//show partition being used for psp / psx games
	pkgi_snprintf(text, sizeof(text), "PSP/PSX partition: %s",pkgi_get_partition());
	rightw = pkgi_text_width(text);
	pkgi_draw_text(
		VITA_WIDTH - PKGI_MAIN_HLINE_EXTRA - rightw,
		bottom_y,
		PKGI_COLOR_TEXT_TAIL,
		text);

		//only show if no file is being downloaded as text may sometimes overlap
	if(!current_download){
		pkgi_snprintf(text, sizeof(text), "Change Partition: select");
		rightw = pkgi_text_width(text);
		pkgi_draw_text(
			(VITA_WIDTH/2) - PKGI_MAIN_HLINE_EXTRA - (rightw/2),
			bottom_y,
			PKGI_COLOR_TEXT_TAIL,
			text);
	}


    int left = pkgi_text_width(text) + PKGI_MAIN_TEXT_PADDING;
    int right = rightw + PKGI_MAIN_TEXT_PADDING;

    if (pkgi_menu_is_open())
    {
        pkgi_snprintf(
                text,
                sizeof(text),
                "%s select  " PKGI_UTF8_T " close  %s cancel",
                pkgi_get_ok_str(),
                pkgi_get_cancel_str());
    }
    else
    {
        DbItem* item = pkgi_db_get(selected_item);
        pkgi_snprintf(
                text,
                sizeof(text),
                "%s %s  " PKGI_UTF8_T " menu",
                pkgi_get_ok_str(),
                item && item->presence == PresenceInstalling ? "cancel"
                                                             : "install");
    }

    pkgi_clip_set(
            left,
            second_line,
            VITA_WIDTH - right - left,
            VITA_HEIGHT - second_line);
    pkgi_draw_text(
            (VITA_WIDTH - pkgi_text_width(text)) / 2,
            second_line,
            PKGI_COLOR_TEXT_TAIL,
            text);
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
    uint32_t count = pkgi_db_count();
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

int main()
{
    pkgi_start();

    Downloader downloader;

    downloader.refresh = [](const std::string& content) {
        // FIXME this runs on the wrong thread
        const auto item = pkgi_db_get_by_content(content.c_str());
        if (!item)
        {
            LOG("couldn't find %s", content.c_str());
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

    if (pkgi_is_unsafe_mode())
    {
        state = StateRefreshing;
        pkgi_start_thread("refresh_thread", &pkgi_refresh_thread);
    }
    else
    {
        state = StateError;
        pkgi_snprintf(
                error_state,
                sizeof(error_state),
                "pkgi requires unsafe enabled in Henkaku settings!");
    }

    pkgi_texture background = pkgi_load_png(background);

    pkgi_input input;
    while (pkgi_update(&input))
    {
        pkgi_draw_texture(background, 0, 0);

        if (state == StateUpdateDone)
        {
            if (!config.no_version_check)
            {
                // pkgi_start_thread("update_thread", &pkgi_check_for_update);
            }

            pkgi_db_configure(NULL, &config);
            state = StateMain;
        }

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

        case StateUpdateDone:
            // never happens, just to shut up the compiler
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
            pkgi_db_configure(search_text, &config);
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
                    pkgi_db_configure(
                            search_active ? search_text : NULL, &config_temp);
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
                    pkgi_db_configure(NULL, &config);
                    break;
                case MenuResultCancel:
                    if (config_temp.sort != config.sort ||
                        config_temp.order != config.order ||
                        config_temp.filter != config.filter)
                    {
                        pkgi_db_configure(
                                search_active ? search_text : NULL, &config);
                        reposition();
                    }
                    break;
                case MenuResultAccept:
                    pkgi_menu_get(&config);
                    pkgi_save_config(config);
                    break;
                case MenuResultRefreshGames:
					RefreshGames(config.games_url.c_str(),ModeGames);
                    break;
                case MenuResultRefreshUpdates:
					RefreshGames(config.updates_url.c_str(),ModeUpdates);
                    break;
                case MenuResultRefreshDlcs:
					RefreshGames(config.dlcs_url.c_str(),ModeDlcs);
                    break;
                case MenuResultRefreshPsxGames:
					RefreshGames(config.psx_games_url.c_str(),ModePsxGames);
                    break;
                case MenuResultRefreshPspGames:
					RefreshGames(config.psp_games_url.c_str(),ModePspGames);
                    break;
                }
            }
        }

        pkgi_swap();
    }

    LOG("finished");
    pkgi_end();
}

