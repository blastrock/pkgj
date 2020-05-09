#include "db.hpp"

#include "file.hpp"
#include "pkgi.hpp"
#include "sha256.hpp"
#include "utils.hpp"

#include <fmt/format.h>

#include <boost/scope_exit.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include <stddef.h>

std::string pkgi_mode_to_string(Mode mode)
{
    switch (mode)
    {
#define RET(mode, str) \
    case Mode##mode:   \
        return str
        RET(Games, "Vita games");
        RET(Dlcs, "Vita DLCs");
        RET(Demos, "Vita demos");
        RET(Themes, "Vita themes");
        RET(PsmGames, "PSM games");
        RET(PsxGames, "PSX games");
        RET(PspGames, "PSP games");
        RET(PspDlcs, "PSP DLCs");
#undef RET
    }
    return "unknown mode";
}

TitleDatabase::TitleDatabase(const std::string& dbPath) : _dbPath(dbPath)
{
}

static const char* pkgi_mode_to_file_name(Mode mode)
{
    switch (mode)
    {
    case ModeGames:
        return "titles_psvgames.tsv";
    case ModeDlcs:
        return "titles_psvdlcs.tsv";
    case ModeDemos:
        return "titles_psvdemos.tsv";
    case ModeThemes:
        return "titles_psvthemes.tsv";
    case ModePsmGames:
        return "titles_psmgames.tsv";
    case ModePspGames:
        return "titles_pspgames.tsv";
    case ModePspDlcs:
        return "titles_pspdlcs.tsv";
    case ModePsxGames:
        return "titles_psxgames.tsv";
    }
    throw formatEx<std::runtime_error>(
            "unknown mode {}", static_cast<int>(mode));
}

namespace
{
std::vector<const char*> pkgi_split_row(char** pptr, const char* end)
{
    auto& ptr = *pptr;

    std::vector<const char*> result;
    while (ptr != end && *ptr != '\n')
    {
        const char* field = ptr;
        while (ptr != end && *ptr != '\t' && *ptr != '\r')
            ++ptr;
        if (ptr == end)
        {
            result.push_back(field);
            break;
        }
        *ptr++ = 0;
        result.push_back(field);

        if (ptr == end)
        {
            result.push_back(field);
            break;
        }
    }
    while (ptr != end && *ptr++ != '\n')
        ;
    return result;
}

enum class Column
{
    Region,
    Content,
    Name,
    NameOrg,
    AppVersion,
    Zrif,
    Url,
    Digest,
    Size,
    FwVersion,
    LastModification,
};

int pkgi_get_column_number(Mode mode, Column column)
{
#define MAP_COL(name, i) \
    case Column::name:   \
        return i

    switch (mode)
    {
    case ModeGames:
        switch (column)
        {
            MAP_COL(Region, 1);
            MAP_COL(Name, 2);
            MAP_COL(Url, 3);
            MAP_COL(Zrif, 4);
            MAP_COL(Content, 5);
            MAP_COL(LastModification, 6);
            MAP_COL(NameOrg, 7);
            MAP_COL(Size, 8);
            MAP_COL(Digest, 9);
            MAP_COL(FwVersion, 10);
            MAP_COL(AppVersion, -1);
        default:
            throw std::runtime_error("invalid column");
        }
    case ModeDlcs:
        switch (column)
        {
            MAP_COL(Region, 1);
            MAP_COL(Name, 2);
            MAP_COL(Url, 3);
            MAP_COL(Zrif, 4);
            MAP_COL(Content, 5);
            MAP_COL(LastModification, 6);
            MAP_COL(Size, 7);
            MAP_COL(Digest, 8);
            MAP_COL(NameOrg, -1);
            MAP_COL(FwVersion, -1);
            MAP_COL(AppVersion, -1);
        default:
            throw std::runtime_error("invalid column");
        }
    case ModeDemos:
        switch (column)
        {
            MAP_COL(Region, 1);
            MAP_COL(Name, 2);
            MAP_COL(Url, 3);
            MAP_COL(Zrif, 4);
            MAP_COL(Content, 5);
            MAP_COL(LastModification, 6);
            MAP_COL(NameOrg, 7);
            MAP_COL(Size, 8);
            MAP_COL(Digest, 9);
            MAP_COL(FwVersion, 10);
            MAP_COL(AppVersion, -1);
        default:
            throw std::runtime_error("invalid column");
        }
    case ModeThemes:
        switch (column)
        {
            MAP_COL(Region, 1);
            MAP_COL(Name, 2);
            MAP_COL(Url, 3);
            MAP_COL(Zrif, 4);
            MAP_COL(Content, 5);
            MAP_COL(LastModification, 6);
            MAP_COL(Size, 7);
            MAP_COL(Digest, 8);
            MAP_COL(NameOrg, -1);
            MAP_COL(FwVersion, -1);
            MAP_COL(AppVersion, -1);
        default:
            throw std::runtime_error("invalid column");
        }
    case ModePsmGames:
        switch (column)
        {
            MAP_COL(Region, 1);
            MAP_COL(Name, 2);
            MAP_COL(Url, 3);
            MAP_COL(Zrif, 4);
            MAP_COL(Content, 5);
            MAP_COL(LastModification, 6);
            MAP_COL(Size, 7);
            MAP_COL(Digest, 8);
            MAP_COL(NameOrg, -1);
            MAP_COL(FwVersion, -1);
            MAP_COL(AppVersion, -1);
        default:
            throw std::runtime_error("invalid column");
        }
    case ModePsxGames:
        switch (column)
        {
            MAP_COL(Region, 1);
            MAP_COL(Name, 2);
            MAP_COL(Url, 3);
            MAP_COL(Content, 4);
            MAP_COL(LastModification, 5);
            MAP_COL(NameOrg, 6);
            MAP_COL(Size, 7);
            MAP_COL(Digest, 8);
            MAP_COL(Zrif, -1);
            MAP_COL(FwVersion, -1);
            MAP_COL(AppVersion, -1);
        default:
            throw std::runtime_error("invalid column");
        }
    case ModePspGames:
        switch (column)
        {
            MAP_COL(Region, 1);
            MAP_COL(Name, 3);
            MAP_COL(Url, 4);
            MAP_COL(Content, 5);
            MAP_COL(LastModification, 6);
            MAP_COL(Size, 9);
            MAP_COL(Digest, 10);
            MAP_COL(FwVersion, -1);
            MAP_COL(NameOrg, -1);
            MAP_COL(Zrif, -1);
            MAP_COL(AppVersion, -1);
        default:
            throw std::runtime_error("invalid column");
        }
    case ModePspDlcs:
        switch (column)
        {
            MAP_COL(Region, 1);
            MAP_COL(Name, 2);
            MAP_COL(Url, 3);
            MAP_COL(Content, 4);
            MAP_COL(LastModification, 5);
            MAP_COL(Size, 8);
            MAP_COL(Digest, 9);
            MAP_COL(FwVersion, -1);
            MAP_COL(NameOrg, -1);
            MAP_COL(Zrif, -1);
            MAP_COL(AppVersion, -1);
        default:
            throw std::runtime_error("invalid column");
        }
    default:
        throw std::runtime_error("invalid mode");
    }
#undef MAP_COL
}

const char* get_or_empty(
        Mode mode, std::vector<const char*> const& v, Column column)
{
    const auto pos = pkgi_get_column_number(mode, column);
    if (pos < 0)
        return "";
    return v.at(pos);
}
}

void TitleDatabase::update(Mode mode, Http* http, const std::string& update_url)
{
    const auto tmppath = _dbPath + "/dbtmp.tsv";
    auto item_file = pkgi_create(tmppath);
    BOOST_SCOPE_EXIT_ALL(&)
    {
        if (item_file)
            pkgi_close(item_file);
    };

    std::vector<uint8_t> db_data(64 * 1024);
    db_total = 0;
    db_size = 0;

    LOGF("loading update from {}", update_url);

    http->start(update_url, 0);

    db_total = http->get_length();

    for (;;)
    {
        int read = http->read(db_data.data(), db_data.size());
        if (read == 0)
            break;
        db_size += read;

        pkgi_write(item_file, db_data.data(), read);
    }

    if (db_size == 0)
        throw std::runtime_error(
                "list is empty... check for newer pkgj version");
    if (db_size != db_total)
        throw std::runtime_error(
                "TSV file is truncated, check your Internet connection and "
                "retry");

    pkgi_close(item_file);
    item_file = nullptr;

    const auto filepath =
            fmt::format("{}/{}", _dbPath, pkgi_mode_to_file_name(mode));

    pkgi_rename(tmppath, filepath);

    LOG("finished downloading");
}

namespace
{
const char* region_to_string(GameRegion region)
{
    switch (region)
    {
    case RegionASA:
        return "ASIA";
    case RegionEUR:
        return "EU";
    case RegionJPN:
        return "JP";
    case RegionUSA:
        return "US";
    default:
        throw std::runtime_error(fmt::format("unknown region {}", (int)region));
    }
}

std::set<std::string> filter_to_vector(uint32_t filter)
{
    std::set<std::string> ret;
#define HANDLE_REGION(reg)            \
    if (filter & DbFilterRegion##reg) \
    ret.insert(region_to_string(Region##reg))
    HANDLE_REGION(ASA);
    HANDLE_REGION(EUR);
    HANDLE_REGION(JPN);
    HANDLE_REGION(USA);
#undef HANDLE_REGION
    return ret;
}

bool lower(const DbItem& a, const DbItem& b, DbSort sort, DbSortOrder order)
{
    GameRegion reg_a = pkgi_get_region(a.titleid);
    GameRegion reg_b = pkgi_get_region(b.titleid);

    int64_t cmp;
    if (sort == SortByTitle)
        cmp = a.titleid.compare(b.titleid);
    else if (sort == SortByRegion)
        cmp = reg_a - reg_b;
    else if (sort == SortByName)
        cmp = pkgi_stricmp(a.name.c_str(), b.name.c_str());
    else if (sort == SortBySize)
        cmp = a.size - b.size;
    else if (sort == SortByDate)
        cmp = a.date.compare(b.date);
    else
        throw std::runtime_error(fmt::format("unknown sort order {}", sort));

    if (cmp == 0)
        cmp = a.titleid.compare(b.titleid);

    if (order == SortDescending)
        cmp = -cmp;

    return cmp < 0;
}
}

void TitleDatabase::reload(
        Mode mode,
        uint32_t region_filter,
        DbSort sort_by,
        DbSortOrder sort_order,
        const std::string& partition,
        const std::string& search,
        const std::set<std::string>& installed_games)
{
    const auto filter_by_region =
            (region_filter & DbFilterAllRegions) != DbFilterAllRegions;
    const auto regions = filter_to_vector(region_filter);

    db.clear();
    _title_count = 0;

    const auto dbpath =
            fmt::format("{}/{}", _dbPath, pkgi_mode_to_file_name(mode));

    if (!pkgi_file_exists(dbpath))
        return;

    auto db_data = pkgi_load(dbpath);

    auto ptr = reinterpret_cast<char*>(db_data.data());
    const auto end = reinterpret_cast<char*>(db_data.data() + db_data.size());

    // skip header
    while (ptr < end && *ptr != '\n')
        ptr++;
    if (ptr == end)
        return;
    ptr++; // \n

    unsigned line = 1;
    while (ptr < end && *ptr)
    {
        ++line;
        try
        {
            const auto fields = pkgi_split_row(&ptr, end);

            const std::string content =
                    get_or_empty(mode, fields, Column::Content);
            const std::string titleid =
                    content.size() >= 7 + 9 ? content.substr(7, 9) : "";
            const auto region = get_or_empty(mode, fields, Column::Region);
            const std::string name = get_or_empty(mode, fields, Column::Name);
            const auto name_org = get_or_empty(mode, fields, Column::NameOrg);
            const auto url = get_or_empty(mode, fields, Column::Url);
            const auto zrif = get_or_empty(mode, fields, Column::Zrif);
            const auto digest = get_or_empty(mode, fields, Column::Digest);
            const std::string size = get_or_empty(mode, fields, Column::Size);
            const std::string fw_version =
                    get_or_empty(mode, fields, Column::FwVersion);
            const auto last_modification =
                    get_or_empty(mode, fields, Column::LastModification);
            const std::string app_version =
                    get_or_empty(mode, fields, Column::AppVersion);

            if (*url == '\0' || std::string(url) == "MISSING" ||
                std::string(url) == "CART ONLY" ||
                std::string(zrif) == "MISSING")
                continue;

            ++_title_count;

            if (filter_by_region && !regions.count(region))
                continue;

            if (!search.empty() &&
                !pkgi_stricontains(name.c_str(), search.c_str()))
                continue;

            bool bdigest = true;
            std::array<uint8_t, 32> digest_array{};
            if (std::all_of(digest, digest + 64, [](const auto c) {
                    return c != 0;
                }))
                digest_array = pkgi_hexbytes(digest, SHA256_DIGEST_SIZE);
            else
                bdigest = false;

            std::string full_name = name;
            if (!app_version.empty())
                full_name = fmt::format("{} ({})", name, app_version);
            if (!name.empty() && name.back() != ']' && fw_version > "3.60")
                full_name = fmt::format("{} [{}]", full_name, fw_version);

            if (!(region_filter & DbFilterInstalled) ||
                installed_games.find(titleid) != installed_games.end())
                db.push_back(DbItem{
                        PresenceUnknown,
                        partition,
                        titleid,
                        content,
                        0,
                        full_name,
                        name_org ? name_org : "",
                        zrif ? zrif : "",
                        url,
                        static_cast<bool>(bdigest),
                        digest_array,
                        size.empty() ? 0 : std::stoll(size),
                        last_modification,
                        app_version,
                        fw_version,
                });
        }
        catch (const std::exception& e)
        {
            throw formatEx<std::runtime_error>(
                    "failed to parse line {}: {}", line, e.what());
        }
    }

    std::sort(db.begin(), db.end(), [&](const auto& a, const auto& b) {
        return lower(a, b, sort_by, sort_order);
    });

    LOGF("reloaded {}/{} items", db.size(), _title_count);
}

void TitleDatabase::get_update_status(uint32_t* updated, uint32_t* total)
{
    *updated = db_size;
    *total = db_total;
}

uint32_t TitleDatabase::count()
{
    return db.size();
}

uint32_t TitleDatabase::total()
{
    return _title_count;
}

DbItem* TitleDatabase::get(uint32_t index)
{
    return index < db.size() ? &db[index] : NULL;
}

DbItem* TitleDatabase::get_by_content(const char* content)
{
    for (size_t i = 0; i < db.size(); ++i)
        if (db[i].content == content)
            return &db[i];
    return NULL;
}

GameRegion pkgi_get_region(const std::string& titleid)
{
    if (titleid.size() < 4)
        return RegionUnknown;

    uint32_t first = get32le((uint8_t*)titleid.c_str());

#define ID(a, b, c, d)                                    \
    (uint32_t)(                                           \
            ((uint8_t)(d) << 24) | ((uint8_t)(c) << 16) | \
            ((uint8_t)(b) << 8) | ((uint8_t)(a)))

    switch (first)
    {
    case ID('V', 'C', 'A', 'S'):
    case ID('P', 'C', 'S', 'H'):
    case ID('V', 'L', 'A', 'S'):
    case ID('P', 'C', 'S', 'D'):
    case ID('N', 'P', 'H', 'I'):
    case ID('N', 'P', 'H', 'J'):
    case ID('N', 'P', 'H', 'G'):
    case ID('N', 'P', 'H', 'H'):
    case ID('N', 'P', 'H', 'Z'):
    case ID('N', 'P', 'Q', 'A'):
    case ID('U', 'C', 'A', 'S'):
        return RegionASA;

    case ID('P', 'C', 'S', 'F'):
    case ID('P', 'C', 'S', 'B'):
    case ID('N', 'P', 'E', 'E'):
    case ID('N', 'P', 'E', 'F'):
    case ID('N', 'P', 'E', 'G'):
    case ID('N', 'P', 'E', 'H'):
    case ID('N', 'P', 'E', 'X'):
    case ID('N', 'P', 'E', 'Z'):
    case ID('N', 'P', 'O', 'A'):
    case ID('U', 'C', 'E', 'S'):
    case ID('U', 'L', 'E', 'S'):
        return RegionEUR;

    case ID('P', 'C', 'S', 'C'):
    case ID('V', 'C', 'J', 'S'):
    case ID('P', 'C', 'S', 'G'):
    case ID('V', 'L', 'J', 'S'):
    case ID('V', 'L', 'J', 'M'):
    case ID('N', 'P', 'J', 'I'):
    case ID('N', 'P', 'J', 'J'):
    case ID('N', 'P', 'J', 'G'):
    case ID('N', 'P', 'J', 'H'):
    case ID('N', 'P', 'J', 'Q'):
    case ID('N', 'P', 'P', 'A'):
    case ID('N', 'P', 'X', 'P'):
    case ID('U', 'C', 'J', 'S'):
    case ID('U', 'L', 'J', 'S'):
    case ID('U', 'L', 'J', 'M'):
        return RegionJPN;

    case ID('P', 'C', 'S', 'E'):
    case ID('P', 'C', 'S', 'A'):
    case ID('N', 'P', 'U', 'F'):
    case ID('N', 'P', 'U', 'I'):
    case ID('N', 'P', 'U', 'J'):
    case ID('N', 'P', 'U', 'G'):
    case ID('N', 'P', 'U', 'H'):
    case ID('N', 'P', 'U', 'X'):
    case ID('N', 'P', 'U', 'Z'):
    case ID('N', 'P', 'N', 'A'):
    case ID('U', 'C', 'U', 'S'):
    case ID('U', 'L', 'U', 'S'):
        return RegionUSA;

    default:
        return RegionUnknown;
    }
#undef ID
}
