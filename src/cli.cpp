#include "comppackdb.hpp"
#include "db.hpp"
#include "download.hpp"
#include "extractzip.hpp"
#include "filedownload.hpp"
#include "filehttp.hpp"
#include "patchinfo.hpp"
#include "zrif.hpp"

#include <boost/algorithm/hex.hpp>

#include <fmt/format.h>

#include <memory>

static constexpr auto USAGE =
        "Usage: %s [extract <filename> <zrif> <sha256>] [refreshlist PSV "
        "path] [refreshcomppack path] [filedownload path] [extractzip path] "
        "[patchinfo xmlfile titleid]\n";

int extract(int argc, char* argv[])
{
    if (argc != 5)
    {
        printf(USAGE, argv[0]);
        return 1;
    }

    std::vector<uint8_t> digest;
    boost::algorithm::unhex(std::string(argv[4]), std::back_inserter(digest));

    uint8_t rif[PKGI_PSM_RIF_SIZE];
    char message[256];
    if (argv[3][0] && !pkgi_zrif_decode(argv[3], rif, message, sizeof(message)))
        throw std::runtime_error(fmt::format("can't decode zrif: {}", message));

    Download d(std::make_unique<FileHttp>());

    d.save_as_iso = false;
    d.update_progress_cb = [](uint64_t, uint64_t) {};
    d.update_status = [](auto&&) {};
    d.is_canceled = [] { return false; };

    d.pkgi_download(
            "tmp", argv[2], argv[2], argv[3][0] ? rif : nullptr, digest.data());

    return 0;
}

Mode arg_to_mode(std::string const& arg)
{
    if (arg == "PSVGAMES")
        return ModeGames;
    else
        throw std::runtime_error("unsupported arg: " + arg);
}

int refreshlist(int argc, char* argv[])
{
    if (argc != 4)
    {
        printf(USAGE, argv[0]);
        return 1;
    }

    const auto http = std::make_unique<FileHttp>();

    const auto mode = arg_to_mode(argv[2]);

    const auto db = std::make_unique<TitleDatabase>(".");
    db->update(mode, http.get(), argv[3]);
    db->reload(mode, DbFilterAllRegions, SortBySize, SortDescending, "", "the", {});
    for (unsigned int i = 0; i < db->count(); ++i)
        fmt::print("{}: {}\n", db->get(i)->name, db->get(i)->size);
    fmt::print("{}/{}\n", db->count(), db->total());

    return 0;
}

int refreshcomppack(int argc, char* argv[])
{
    if (argc != 3)
    {
        printf(USAGE, argv[0]);
        return 1;
    }

    const auto http = std::make_unique<FileHttp>();

    const auto db = std::make_unique<CompPackDatabase>("comppack.db");
    db->update(http.get(), argv[2]);
    const auto item = db->get("PCSA00134").value();
    fmt::print("got {} {}\n", item.path, item.app_version);

    return 0;
}

int filedownload(int argc, char* argv[])
{
    if (argc != 3)
    {
        printf(USAGE, argv[0]);
        return 1;
    }

    FileDownload d(std::make_unique<FileHttp>());

    d.download("tmp", "id", argv[2]);

    return 0;
}

int extractzip(int argc, char* argv[])
{
    if (argc != 3)
    {
        printf(USAGE, argv[0]);
        return 1;
    }

    pkgi_extract_zip(argv[2], "tmp");

    return 0;
}

int patchinfo(int argc, char* argv[])
{
    if (argc != 4)
    {
        printf(USAGE, argv[0]);
        return 1;
    }

    const auto patch_info = pkgi_download_patch_info(
            std::make_unique<FileHttp>(argv[2]).get(), argv[3]);

    if (!patch_info)
        puts("No patch found");

    fmt::print("Version: {}\n", patch_info->version);
    fmt::print("Url: {}\n", patch_info->url);

    return 0;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printf(USAGE, argv[0]);
        return 1;
    }

    if (std::string(argv[1]) == "extract")
        return extract(argc, argv);
    if (std::string(argv[1]) == "refreshlist")
        return refreshlist(argc, argv);
    if (std::string(argv[1]) == "refreshcomppack")
        return refreshcomppack(argc, argv);
    if (std::string(argv[1]) == "filedownload")
        return filedownload(argc, argv);
    if (std::string(argv[1]) == "extractzip")
        return extractzip(argc, argv);
    if (std::string(argv[1]) == "patchinfo")
        return patchinfo(argc, argv);

    printf(USAGE, argv[0]);
    return 1;
}
