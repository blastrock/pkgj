#pragma once

#include "db.hpp"

typedef struct pkgi_input pkgi_input;

typedef enum {
    MenuResultSearch,
    MenuResultSearchClear,
    MenuResultAccept,
    MenuResultCancel,
    MenuResultRefresh,
    MenuResultRefreshCompPack,
    MenuResultShowGames,
    MenuResultShowUpdates,
    MenuResultShowDlcs,
    MenuResultShowPsxGames,
    MenuResultShowPspGames,
    MenuResultShowPsmGames,
} MenuResult;

typedef struct Config Config;

int pkgi_menu_is_open(void);
void pkgi_menu_get(Config* config);
MenuResult pkgi_menu_result(void);

void pkgi_menu_start(int search_clear, const Config* config, int allow_update);

int pkgi_do_menu(pkgi_input* input);
