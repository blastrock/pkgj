#pragma once

#include <string>
#include <vector>

struct CompPackVersion
{
    bool present;
    std::string base;
    std::string patch;
};

std::vector<std::string> pkgi_get_installed_games(const std::string& partition);
std::vector<std::string> pkgi_get_installed_themes(const std::string& partition);
std::string pkgi_get_game_version(const std::string& partition, const std::string& titleid);
CompPackVersion pkgi_get_comppack_versions(const std::string& partition, const std::string& titleid);
bool pkgi_dlc_is_installed(const char* partition, const char* content);
bool pkgi_psm_is_installed(const char* partition, const char* titleid);
bool pkgi_psp_is_installed(const char* psppartition, const char* gpath, const char* ipath, const char* content);
bool pkgi_psx_is_installed(const char* psppartition, const char* ppath, const char* content);
void pkgi_install(const char* partition, const char* contentid);
void pkgi_install_update(const std::string& partition, const std::string& titleid);
void pkgi_install_comppack(
        const std::string& partition, const std::string& titleid, bool patch, const std::string& version);
void pkgi_install_psmgame(const char* partition, const char* contentid);
void pkgi_install_pspgame(const char* partition, const char* gpath, const char* contentid);
void pkgi_install_pspgame_as_iso(const char* partition, const char* gpath, const char* ipath, const char* contentid);
void pkgi_install_pspdlc(const char* partition, const char* gpath, const char* contentid);
