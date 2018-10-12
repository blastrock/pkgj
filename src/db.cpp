#include "db.hpp"

extern "C"
{
#include "sha256.h"
#include "utils.h"
}
#include "pkgi.hpp"
#include "sqlite.hpp"

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
        RET(Updates, "Vita updates");
        RET(Dlcs, "Vita DLCs");
        RET(PsmGames, "PSM games");
        RET(PsxGames, "PSX games");
        RET(PspGames, "PSP games");
#undef RET
    default:
        return "unknown mode";
    }
}

static uint8_t hexvalue(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    else if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }
    else if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }
    return 0;
}

static std::array<uint8_t, 32> pkgi_hexbytes(
        const char* digest, uint32_t length)
{
    std::array<uint8_t, 32> result;

    for (uint32_t i = 0; i < length; i++)
    {
        char ch1 = digest[2 * i];
        char ch2 = digest[2 * i + 1];
        if (ch1 == 0 || ch2 == 0)
            return result;

        result[i] = hexvalue(ch1) * 16 + hexvalue(ch2);
    }

    return result;
}

static const char* pkgi_mode_to_table_name(Mode mode)
{
    switch (mode)
    {
    case ModeGames:
        return "titles_psvgames";
    case ModeUpdates:
        return "titles_psvupdates";
    case ModeDlcs:
        return "titles_psvdlcs";
    case ModePsmGames:
        return "titles_psmgames";
    case ModePspGames:
        return "titles_pspgames";
    case ModePsxGames:
        return "titles_psxgames";
    }
    throw formatEx<std::runtime_error>(
            "unknown mode {}", static_cast<int>(mode));
}

TitleDatabase::TitleDatabase(std::string const& dbPath) : _dbPath(dbPath)
{
    reopen();
}

void TitleDatabase::reopen()
{
    LOG("opening database %s", _dbPath.c_str());
    sqlite3* db;
    SQLITE_CHECK(sqlite3_open(_dbPath.c_str(), &db), "can't open database");
    _sqliteDb.reset(db);

    for (int i = 0; i < ModeCount; ++i)
        SQLITE_EXEC(
                _sqliteDb,
                fmt::format(
                        R"(
                        CREATE TABLE IF NOT EXISTS {} (
                            id INTEGER PRIMARY KEY AUTOINCREMENT,
                            content TEXT NOT NULL,
                            name TEXT NOT NULL,
                            name_org TEXT,
                            zrif TEXT,
                            url TEXT NOT NULL,
                            digest BLOB,
                            size INT,
                            fw_version TEXT,
                            last_modification DATETIME,
                            region TEXT NOT NULL,
                            app_version TEXT
                        ))",
                        pkgi_mode_to_table_name(static_cast<Mode>(i)))
                        .c_str(),
                "can't create table");
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
    case ModeUpdates:
        switch (column)
        {
            MAP_COL(Region, 1);
            MAP_COL(Name, 2);
            MAP_COL(AppVersion, 3);
            MAP_COL(FwVersion, 4);
            MAP_COL(Url, 5);
            MAP_COL(LastModification, 7);
            MAP_COL(Size, 8);
            MAP_COL(Digest, 9);
            MAP_COL(Content, -1);
            MAP_COL(NameOrg, -1);
            MAP_COL(Zrif, -1);
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

void TitleDatabase::parse_tsv_file(Mode mode, std::string& db_data)
{
    SQLITE_EXEC(_sqliteDb, "BEGIN", "can't begin transaction");

    BOOST_SCOPE_EXIT_ALL(&)
    {
        if (!std::uncaught_exception())
            SQLITE_EXEC(_sqliteDb, "END", "can't end transaction");
        else
        {
            char* errmsg;
            auto err = sqlite3_exec(
                    _sqliteDb.get(), "ROLLBACK", nullptr, nullptr, &errmsg);
            if (err != SQLITE_OK)
            {
                LOG("sqlite error: %s", errmsg);
            }
        }
    };

    SQLITE_EXEC(
            _sqliteDb,
            fmt::format("DELETE FROM {}", pkgi_mode_to_table_name(mode))
                    .c_str(),
            "can't truncate table");

    sqlite3_stmt* stmt;
    SQLITE_CHECK(
            sqlite3_prepare_v2(
                    _sqliteDb.get(),
                    fmt::format(
                            R"(INSERT INTO {}
                            (content, name, name_org, zrif, url, digest, size, fw_version, last_modification, region, app_version)
                            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?))",
                            pkgi_mode_to_table_name(mode))
                            .c_str(),
                    -1,
                    &stmt,
                    nullptr),
            "can't prepare SQL statement");
    BOOST_SCOPE_EXIT_ALL(&)
    {
        sqlite3_finalize(stmt);
    };

    char* ptr = db_data.data();
    char* end = db_data.data() + db_data.size();

    // skip header
    while (ptr < end && *ptr != '\n')
        ptr++;
    if (ptr == end)
        return;
    ptr++; // \n

    while (ptr < end && *ptr)
    {
        try
        {
            const auto fields = pkgi_split_row(&ptr, end);

            auto content = get_or_empty(mode, fields, Column::Content);
            const auto region = get_or_empty(mode, fields, Column::Region);
            const auto name = get_or_empty(mode, fields, Column::Name);
            const auto name_org = get_or_empty(mode, fields, Column::NameOrg);
            const auto url = get_or_empty(mode, fields, Column::Url);
            const auto zrif = get_or_empty(mode, fields, Column::Zrif);
            const auto digest = get_or_empty(mode, fields, Column::Digest);
            const auto size = get_or_empty(mode, fields, Column::Size);
            const auto fw_version =
                    get_or_empty(mode, fields, Column::FwVersion);
            const auto last_modification =
                    get_or_empty(mode, fields, Column::LastModification);
            const auto app_version =
                    get_or_empty(mode, fields, Column::AppVersion);

            if (*url == '\0' || std::string(url) == "MISSING" ||
                std::string(url) == "CART ONLY" ||
                std::string(zrif) == "MISSING")
                continue;

            if (mode == ModeUpdates)
            {
                std::reverse_iterator<const char*> rbegin(
                        url + strlen(url) - 1);
                std::reverse_iterator<const char*> rend(url - 1);
                auto const it = std::find_if(
                        rbegin, rend, [](char c) { return c == '/'; });
                if (it != rend)
                    content = it.base();
            }

            sqlite3_reset(stmt);
            sqlite3_bind_text(stmt, 1, content, strlen(content), nullptr);
            sqlite3_bind_text(stmt, 2, name, strlen(name), nullptr);
            sqlite3_bind_text(stmt, 3, name_org, strlen(name_org), nullptr);
            sqlite3_bind_text(stmt, 4, zrif, strlen(zrif), nullptr);
            sqlite3_bind_text(stmt, 5, url, strlen(url), nullptr);
            std::array<uint8_t, 32> digest_array;
            if (std::all_of(digest, digest + 64, [](const auto c) {
                    return c != 0;
                }))
            {
                digest_array = pkgi_hexbytes(digest, SHA256_DIGEST_SIZE);
                sqlite3_bind_blob(
                        stmt,
                        6,
                        digest_array.data(),
                        digest_array.size(),
                        nullptr);
            }
            else
                sqlite3_bind_null(stmt, 6);
            sqlite3_bind_text(stmt, 7, size, strlen(size), nullptr);
            sqlite3_bind_text(stmt, 8, fw_version, strlen(fw_version), nullptr);
            sqlite3_bind_text(
                    stmt,
                    9,
                    last_modification,
                    strlen(last_modification),
                    nullptr);
            sqlite3_bind_text(stmt, 10, region, strlen(region), nullptr);
            sqlite3_bind_text(
                    stmt, 11, app_version, strlen(app_version), nullptr);

            auto err = sqlite3_step(stmt);
            if (err != SQLITE_DONE)
                throw std::runtime_error(fmt::format(
                        "can't execute SQL statement:\n{}",
                        sqlite3_errmsg(_sqliteDb.get())));
        }
        catch (const std::exception& e)
        {
            throw formatEx<std::runtime_error>(
                    "failed to parse line\n{}\n{}", ptr, e.what());
        }
    }
}

void TitleDatabase::update(Mode mode, Http* http, const std::string& update_url)
{
    std::string db_data;
    db_data.resize(MAX_DB_SIZE);
    db_total = 0;
    db_size = 0;

    if (update_url.empty())
        throw std::runtime_error("no update url");

    LOGF("loading update from {}", update_url);

    http->start(update_url, 0);

    const auto length = http->get_length();

    if (length > (int64_t)db_data.size() - 1)
        throw std::runtime_error(
                "list is too large... check for newer pkgj version");

    if (length != 0)
        db_total = (uint32_t)length;

    for (;;)
    {
        uint32_t want = (uint32_t)min64(1 << 16, db_data.size() - 1 - db_size);
        int read = http->read(
                reinterpret_cast<uint8_t*>(db_data.data()) + db_size, want);
        if (read == 0)
            break;
        db_size += read;
    }

    if (db_size == 0)
        throw std::runtime_error(
                "list is empty... check for newer pkgj version");

    LOG("parsing items");

    db_data.resize(db_size);
    parse_tsv_file(mode, db_data);

    LOG("finished parsing");
}

namespace
{
const char* region_to_quoted_string(GameRegion region)
{
    switch (region)
    {
    case RegionASA:
        return "'ASIA'";
    case RegionEUR:
        return "'EU'";
    case RegionJPN:
        return "'JP'";
    case RegionUSA:
        return "'US'";
    default:
        throw std::runtime_error(fmt::format("unknown region {}", (int)region));
    }
}

std::vector<std::string> filter_to_vector(uint32_t filter)
{
    std::vector<std::string> ret;
#define HANDLE_REGION(reg)            \
    if (filter & DbFilterRegion##reg) \
    ret.push_back(region_to_quoted_string(Region##reg))
    HANDLE_REGION(ASA);
    HANDLE_REGION(EUR);
    HANDLE_REGION(JPN);
    HANDLE_REGION(USA);
#undef HANDLE_REGION
    return ret;
}

std::string join(const std::vector<std::string>& vec, const std::string sep)
{
    if (vec.empty())
        return "";

    std::string out = vec[0];
    for (auto s = ++vec.begin(); s != vec.end(); ++s)
    {
        out += sep;
        out += *s;
    }
    return out;
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
        const std::string& search)
{
    // we need to reopen the db before every query because for some reason,
    // after the app is suspended, all further query will return disk I/O error
    reopen();

    LOG("reloading database");

    std::string query = fmt::format(
            "SELECT id, content, name, name_org, zrif, url, digest, size, "
            "last_modification, app_version, fw_version "
            "FROM {} WHERE 1 ",
            pkgi_mode_to_table_name(mode));

    if ((region_filter & DbFilterAllRegions) != DbFilterAllRegions)
        query += " AND region IN (" +
                 join(filter_to_vector(region_filter), ", ") + ")";
    if (!search.empty())
        query += " AND name LIKE ? COLLATE NOCASE";

    sqlite3_stmt* stmt;
    SQLITE_CHECK(
            sqlite3_prepare_v2(
                    _sqliteDb.get(), query.c_str(), -1, &stmt, nullptr),
            "can't prepare SQL statement");
    BOOST_SCOPE_EXIT_ALL(&)
    {
        sqlite3_finalize(stmt);
    };

    if (!search.empty())
    {
        const auto like = '%' + search + '%';
        sqlite3_bind_text(stmt, 1, like.data(), like.size(), SQLITE_TRANSIENT);
    }

    LOG("filling memory cache");

    db.clear();
    while (true)
    {
        auto const err = sqlite3_step(stmt);
        if (err == SQLITE_DONE)
            break;
        if (err != SQLITE_ROW)
            throw std::runtime_error(fmt::format(
                    "can't execute SQL statement:\n{}",
                    sqlite3_errmsg(_sqliteDb.get())));

        std::string content =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const std::string name =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* name_org =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* zrif =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        const char* url =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        const auto bdigest =
                static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 6));
        const auto digest_size = sqlite3_column_bytes(stmt, 6);
        std::array<uint8_t, 32> digest;
        if (bdigest)
            std::copy_n(
                    bdigest,
                    std::min<int>(digest_size, digest.size()),
                    digest.begin());
        const auto size = sqlite3_column_int64(stmt, 7);
        const char* date =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        const std::string app_version =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        const std::string fw_version =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));

        std::string full_name = name;
        if (!app_version.empty())
            full_name = fmt::format("{} ({})", name, app_version);
        if (!name.empty() && name.back() != ']' && fw_version > "3.60")
            full_name = fmt::format("{} [{}]", full_name, fw_version);

        db.push_back(DbItem{
                PresenceUnknown,
                content.size() >= 7 + 9 ? content.substr(7, 9) : "",
                content,
                0,
                full_name,
                name_org ? name_org : "",
                zrif ? zrif : "",
                url,
                static_cast<bool>(bdigest),
                bdigest ? digest : std::array<uint8_t, 32>{},
                size,
                date,
                app_version,
        });
    }

    LOG("sorting results");

    // We do not sort with an ORDER BY clause because it forces sqlite to create
    // a temporary table in a temporary file. While it is possible for the VFS
    // to support temporary files, we do not want to store them on the file
    // system as it is *very* slow. Options are:
    // - compile sqlite3 with SQLITE_TEMP_STORE=3, but sqlite3 is hard to
    //   compile because the vita lacks some APIs like mmap
    // - store the file in memory, but I couldn't find a memfd_create-like API,
    //   or a ramfs I could write to
    // - do the sorting manually, that's what I am left with
    std::sort(db.begin(), db.end(), [&](const auto& a, const auto& b) {
        return lower(a, b, sort_by, sort_order);
    });

    LOG("reloaded %d items", db.size());

    SQLITE_EXEC_RESULT(
            _sqliteDb,
            fmt::format(
                    "SELECT COUNT(*) FROM {}", pkgi_mode_to_table_name(mode))
                    .c_str(),
            "failed to get title count",
            ([](void* data, int, char** row, char**) {
                auto const title_count = static_cast<uint32_t*>(data);
                *title_count = atoi(row[0]);
                return 0;
            }),
            &_title_count);

    LOG("total: %d items", _title_count);
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
