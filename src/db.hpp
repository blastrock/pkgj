#pragma once

#include "http.hpp"

#include <array>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <cstdint>

enum DbPresence
{
    PresenceUnknown,
    PresenceIncomplete,
    PresenceInstalling,
    PresenceInstalled,
    PresenceMissing,
    PresenceGamePresent,
};

enum DbSort
{
    SortByTitle,
    SortByRegion,
    SortByName,
    SortBySize,
    SortByDate,
};

enum DbSortOrder
{
    SortAscending,
    SortDescending,
};

enum DbFilter
{
    DbFilterRegionASA = 0x01,
    DbFilterRegionEUR = 0x02,
    DbFilterRegionJPN = 0x04,
    DbFilterRegionUSA = 0x08,

    DbFilterInstalled = 0x10,

    DbFilterAllRegions = DbFilterRegionUSA | DbFilterRegionEUR |
                         DbFilterRegionJPN | DbFilterRegionASA,
    DbFilterAll = DbFilterAllRegions,
};

struct DbItem
{
    DbPresence presence;
    std::string partition;
    std::string titleid;
    std::string content;
    uint32_t flags;
    std::string name;
    std::string name_org;
    std::string zrif;
    std::string url;
    bool has_digest;
    std::array<uint8_t, 32> digest;
    int64_t size;
    std::string date;
    std::string app_version;
    std::string fw_version;
};

enum GameRegion
{
    RegionASA,
    RegionEUR,
    RegionJPN,
    RegionUSA,
    RegionUnknown,
};

enum Mode
{
    ModeGames,
    ModeDlcs,
    ModeDemos,
    ModeThemes,
    ModePsmGames,
    ModePsxGames,
    ModePspGames,
    ModePspDlcs,
};

static constexpr auto ModeCount = 8;

std::string pkgi_mode_to_string(Mode mode);

class TitleDatabase
{
public:
    TitleDatabase(const std::string& dbPath);

    void reload(
            Mode mode,
            uint32_t region_filter,
            DbSort sort_by,
            DbSortOrder sort_order,
            const std::string& partition,
            const std::string& search,
            const std::set<std::string>& installed_games);

    void update(Mode mode, Http* http, const std::string& update_url);
    void get_update_status(uint32_t* updated, uint32_t* total);

    uint32_t count();
    uint32_t total();
    DbItem* get(uint32_t index);
    DbItem* get_by_content(const char* content);

private:
    static constexpr auto MAX_DB_ITEMS = 8192;

    std::string _dbPath;
    uint32_t db_total;
    uint32_t db_size;
    uint32_t _title_count;

    std::vector<DbItem> db;
};

GameRegion pkgi_get_region(const std::string& titleid);
