#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

#include <stdint.h>

extern "C" {
#include "aes128.h"
#include "sha256.h"
}

#include "http.hpp"

#define PKGI_RIF_SIZE 512
#define PKGI_PSM_RIF_SIZE 1024
#define PKG_HEADER_SIZE 192
#define PKG_HEADER_EXT_SIZE 64
#define PKG_TAIL_SIZE 480

class ResumeError : public std::exception
{
public:
    ResumeError(std::string msg) : _msg(std::move(msg))
    {
    }

    virtual const char* what() const noexcept override
    {
        return _msg.c_str();
    }

private:
    std::string _msg;
};

class DownloadError : public std::exception
{
public:
    DownloadError(std::string msg) : _msg(std::move(msg))
    {
    }

    virtual const char* what() const noexcept override
    {
        return _msg.c_str();
    }

private:
    std::string _msg;
};

class Download
{
public:
    Download(std::unique_ptr<Http> http);

    int pkgi_download(
            const char* partition,
            const char* content,
            const char* url,
            const uint8_t* rif,
            const uint8_t* digest);

    // private:
    // temporary unpack folder ux0:pkgi/TITLE
    bool save_as_iso{false};

    std::string root;

    std::unique_ptr<Http> _http;
    const char* download_content;
    const char* download_url;

    uint64_t download_offset; // pkg absolute offset
    uint64_t download_size;   // pkg total size (from http request)

    uint8_t iv[AES_BLOCK_SIZE];
    aes128_ctx aes;
    sha256_ctx sha;

    void* item_file;       // current file handle
    std::string item_name; // current file name
    std::string item_path; // current file path
    uint32_t item_index;   // current item

    // head.bin contents, kept in memory while downloading
    std::vector<uint8_t> head;

    // pkg header
    uint32_t index_count;
    uint64_t total_size;
    uint64_t enc_offset;
    uint64_t enc_size;

    uint32_t content_type;

    // encrypted files
    uint64_t encrypted_base;   // offset in pkg where it starts
    uint64_t encrypted_offset; // offset from beginning of file
    uint64_t decrypted_size;   // size that's left to write into decrypted file

    uint64_t last_state_save;

    bool resuming;

    // UI stuff
    uint32_t info_start;
    uint32_t info_update;
    std::function<void(uint64_t download_offset, uint64_t download_size)>
            update_progress_cb;
    std::function<void(const std::string& status)> update_status;
    std::function<bool()> is_canceled;

    void update_progress();
    void download_start(void);
    void download_data(uint8_t* buffer, uint32_t size, int encrypted, int save);
    void skip_to_file_offset(uint64_t to_offset);
    void create_file(void);
    void open_file();
    int download_head(const uint8_t* rif);
    void download_file_content(uint64_t encrypted_size);
    void download_file_content_to_iso(uint64_t item_size);
    void download_file_content_to_pspkey(uint64_t item_size);
    int download_files(void);
    int download_tail(void);
    int create_stat();
    int check_integrity(const uint8_t* digest);
    int create_rif(const uint8_t* rif);
    int create_psm_rif(const uint8_t* rif);
    int adjust_psm_files(void);

    void serialize_state() const;
    void deserialize_state();
};
