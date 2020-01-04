#pragma once

#include "http.hpp"
#include "thread.hpp"

#include <vita2d.h>

class ImageFetcher
{
public:
    enum class Status
    {
        Fetching,
        NoUpdate,
        Found,
        Error,
    };

    ImageFetcher(DbItem *item);
    ~ImageFetcher();

    Status get_status();
    vita2d_texture* get_texture();

private:
    Mutex _mutex;

    std::string _url;

    bool _abort{false};
    Status _status{Status::Fetching};
    std::unique_ptr<Http> _http;
    vita2d_texture* _texture;

    Thread _thread;

    void do_request();
};
