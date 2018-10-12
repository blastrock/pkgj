#include "comppackdb.hpp"

extern "C" {
#include "sha256.h"
#include "utils.h"
}
#include "pkgi.hpp"
#include "sqlite.hpp"

#include <fmt/format.h>

#include <boost/scope_exit.hpp>

#include <algorithm>
#include <cstring>
#include <regex>
#include <stdexcept>
#include <string>

#include <stddef.h>

CompPackDatabase::CompPackDatabase(std::string const& dbPath) : _dbPath(dbPath)
{
    reopen();
}

void CompPackDatabase::reopen()
{
    LOG("opening database %s", _dbPath.c_str());
    sqlite3* db;
    SQLITE_CHECK(sqlite3_open(_dbPath.c_str(), &db), "can't open database");
    _sqliteDb.reset(db);

    try
    {
        sqlite3_stmt* stmt;
        SQLITE_CHECK(
                sqlite3_prepare_v2(
                        _sqliteDb.get(),
                        R"(
                        SELECT titleid, app_version, path
                        FROM entries
                        WHERE 0)",
                        -1,
                        &stmt,
                        nullptr),
                "sanity select failed");
        sqlite3_finalize(stmt);
    }
    catch (const std::exception& e)
    {
        LOG("%s. Trying migration.", e.what());
        SQLITE_EXEC(
                _sqliteDb,
                R"(DROP TABLE IF EXISTS entries)",
                "drop table failed");
    }

    SQLITE_EXEC(_sqliteDb, R"(
        CREATE TABLE IF NOT EXISTS entries (
            titleid TEXT NOT NULL,
            app_version TEXT NOT NULL,
            path TEXT NOT NULL,
            PRIMARY KEY (titleid, app_version)
        ))", "can't create comp pack table");
}

namespace
{
std::vector<const char*> pkgi_split_row(char** pptr, const char* end)
{
    auto& ptr = *pptr;

    std::vector<const char*> result;
    while (ptr != end)
    {
        const char* field = ptr;
        while (ptr != end && *ptr != '=' && *ptr != '\n')
            ++ptr;
        if (ptr == end)
        {
            result.push_back(field);
            break;
        }
        if (*ptr == '\n')
        {
            *ptr++ = 0;
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
    return result;
}
}

void CompPackDatabase::parse_entries(std::string& db_data)
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

    SQLITE_EXEC(_sqliteDb, "DELETE FROM entries", "can't truncate table");

    sqlite3_stmt* stmt;
    SQLITE_CHECK(
            sqlite3_prepare_v2(
                    _sqliteDb.get(),
                    R"(INSERT INTO entries
                    (titleid, path, app_version)
                    VALUES (?, ?, ?))",
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

    const auto regex = std::regex(
            R"(([A-Z]{4}\d{5})-(\d{2}_\d{3})-(\d{2}_\d{2})-(\d{2}_\d{2}).ppk)");

    const char* current_line = ptr;
    while (ptr < end && *ptr)
    {
        try
        {
            current_line = ptr;
            const auto fields = pkgi_split_row(&ptr, end);
            if (fields.size() < 1)
                throw std::runtime_error("failed to split line");

            const auto path = std::string(fields[0]);

            std::smatch matches;
            if (!std::regex_search(path, matches, regex))
                throw formatEx<std::runtime_error>("regex didn't match");
            const auto titleid = matches.str(1);
            const auto app_version = matches.str(3);

            sqlite3_reset(stmt);
            sqlite3_bind_text(stmt, 1, titleid.data(), titleid.size(), nullptr);
            sqlite3_bind_text(stmt, 2, path.data(), path.size(), nullptr);
            sqlite3_bind_text(
                    stmt, 3, app_version.data(), app_version.size(), nullptr);

            auto err = sqlite3_step(stmt);
            if (err != SQLITE_DONE)
                throw std::runtime_error(fmt::format(
                        "can't execute SQL statement:\n{}",
                        sqlite3_errmsg(_sqliteDb.get())));
        }
        catch (const std::exception& e)
        {
            throw formatEx<std::runtime_error>(
                    "failed to parse line\n{}\n{}", current_line, e.what());
        }
    }
}

void CompPackDatabase::update(Http* http, const std::string& update_url)
{
    std::string db_data;
    db_data.resize(MAX_DB_SIZE);
    uint64_t db_size = 0;

    if (update_url.empty())
        throw std::runtime_error("no comp pack url");

    LOGF("loading comp pack list from {}", update_url);

    http->start(update_url, 0);

    const auto length = http->get_length();

    if (length > (int64_t)db_data.size())
        throw std::runtime_error(
                "comp pack list is too large... check for newer pkgj version");

    for (;;)
    {
        uint32_t want = (uint32_t)min64(64 * 1024, db_data.size() - db_size);
        int read = http->read(
                reinterpret_cast<uint8_t*>(db_data.data()) + db_size, want);
        if (read == 0)
            break;
        db_size += read;
    }

    if (db_size == 0)
        throw std::runtime_error(
                "comp pack list is empty... check for newer pkgj version");

    LOG("parsing items");

    db_data.resize(db_size);
    parse_entries(db_data);

    LOG("finished parsing");
}

std::optional<CompPackDatabase::Item> CompPackDatabase::get(
        const std::string& titleid)
{
    // we need to reopen the db before every query because for some reason,
    // after the app is suspended, all further query will return disk I/O error
    reopen();

    LOG("reloading database");

    sqlite3_stmt* stmt;
    SQLITE_CHECK(
            sqlite3_prepare_v2(
                    _sqliteDb.get(),
                    "SELECT path, app_version "
                    "FROM entries "
                    "WHERE titleid = ? ",
                    -1,
                    &stmt,
                    nullptr),
            "can't prepare SQL statement");
    BOOST_SCOPE_EXIT_ALL(&)
    {
        sqlite3_finalize(stmt);
    };

    sqlite3_bind_text(stmt, 1, titleid.data(), titleid.size(), nullptr);

    auto const err = sqlite3_step(stmt);
    if (err == SQLITE_DONE)
        return std::nullopt;
    if (err != SQLITE_ROW)
        throw std::runtime_error(fmt::format(
                "can't execute SQL statement:\n{}",
                sqlite3_errmsg(_sqliteDb.get())));

    std::string app_version =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    // replace _ by .
    app_version[2] = '.';

    return Item{
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
            app_version,
    };
}
