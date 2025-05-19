#include "menu.hpp"

extern "C"
{
#include "style.h"
}
#include "config.hpp"
#include "pkgi.hpp"

static int menu_search_clear;
static int menu_selected_clear;

static Config menu_config;
static uint32_t menu_selected;
static int menu_allow_refresh;

static MenuResult menu_result;

static int32_t menu_width;
static int32_t menu_delta;

typedef enum
{
    MenuSearch,
    MenuSearchClear,
    MenuText,
    MenuSort,
    MenuFilter,
    MenuRefresh,
    MenuShow,
    MenuSelectedClear,
} MenuType;

typedef struct
{
    MenuType type;
    const char* text;
    uint32_t value;
} MenuEntry;

static const MenuEntry menu_entries[] = {
        {MenuSearch, "Search...", 0},
        {MenuSearchClear, PKGI_UTF8_CLEAR " clear", 0},

        {MenuText, "Sort by:", 0},
        {MenuSort, "Title", SortByTitle},
        {MenuSort, "Region", SortByRegion},
        {MenuSort, "Name", SortByName},
        {MenuSort, "Size", SortBySize},
        {MenuSort, "Date", SortByDate},

        {MenuText, "Filters:", 0},
        {MenuFilter, "Asia", DbFilterRegionASA},
        {MenuFilter, "Europe", DbFilterRegionEUR},
        {MenuFilter, "Japan", DbFilterRegionJPN},
        {MenuFilter, "USA", DbFilterRegionUSA},
        {MenuFilter, "Installed games only", DbFilterInstalled},

        {MenuRefresh, "Refresh", 0},

        {MenuShow, "Show games", 1},
        {MenuShow, "Show DLCs", 2},
        {MenuShow, "Show demos", 64},
        {MenuShow, "Show themes", 32},
        {MenuShow, "Show PS1 games", 4},
        {MenuShow, "Show PSP games", 8},
        {MenuShow, "Show PSP DLCs", 128},
        {MenuShow, "Show PSM games", 16},

        {MenuSelectedClear, "Clear selected", 0},
};

int pkgi_menu_is_open(void)
{
    return menu_width != 0;
}

MenuResult pkgi_menu_result()
{
    return menu_result;
}

void pkgi_menu_get(Config* config)
{
    *config = menu_config;
}

void pkgi_menu_start(int search_clear, int selected_clear, const Config* config, int allow_refresh)
{
    menu_search_clear = search_clear;
    menu_selected_clear = selected_clear;
    menu_width = 1;
    menu_delta = 1;
    menu_config = *config;
    menu_allow_refresh = allow_refresh;
}

int pkgi_do_menu(pkgi_input* input)
{
    if (menu_delta != 0)
    {
        menu_width +=
                menu_delta *
                (int32_t)(input->delta * PKGI_ANIMATION_SPEED / 2 / 1000000);

        if (menu_delta < 0 && menu_width <= 0)
        {
            menu_width = 0;
            menu_delta = 0;
            return 0;
        }
        else if (menu_delta > 0 && menu_width >= PKGI_MENU_WIDTH)
        {
            menu_width = PKGI_MENU_WIDTH;
            menu_delta = 0;
        }
    }

    if (menu_width != 0)
    {
        pkgi_draw_rect(
                VITA_WIDTH - menu_width,
                0,
                menu_width,
                VITA_HEIGHT,
                PKGI_COLOR_MENU_BACKGROUND);
    }

    if (input->active & PKGI_BUTTON_UP)
    {
        do
        {
            if (menu_selected == 0)
            {
                menu_selected = PKGI_COUNTOF(menu_entries) - 1;
            }
            else
            {
                menu_selected--;
            }
        } while (menu_entries[menu_selected].type == MenuText ||
                 (menu_entries[menu_selected].type == MenuSearchClear &&
                  !menu_search_clear) ||
                  (menu_entries[menu_selected].type == MenuSelectedClear &&
                   (menu_search_clear || !menu_selected_clear)) ||
                 (menu_entries[menu_selected].type == MenuShow &&
                  !(menu_entries[menu_selected].value & menu_allow_refresh)));
    }

    if (input->active & PKGI_BUTTON_DOWN)
    {
        do
        {
            if (menu_selected == PKGI_COUNTOF(menu_entries) - 1)
            {
                menu_selected = 0;
            }
            else
            {
                menu_selected++;
            }
        } while (menu_entries[menu_selected].type == MenuText ||
                 (menu_entries[menu_selected].type == MenuSearchClear &&
                  !menu_search_clear) ||
                 (menu_entries[menu_selected].type == MenuSelectedClear &&
                   (menu_search_clear || !menu_selected_clear)) ||
                 (menu_entries[menu_selected].type == MenuShow &&
                  !(menu_entries[menu_selected].value & menu_allow_refresh)));
    }

    if (input->pressed & pkgi_cancel_button())
    {
        menu_result = MenuResultCancel;
        menu_delta = -1;
        return 1;
    }
    else if (input->pressed & PKGI_BUTTON_T)
    {
        menu_result = MenuResultAccept;
        menu_delta = -1;
        return 1;
    }
    else if (input->pressed & pkgi_ok_button())
    {
        MenuType type = menu_entries[menu_selected].type;
        if (type == MenuSearch)
        {
            menu_result = MenuResultSearch;
            menu_delta = -1;
            return 1;
        }
        else if (type == MenuSearchClear)
        {
            menu_selected--;
            menu_result = MenuResultSearchClear;
            menu_delta = -1;
            return 1;
        }
        else if (type == MenuRefresh)
        {
            menu_result = MenuResultRefresh;
            menu_delta = -1;
            return 1;
        }
        else if (type == MenuShow)
        {
            switch (menu_entries[menu_selected].value)
            {
            case 1:
                menu_result = MenuResultShowGames;
                break;
            case 2:
                menu_result = MenuResultShowDlcs;
                break;
            case 4:
                menu_result = MenuResultShowPsxGames;
                break;
            case 8:
                menu_result = MenuResultShowPspGames;
                break;
            case 16:
                menu_result = MenuResultShowPsmGames;
                break;
            case 32:
                menu_result = MenuResultShowThemes;
                break;
            case 64:
                menu_result = MenuResultShowDemos;
                break;
            case 128:
                menu_result = MenuResultShowPspDlcs;
                break;
            }

            menu_delta = -1;
            return 1;
        }
        else if (type == MenuSelectedClear)
        {
            menu_selected--;
            menu_result = MenuResultSelectedClear;
            menu_delta = -1;
            return 1;
        }
        else if (type == MenuSort)
        {
            DbSort value = (DbSort)menu_entries[menu_selected].value;
            if (menu_config.sort == value)
            {
                menu_config.order = menu_config.order == SortAscending
                                            ? SortDescending
                                            : SortAscending;
            }
            else
            {
                menu_config.sort = value;
            }
        }
        else if (type == MenuFilter)
        {
            menu_config.filter ^= menu_entries[menu_selected].value;
        }
    }

    if (menu_width != PKGI_MENU_WIDTH)
    {
        return 1;
    }

    int font_height = pkgi_text_height("M");

    int y = PKGI_MENU_TOP_PADDING;
    for (uint32_t i = 0; i < PKGI_COUNTOF(menu_entries); i++)
    {
        const MenuEntry* entry = menu_entries + i;

        MenuType type = entry->type;
        if (type == MenuText)
        {
            y += font_height / 3;
        }
        else if (type == MenuSearchClear && !menu_search_clear)
        {
            continue;
        }
        else if (type == MenuSelectedClear && (menu_search_clear || !menu_selected_clear))
        {
            continue;
        }
        else if (type == MenuRefresh)
            y += font_height / 3;
        else if (type == MenuShow)
        {
            if (!(entry->value & menu_allow_refresh))
            {
                continue;
            }
        }

        uint32_t color = menu_selected == i ? PKGI_COLOR_TEXT_MENU_SELECTED
                                            : PKGI_COLOR_TEXT_MENU;

        int x = VITA_WIDTH - PKGI_MENU_WIDTH + PKGI_MENU_LEFT_PADDING;

        char text[64];
        if (type == MenuSearch || type == MenuSearchClear || type == MenuText ||
            type == MenuRefresh || type == MenuShow || type == MenuSelectedClear)
        {
            pkgi_strncpy(text, sizeof(text), entry->text);
        }
        else if (type == MenuSort)
        {
            if (menu_config.sort == (DbSort)entry->value)
            {
                pkgi_snprintf(
                        text,
                        sizeof(text),
                        "%s %s",
                        menu_config.order == SortAscending
                                ? PKGI_UTF8_SORT_ASC
                                : PKGI_UTF8_SORT_DESC,
                        entry->text);
            }
            else
            {
                x += pkgi_text_width(PKGI_UTF8_SORT_ASC " ");
                pkgi_strncpy(text, sizeof(text), entry->text);
            }
        }
        else if (type == MenuFilter)
        {
            pkgi_snprintf(
                    text,
                    sizeof(text),
                    "%s %s",
                    menu_config.filter & entry->value ? PKGI_UTF8_CHECK_ON
                                                      : PKGI_UTF8_CHECK_OFF,
                    entry->text);
        }
        pkgi_draw_text(x, y, color, text);

        y += font_height - 1;
    }

    return 1;
}
