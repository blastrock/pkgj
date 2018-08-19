#pragma once

#include "db.hpp"

#include <string>

typedef struct Config
{
    DbSort sort;
    DbSortOrder order;
    uint32_t filter;
    int no_version_check;
    int install_psp_as_pbp;
    std::string install_psp_psx_location;
    bool psm_readme_disclaimer;

    std::string games_url;
    std::string updates_url;
    std::string dlcs_url;
    std::string psm_games_url;
    std::string psx_games_url;
    std::string psp_games_url;

    std::string comppack_url;
} Config;

Config pkgi_load_config();
void pkgi_save_config(const Config& config);
