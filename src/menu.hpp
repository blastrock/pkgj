#pragma once

#include "db.hpp"

typedef struct pkgi_input pkgi_input;

typedef enum
{
    MenuResultSearch,
    MenuResultSearchClear,
    MenuResultAccept,
    MenuResultCancel,
    MenuResultRefresh,
    MenuResultShowGames,
    MenuResultShowDlcs,
    MenuResultShowDemos,
    MenuResultShowThemes,
    MenuResultShowPsxGames,
    MenuResultShowPspGames,
    MenuResultShowPspDlcs,
    MenuResultShowPsmGames,
    MenuResultSelectedClear,
} MenuResult;

typedef struct Config Config;

int pkgi_menu_is_open(void);
void pkgi_menu_get(Config* config);
MenuResult pkgi_menu_result(void);

void pkgi_menu_start(int search_clear, int selected_clear, const Config* config, int allow_update);

int pkgi_do_menu(pkgi_input* input);
