#include "imagefetcher.hpp"

#include "db.hpp"
#include "pkgi.hpp"
#include "file.hpp"
#include "vitahttp.hpp"

#include <fmt/format.h>
#include <mutex>
#include <optional>

std::string get_image_url(DbItem* item)
{
    std::string country_abbv = "USA";
    std::string language = "en";
    switch (pkgi_get_region(item->titleid))
    {
    case RegionASA:
    {
        language = "zh";
        country_abbv = "HK";
        const std::string region = item->content.substr(0, 6);
        if (item->name.find("CHN") != std::string::npos)
        {
            country_abbv = "CN";
        }
        else if (region.compare("HP0507") == 0)
        {
            language = "ko";
            country_abbv = "KR";
        }
        else if (region.compare("HP2005"))
        {
            language = "en";
        }
    }
    break;
    case RegionJPN:
        country_abbv = "JP";
        language = "ja";
        break;
    case RegionEUR:
        country_abbv = "GB";
        break;
    default:
        country_abbv = "US";
    }
    return fmt::format(
            "https://store.playstation.com/store/api/chihiro/"
            "00_09_000/container/{}/{}/19/{}/{}/image?w=248&h=248",
            country_abbv,
            language,
            item->content,
            pkgi_time_msec());
}

ImageFetcher::ImageFetcher(DbItem* item)
    : _mutex("image_fetcher_mutex")
    , _path(fmt::format("ux0:pkgj/cover/{}.jpg", item->titleid))
    , _url(get_image_url(item))
    , _texture(nullptr)
    , _thread("image_fetcher", [this] { do_request(); })
{
    pkgi_mkdirs("ux0:pkgj/cover");
}

ImageFetcher::~ImageFetcher()
{
    Http* http;
    {
        std::lock_guard<Mutex> lock(_mutex);
        _abort = true;
        http = _http.get();
    }
    if (http)
        http->abort();
    _thread.join();
}

vita2d_texture* ImageFetcher::get_texture()
{
    std::lock_guard<Mutex> lock(_mutex);
    return _texture;
}

std::optional<std::vector<uint8_t>> download_data(
        Http* http, const std::string& url)
{
    std::vector<uint8_t> data;
    http->start(url, 0);
    if (http->get_status() == 404)
        return std::nullopt;
    size_t pos = 0;
    while (true)
    {
        if (pos == data.size())
            data.resize(pos + 4096);
        const auto read = http->read(data.data() + pos, data.size() - pos);
        if (read == 0)
            break;
        pos += read;
    }
    data.resize(pos);
    return data;
}

void ImageFetcher::do_request()
{
    try
    {
        if (pkgi_file_exists(_path.c_str()))
        {
            std::lock_guard<Mutex> lock(_mutex);
            _texture = vita2d_load_JPEG_file(_path.c_str());
            if (_texture) return;
        }
        {
            std::lock_guard<Mutex> lock(_mutex);
            if (_abort)
                return;
            _http = std::make_unique<VitaHttp>();
        }
        const auto image = download_data(_http.get(), _url);
        {
            std::lock_guard<Mutex> lock(_mutex);
            if (image && !image->empty())
                _texture = vita2d_load_JPEG_buffer(image->data(), image->size());
            _http = nullptr;
        }
        if (image && !image->empty())
        {
            auto image_file = pkgi_create(_path.c_str());
            pkgi_write(image_file, image->data(), image->size());
            pkgi_close(image_file);
        }
    }
    catch (const std::exception& e)
    {
        LOGF("Failed to fetch patch info: {}", e.what());
        std::lock_guard<Mutex> lock(_mutex);
        _http = nullptr;
    }
}
