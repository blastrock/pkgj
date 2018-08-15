#include "download.hpp"

extern "C" {
#include "utils.h"
}
#include "pkgi.hpp"

#include <fmt/format.h>

#include <boost/scope_exit.hpp>

#include <cereal/archives/binary.hpp>

#include <fstream>

#include <cstddef>

static constexpr auto ISO_SECTOR_SIZE = 2048;

enum ContentType
{
    CONTENT_TYPE_PSX_GAME = 6,
    CONTENT_TYPE_PSP_GAME = 7,
    CONTENT_TYPE_PSP_MINI_GAME = 15,
    CONTENT_TYPE_PSV_GAME = 21, // or update
    CONTENT_TYPE_PSV_DLC = 22,
    CONTENT_TYPE_PSM_GAME = 24,
    CONTENT_TYPE_PSM_GAME_ALT = 29, // also sometimes 29
};

// clang-format off
static const uint8_t pkg_psp_key[] = { 0x07, 0xf2, 0xc6, 0x82, 0x90, 0xb5, 0x0d, 0x2c, 0x33, 0x81, 0x8d, 0x70, 0x9b, 0x60, 0xe6, 0x2b };
static const uint8_t pkg_vita_2[] = { 0xe3, 0x1a, 0x70, 0xc9, 0xce, 0x1d, 0xd7, 0x2b, 0xf3, 0xc0, 0x62, 0x29, 0x63, 0xf2, 0xec, 0xcb };
static const uint8_t pkg_vita_3[] = { 0x42, 0x3a, 0xca, 0x3a, 0x2b, 0xd5, 0x64, 0x9f, 0x96, 0x86, 0xab, 0xad, 0x6f, 0xd8, 0x80, 0x1f };
static const uint8_t pkg_vita_4[] = { 0xaf, 0x07, 0xfd, 0x59, 0x65, 0x25, 0x27, 0xba, 0xf1, 0x33, 0x89, 0x66, 0x8b, 0x17, 0xd9, 0xea };
static const uint8_t kirk7_key38[] = { 0x12, 0x46, 0x8d, 0x7e, 0x1c, 0x42, 0x20, 0x9b, 0xba, 0x54, 0x26, 0x83, 0x5e, 0xb0, 0x33, 0x03 };
static const uint8_t kirk7_key39[] = { 0xc4, 0x3b, 0xb6, 0xd6, 0x53, 0xee, 0x67, 0x49, 0x3e, 0xa9, 0x5f, 0xbc, 0x0c, 0xed, 0x6f, 0x8a };
static const uint8_t kirk7_key63[] = { 0x9c, 0x9b, 0x13, 0x72, 0xf8, 0xc6, 0x40, 0xcf, 0x1c, 0x62, 0xf5, 0xd5, 0x92, 0xdd, 0xb5, 0x82 };
static const uint8_t amctl_hashkey_3[] = { 0xe3, 0x50, 0xed, 0x1d, 0x91, 0x0a, 0x1f, 0xd0, 0x29, 0xbb, 0x1c, 0x3e, 0xf3, 0x40, 0x77, 0xfb };
static const uint8_t amctl_hashkey_4[] = { 0x13, 0x5f, 0xa4, 0x7c, 0xab, 0x39, 0x5b, 0xa4, 0x76, 0xb8, 0xcc, 0xa9, 0x8f, 0x3a, 0x04, 0x45 };
static const uint8_t amctl_hashkey_5[] = { 0x67, 0x8d, 0x7f, 0xa3, 0x2a, 0x9c, 0xa0, 0xd1, 0x50, 0x8a, 0xd8, 0x38, 0x5e, 0x4b, 0x01, 0x7e };
// clang-format on

Download::Download(std::unique_ptr<Http> http) : _http(std::move(http))
{
}

void Download::download_start(void)
{
    LOGF("resuming pkg download from {} offset", download_offset);
    info_update = pkgi_time_msec() + 1000;
    update_status("Downloading");
}

void Download::update_progress()
{
    uint32_t info_now = pkgi_time_msec();
    if (info_now >= info_update)
    {
        update_progress_cb(download_offset, download_size);
        info_update = info_now + 500;
    }
}

// lzrc decompression code from libkirk by tpu
typedef struct
{
    // input stream
    const uint8_t* input;
    uint32_t in_ptr;
    uint32_t in_len;

    // output stream
    uint8_t* output;
    uint32_t out_ptr;
    uint32_t out_len;

    // range decode
    uint32_t range;
    uint32_t code;
    uint32_t out_code;
    uint8_t lc;

    uint8_t bm_literal[8][256];
    uint8_t bm_dist_bits[8][39];
    uint8_t bm_dist[18][8];
    uint8_t bm_match[8][8];
    uint8_t bm_len[8][31];
} lzrc_decode;

static void rc_init(
        lzrc_decode* rc, void* out, int out_len, const void* in, int in_len)
{
    if (in_len < 5)
    {
        throw DownloadError(
                "internal error - lzrc input underflow! pkg may be corrupted");
    }

    rc->input = static_cast<const uint8_t*>(in);
    rc->in_len = in_len;
    rc->in_ptr = 5;

    rc->output = static_cast<uint8_t*>(out);
    rc->out_len = out_len;
    rc->out_ptr = 0;

    rc->range = 0xffffffff;
    rc->lc = rc->input[0];
    rc->code = get32be(rc->input + 1);
    rc->out_code = 0xffffffff;

    memset(rc->bm_literal, 0x80, sizeof(rc->bm_literal));
    memset(rc->bm_dist_bits, 0x80, sizeof(rc->bm_dist_bits));
    memset(rc->bm_dist, 0x80, sizeof(rc->bm_dist));
    memset(rc->bm_match, 0x80, sizeof(rc->bm_match));
    memset(rc->bm_len, 0x80, sizeof(rc->bm_len));
}

static void normalize(lzrc_decode* rc)
{
    if (rc->range < 0x01000000)
    {
        rc->range <<= 8;
        rc->code = (rc->code << 8) + rc->input[rc->in_ptr];
        rc->in_ptr++;
    }
}

static int rc_bit(lzrc_decode* rc, uint8_t* prob)
{
    uint32_t bound;

    normalize(rc);

    bound = (rc->range >> 8) * (*prob);
    *prob -= *prob >> 3;

    if (rc->code < bound)
    {
        rc->range = bound;
        *prob += 31;
        return 1;
    }
    else
    {
        rc->code -= bound;
        rc->range -= bound;
        return 0;
    }
}

static int rc_bittree(lzrc_decode* rc, uint8_t* probs, int limit)
{
    int number = 1;

    do
    {
        number = (number << 1) + rc_bit(rc, probs + number);
    } while (number < limit);

    return number;
}

static int rc_number(lzrc_decode* rc, uint8_t* prob, uint32_t n)
{
    int number = 1;

    if (n > 3)
    {
        number = (number << 1) + rc_bit(rc, prob + 3);
        if (n > 4)
        {
            number = (number << 1) + rc_bit(rc, prob + 3);
            if (n > 5)
            {
                // direct bits
                normalize(rc);

                for (uint32_t i = 0; i < n - 5; i++)
                {
                    rc->range >>= 1;
                    number <<= 1;
                    if (rc->code < rc->range)
                    {
                        number += 1;
                    }
                    else
                    {
                        rc->code -= rc->range;
                    }
                }
            }
        }
    }

    if (n > 0)
    {
        number = (number << 1) + rc_bit(rc, prob);
        if (n > 1)
        {
            number = (number << 1) + rc_bit(rc, prob + 1);
            if (n > 2)
            {
                number = (number << 1) + rc_bit(rc, prob + 2);
            }
        }
    }

    return number;
}

static int lzrc_decompress(void* out, int out_len, const void* in, int in_len)
{
    lzrc_decode rc;
    rc_init(&rc, out, out_len, in, in_len);

    if (rc.lc & 0x80)
    {
        // plain text
        memcpy(rc.output, rc.input + 5, rc.code);
        return rc.code;
    }

    int rc_state = 0;
    uint8_t last_byte = 0;

    for (;;)
    {
        uint32_t match_step = 0;

        int bit = rc_bit(&rc, &rc.bm_match[rc_state][match_step]);
        if (bit == 0) // literal
        {
            if (rc_state > 0)
            {
                rc_state -= 1;
            }

            int byte = rc_bittree(
                    &rc,
                    &rc.bm_literal[((last_byte >> rc.lc) & 0x07)][0],
                    0x100);
            byte -= 0x100;

            if (rc.out_ptr == rc.out_len)
            {
                throw DownloadError(
                        "internal error - lzrc output overflow! pkg may be "
                        "corrupted");
            }
            rc.output[rc.out_ptr++] = (uint8_t)byte;
            last_byte = (uint8_t)byte;
        }
        else // match
        {
            // find bits of match length
            uint32_t len_bits = 0;
            for (int i = 0; i < 7; i++)
            {
                match_step += 1;
                bit = rc_bit(&rc, &rc.bm_match[rc_state][match_step]);
                if (bit == 0)
                {
                    break;
                }
                len_bits += 1;
            }

            // find match length
            uint32_t match_len;
            if (len_bits == 0)
            {
                match_len = 1;
            }
            else
            {
                uint32_t len_state = ((len_bits - 1) << 2) +
                                     ((rc.out_ptr << (len_bits - 1)) & 0x03);
                match_len = rc_number(
                        &rc, &rc.bm_len[rc_state][len_state], len_bits);
                if (match_len == 0xFF)
                {
                    // end of stream
                    return rc.out_ptr;
                }
            }

            // find number of bits of match distance
            uint32_t dist_state = 0;
            uint32_t limit = 8;
            if (match_len > 2)
            {
                dist_state += 7;
                limit = 44;
            }
            int dist_bits = rc_bittree(
                    &rc, &rc.bm_dist_bits[len_bits][dist_state], limit);
            dist_bits -= limit;

            // find match distance
            uint32_t match_dist;
            if (dist_bits > 0)
            {
                match_dist =
                        rc_number(&rc, &rc.bm_dist[dist_bits][0], dist_bits);
            }
            else
            {
                match_dist = 1;
            }

            // copy match bytes
            if (match_dist > rc.out_ptr)
            {
                throw DownloadError(
                        "internal error - lzrc match_dist out of range! pkg "
                        "may be corrupted");
            }

            if (rc.out_ptr + match_len + 1 > rc.out_len)
            {
                throw DownloadError(
                        "internal error - lzrc output overflow! pkg may be "
                        "corrupted");
            }

            const uint8_t* match_src = rc.output + rc.out_ptr - match_dist;
            for (uint32_t i = 0; i <= match_len; i++)
            {
                rc.output[rc.out_ptr++] = *match_src++;
            }
            last_byte = match_src[-1];

            rc_state = 6 + ((rc.out_ptr + 1) & 1);
        }
    }
}

static void init_psp_decrypt(
        aes128_ctx* key,
        uint8_t* iv,
        int eboot,
        const uint8_t* mac,
        const uint8_t* header,
        uint32_t offset1,
        uint32_t offset2)
{
    uint8_t tmp[16];
    aes128_init_dec(key, kirk7_key63);
    if (eboot)
    {
        aes128_decrypt(key, header + offset1, tmp);
    }
    else
    {
        memcpy(tmp, header + offset1, 16);
    }

    aes128_ctx aes;
    aes128_init_dec(&aes, kirk7_key38);
    aes128_decrypt(&aes, tmp, tmp);

    for (size_t i = 0; i < 16; i++)
    {
        iv[i] = mac[i] ^ tmp[i] ^ header[offset2 + i] ^ amctl_hashkey_3[i] ^
                amctl_hashkey_5[i];
    }
    aes128_init_dec(&aes, kirk7_key39);
    aes128_decrypt(&aes, iv, iv);

    for (size_t i = 0; i < 16; i++)
    {
        iv[i] ^= amctl_hashkey_4[i];
    }
}

void Download::download_data(
        uint8_t* buffer, uint32_t size, int encrypted, int save)
{
    if (is_canceled())
        throw std::runtime_error("download was canceled");

    if (size == 0)
        return;

    update_progress();

    if (!*_http)
    {
        LOGF("requesting {} @ {}", download_url, download_offset);
        _http->start(download_url, download_offset);

        const int64_t http_length = _http->get_length();
        if (http_length < 0)
        {
            throw DownloadError("HTTP response has unknown length");
        }

        download_size = http_length + download_offset;

        LOGF("http response length = {}, total pkg size = {}",
             http_length,
             download_size);
        info_start = pkgi_time_msec();
        info_update = pkgi_time_msec() + 500;
    }

    {
        size_t pos = 0;
        while (pos < size)
        {
            const int read = _http->read(buffer + pos, size - pos);
            if (read == 0)
                throw DownloadError("HTTP connection closed");
            pos += read;
        }
    }

    download_offset += size;

    sha256_update(&sha, buffer, size);

    if (encrypted)
    {
        aes128_ctr(&aes, iv, encrypted_base + encrypted_offset, buffer, size);
        encrypted_offset += size;
    }

    if (save)
    {
        uint32_t write;
        if (encrypted)
        {
            write = (uint32_t)min64(decrypted_size, size);
            decrypted_size -= write;
        }
        else
        {
            write = size;
        }

        if (!pkgi_write(item_file, buffer, write))
            throw formatEx<DownloadError>("failed to write to {}", item_path);
    }
}

void Download::skip_to_file_offset(uint64_t to_offset)
{
    if (to_offset < encrypted_offset)
        throw DownloadError(
                fmt::format("can't seek backward to {}", to_offset));
    std::vector<uint8_t> tmp(to_offset - encrypted_offset);
    download_data(tmp.data(), tmp.size(), 1, 0);
}

// this includes creating of all the parent folders necessary to actually
// create file
void Download::create_file()
{
    std::string folder = item_path;
    folder.erase(folder.rfind('/'));

    pkgi_mkdirs(folder.c_str());

    LOGF("creating {} file", item_name);
    item_file = pkgi_create(item_path.c_str());
    if (!item_file)
        throw formatEx<DownloadError>("cannot create file {}", item_name);
}

void Download::open_file()
{
    LOGF("opening {} file for resume", item_name);
    item_file = pkgi_openrw(item_path.c_str());
    if (!item_file)
        throw formatEx<DownloadError>("cannot create file {}", item_name);
}

int Download::download_head(const uint8_t* rif)
{
    LOG("downloading pkg head");

    item_name = "Preparing...";
    item_path = fmt::format("{}/sce_sys/package/head.bin", root);

    BOOST_SCOPE_EXIT_ALL(&)
    {
        if (item_file)
        {
            pkgi_close(item_file);
            item_file = NULL;
        }
    };

    create_file();

    head.resize(PKG_HEADER_SIZE + PKG_HEADER_EXT_SIZE);
    download_data(head.data(), head.size(), 0, 1);

    if (get32be(head.data()) != 0x7f504b47 ||
        get32be(head.data() + PKG_HEADER_SIZE) != 0x7F657874)
    {
        throw DownloadError("wrong pkg header");
    }

    // contentid is at 0x50 for psm games
    if (rif && !(pkgi_memequ(rif + 0x10, head.data() + 0x30, 0x30) || pkgi_memequ(rif + 0x50, head.data() + 0x30, 0x30)))
    {
        throw DownloadError("zRIF content id doesn't match pkg");
    }

    const auto meta_offset = get32be(head.data() + 8);
    const auto meta_count = get32be(head.data() + 12);
    index_count = get32be(head.data() + 20);
    total_size = get64be(head.data() + 24);
    enc_offset = get64be(head.data() + 32);
    enc_size = get64be(head.data() + 40);
    LOGF("meta_offset={} meta_count={} index_count={} total_size={} "
         "enc_offset={} enc_size={}",
         meta_offset,
         meta_count,
         index_count,
         total_size,
         enc_offset,
         enc_size);

    pkgi_memcpy(iv, head.data() + 0x70, sizeof(iv));

    uint8_t key[AES_BLOCK_SIZE];
    int key_type = head[0xe7] & 7;
    if (key_type == 1)
    {
        pkgi_memcpy(key, pkg_psp_key, sizeof(key));
    }
    else if (key_type == 2)
    {
        aes128_ctx ctx;
        aes128_init(&ctx, pkg_vita_2);
        aes128_encrypt(&ctx, iv, key);
    }
    else if (key_type == 3)
    {
        aes128_ctx ctx;
        aes128_init(&ctx, pkg_vita_3);
        aes128_encrypt(&ctx, iv, key);
    }
    else if (key_type == 4)
    {
        aes128_ctx ctx;
        aes128_init(&ctx, pkg_vita_4);
        aes128_encrypt(&ctx, iv, key);
    }
    else
        throw DownloadError("invalid key type " + std::to_string(key_type));

    aes128_ctr_init(&aes, key);

    head.resize(enc_offset);
    download_data(
            head.data() + PKG_HEADER_SIZE + PKG_HEADER_EXT_SIZE,
            enc_offset - (PKG_HEADER_SIZE + PKG_HEADER_EXT_SIZE),
            0,
            1);

    uint32_t index_size = 0;

    uint32_t offset = meta_offset;
    for (uint32_t i = 0; i < meta_count; i++)
    {
        if (offset + 16 >= enc_offset)
        {
            throw DownloadError("pkg file is too small or corrupted");
        }

        uint32_t type = get32be(head.data() + offset + 0);
        uint32_t size = get32be(head.data() + offset + 4);

        if (type == 2)
        {
            content_type = get32be(head.data() + offset + 8);
            if (content_type != CONTENT_TYPE_PSX_GAME &&
                content_type != CONTENT_TYPE_PSP_GAME &&
                content_type != CONTENT_TYPE_PSP_MINI_GAME &&
                content_type != CONTENT_TYPE_PSM_GAME &&
                content_type != CONTENT_TYPE_PSM_GAME_ALT &&
                content_type != CONTENT_TYPE_PSV_GAME &&
                content_type != CONTENT_TYPE_PSV_DLC)
            {
                throw DownloadError(
                        "unsupported package type: " +
                        std::to_string(content_type));
            }
        }
        else if (type == 13)
        {
            // index_offset = get32be(head.data() + offset + 8);
            index_size = get32be(head.data() + offset + 12);
        }
        offset += 8 + size;
    }

    head.resize(head.size() + index_count * 32);
    download_data(head.data() + enc_offset, index_count * 32, 0, 1);

    uint64_t item_offset;
    {
        uint8_t item[32];
        pkgi_memcpy(item, head.data() + enc_offset, sizeof(item));
        aes128_ctr(&aes, iv, 0, item, sizeof(item));

        item_offset = get64be(item + 8);
    }

    if (index_size && item_offset != index_size)
    {
        throw DownloadError(
                "assertion error: read-ahead mismatch, expected: " +
                std::to_string(index_size) +
                ", got: " + std::to_string(item_offset));
    }

    const auto target_size = (uint32_t)(enc_offset + item_offset);
    head.resize(target_size);
    download_data(
            head.data() + enc_offset + index_count * 32,
            target_size - (enc_offset + index_count * 32),
            0,
            1);

    LOG("head.bin downloaded");
    return 1;
}

void Download::download_file_content(uint64_t encrypted_size)
{
    static constexpr auto SAVE_PERIOD = 10 * 1024 * 1024;

    std::vector<uint8_t> down(1 * 1024 * 1024);
    while (encrypted_offset != encrypted_size)
    {
        const uint32_t read =
                (uint32_t)min64(down.size(), encrypted_size - encrypted_offset);
        download_data(down.data(), read, 1, 1);

        if ((encrypted_base + encrypted_offset - last_state_save) /
                    SAVE_PERIOD >=
            1)
            serialize_state();
    }
}

void Download::download_file_content_to_iso(uint64_t item_size)
{
    if (item_size < 0x28)
        throw DownloadError("eboot.pbp file is too small");

    uint8_t eboot_header[0x28];
    download_data(eboot_header, sizeof(eboot_header), 1, 0);

    if (memcmp(eboot_header, "\x00PBP", 4) != 0)
        throw DownloadError("wrong eboot.pbp header magic");

    uint32_t const psar_offset = get32le(eboot_header + 0x24);
    if (psar_offset + 256 > item_size)
        throw DownloadError("eboot.pbp file is to short");
    if (psar_offset % 16 != 0)
        throw DownloadError("psar_offset is not aligned");

    skip_to_file_offset(psar_offset);

    std::vector<uint8_t> psar_header(256);
    download_data(psar_header.data(), psar_header.size(), 1, 0);

    if (memcmp(psar_header.data(), "NPUMDIMG", 8) != 0)
        throw DownloadError("wrong data.psar header magic");

    uint32_t const iso_block = get32le(psar_header.data() + 0x0c);
    if (iso_block > 16)
        throw DownloadError(fmt::format(
                "unsupported data.psar block size %u, max %u supported",
                iso_block,
                16));

    uint8_t mac[16];
    aes128_cmac(kirk7_key38, psar_header.data(), 0xc0, mac);

    aes128_ctx psp_key;
    uint8_t psp_iv[16];
    init_psp_decrypt(&psp_key, psp_iv, 1, mac, psar_header.data(), 0xc0, 0xa0);
    aes128_psp_decrypt(&psp_key, psp_iv, 0, psar_header.data() + 0x40, 0x60);

    uint32_t iso_start = get32le(psar_header.data() + 0x54);
    uint32_t iso_end = get32le(psar_header.data() + 0x64);
    uint32_t iso_total = iso_end - iso_start - 1;
    uint32_t block_count = (iso_total + iso_block - 1) / iso_block;

    uint32_t iso_table = get32le(psar_header.data() + 0x6c);

    if (iso_table + block_count * 32 > item_size)
        throw DownloadError("offset table in data.psar file is too large");

    uint64_t const table_offset = psar_offset + iso_table;
    skip_to_file_offset(table_offset);

    std::vector<std::array<uint8_t, 32>> tables(block_count);
    for (auto& table : tables)
        download_data(table.data(), table.size(), 1, 0);

    for (uint32_t i = 0; i < block_count; i++)
    {
        auto const& table = tables[i];

        uint32_t t[8];
        for (size_t k = 0; k < 8; k++)
            t[k] = get32le(table.data() + k * 4);

        uint32_t block_offset = t[4] ^ t[2] ^ t[3];
        uint32_t block_size = t[5] ^ t[1] ^ t[2];
        uint32_t block_flags = t[6] ^ t[0] ^ t[3];

        if (psar_offset + block_size > item_size)
            throw DownloadError(fmt::format(
                    "iso block size/offset is to large: {} > {}",
                    psar_offset + block_size,
                    item_size));

        std::vector<uint8_t> data(16 * ISO_SECTOR_SIZE);

        uint64_t abs_offset = psar_offset + block_offset;
        skip_to_file_offset(abs_offset);
        download_data(data.data(), block_size, 1, 0);

        if ((block_flags & 4) == 0)
        {
            aes128_psp_decrypt(
                    &psp_key,
                    psp_iv,
                    block_offset / 16,
                    data.data(),
                    block_size);
        }

        if (block_size == iso_block * ISO_SECTOR_SIZE)
        {
            if (!pkgi_write(item_file, data.data(), block_size))
                throw DownloadError(
                        fmt::format("failed to write to %s", item_path));
        }
        else
        {
            std::vector<uint8_t> uncompressed(16 * ISO_SECTOR_SIZE);
            auto const out_size = lzrc_decompress(
                    uncompressed.data(),
                    uncompressed.size(),
                    data.data(),
                    block_size);
            if (out_size != int(iso_block) * ISO_SECTOR_SIZE)
            {
                throw DownloadError(
                        "internal error - lzrc decompression failed! "
                        "pkg may be corrupted");
            }
            if (!pkgi_write(item_file, uncompressed.data(), out_size))
                throw DownloadError(
                        fmt::format("failed to write to %s", item_path));
        }
    }

    skip_to_file_offset(item_size);
}

void Download::download_file_content_to_pspkey(uint64_t item_size)
{
    if (item_size < 0x90 + 0xa0)
        throw DownloadError("PSP-KEY.EDAT file is too short");

    skip_to_file_offset(0x90);

    uint8_t key_header[0xa0];
    download_data(key_header, sizeof(key_header), 1, 0);

    if (memcmp(key_header, "\x00PGD", 4) != 0)
        throw DownloadError("wrong PSP-KEY.EDAT header magic");

    uint32_t key_index = get32le(key_header + 4);
    uint32_t drm_type = get32le(key_header + 8);
    if (key_index != 1 || drm_type != 1)
        throw DownloadError(
                "unsupported PSP-KEY.EDAT file, key/drm type is wrong");

    uint8_t mac[16];
    aes128_cmac(kirk7_key38, key_header, 0x70, mac);

    aes128_ctx psp_key;
    uint8_t psp_iv[16];
    init_psp_decrypt(&psp_key, psp_iv, 0, mac, key_header, 0x70, 0x10);
    aes128_psp_decrypt(&psp_key, psp_iv, 0, key_header + 0x30, 0x30);

    uint32_t data_size = get32le(key_header + 0x44);
    uint32_t data_offset = get32le(key_header + 0x4c);

    if (data_size != 0x10 || data_offset != 0x90)
        throw DownloadError(
                "unsupported PSP-KEY.EDAT file, data/offset is wrong");

    init_psp_decrypt(&psp_key, psp_iv, 0, mac, key_header, 0x70, 0x30);
    aes128_psp_decrypt(&psp_key, psp_iv, 0, key_header + 0x90, 0x10);

    if (!pkgi_write(item_file, key_header + 0x90, 0x10))
        throw DownloadError(fmt::format("failed to write to %s", item_path));

    skip_to_file_offset(item_size);
}

int Download::download_files(void)
{
    LOG("downloading encrypted files");

    BOOST_SCOPE_EXIT_ALL(&)
    {
        if (item_file)
        {
            pkgi_close(item_file);
            item_file = NULL;
        }
    };

    for (; item_index < index_count; ++item_index)
    {
        uint8_t item[32];
        pkgi_memcpy(
                item,
                head.data() + enc_offset + sizeof(item) * item_index,
                sizeof(item));
        aes128_ctr(&aes, iv, sizeof(item) * item_index, item, sizeof(item));

        const uint32_t name_offset = get32be(item + 0);
        const uint32_t name_size = get32be(item + 4);
        const uint64_t item_offset = get64be(item + 8);
        const uint64_t item_size = get64be(item + 16);
        const uint8_t type = item[27];

        if (enc_offset + name_offset + name_size > total_size)
            throw DownloadError("pkg file is too small or corrupted");

        {
            std::vector<uint8_t> item_name_v(
                    head.begin() + enc_offset + name_offset,
                    head.begin() + enc_offset + name_offset + name_size);
            aes128_ctr(
                    &aes,
                    iv,
                    name_offset,
                    item_name_v.data(),
                    item_name_v.size());
            item_name = std::string(
                    reinterpret_cast<const char*>(item_name_v.data()),
                    item_name_v.size());
        }

        const uint64_t encrypted_size = (item_size + AES_BLOCK_SIZE - 1) &
                                        ~((uint64_t)AES_BLOCK_SIZE - 1);

        if (!resuming)
        {
            decrypted_size = item_size;
            encrypted_base = item_offset;
            encrypted_offset = 0;
        }

        LOGF("[{}/{}] {} item_offset={} item_size={} type={}",
             item_index + 1,
             index_count,
             item_name,
             item_offset,
             item_size,
             type);

        if (content_type == CONTENT_TYPE_PSX_GAME ||
            content_type == CONTENT_TYPE_PSP_GAME ||
            content_type == CONTENT_TYPE_PSP_MINI_GAME)
        {
            if (item_name == "USRDIR/CONTENT/DOCUMENT.DAT")
                item_path = fmt::format("{}/DOCUMENT.DAT", root);
            else if (item_name == "USRDIR/CONTENT/EBOOT.PBP")
                item_path = fmt::format("{}/EBOOT.PBP", root);
            else if (item_name == "USRDIR/CONTENT/CONTENT.DAT")
                item_path = fmt::format("{}/CONTENT.DAT", root);
            else if (item_name == "USRDIR/CONTENT/PSP-KEY.EDAT")
                item_path = fmt::format("{}/PSP-KEY.EDAT", root);
            else
            {
                skip_to_file_offset(encrypted_size);
                continue;
            }
        }
        else if (content_type == CONTENT_TYPE_PSM_GAME ||
                 content_type == CONTENT_TYPE_PSM_GAME_ALT)
        {
            // skip "content/" prefix
            item_path = fmt::format("{}/RO/{}", root, item_name.c_str() + 8);
        }
        else
            item_path = fmt::format("{}/{}", root, item_name);

        if (type == 4)
        {
            pkgi_mkdirs(item_path.c_str());
            continue;
        }
        else if (type == 18)
        {
            continue;
        }

        if (resuming)
        {
            open_file();
            if (pkgi_seek(item_file, encrypted_offset) < 0)
                throw ResumeError("failed to seek for resume");
        }
        else
            create_file();

        if (enc_offset + item_offset + encrypted_offset != download_offset)
            throw formatEx<DownloadError>(
                    "pkg is not supported, files are in wrong order, "
                    "expected: {}, actual: {}",
                    enc_offset + item_offset + encrypted_offset,
                    +download_offset);

        if (enc_offset + item_offset + item_size > total_size)
            throw DownloadError("pkg file is too small or corrupted");

        if (content_type == CONTENT_TYPE_PSP_GAME ||
            content_type == CONTENT_TYPE_PSP_MINI_GAME)
        {
            if (save_as_iso && item_name == "USRDIR/CONTENT/EBOOT.PBP")
                download_file_content_to_iso(item_size);
            else if (item_name == "USRDIR/CONTENT/PSP-KEY.EDAT")
                download_file_content_to_pspkey(item_size);
            else
                download_file_content(encrypted_size);
        }
        else
            download_file_content(encrypted_size);

        pkgi_close(item_file);
        item_file = NULL;

        resuming = false;
    }

    LOG("all files decrypted");
    return 1;
}

int Download::download_tail(void)
{
    LOG("downloading tail.bin");

    BOOST_SCOPE_EXIT_ALL(&)
    {
        if (item_file)
        {
            pkgi_close(item_file);
            item_file = NULL;
        }
    };

    item_name = "Finishing...";
    item_path = fmt::format("{}/sce_sys/package/tail.bin", root);

    create_file();

    std::vector<uint8_t> down(1 * 1024 * 1024);
    uint64_t tail_offset = enc_offset + enc_size;
    while (download_offset < tail_offset)
    {
        const auto read =
                (uint32_t)min64(down.size(), tail_offset - download_offset);
        download_data(down.data(), read, 0, 0);
    }

    while (download_offset != total_size)
    {
        const auto read =
                (uint32_t)min64(down.size(), total_size - download_offset);
        download_data(
                down.data(), read, 0, content_type != CONTENT_TYPE_PSX_GAME);
    }

    LOG("tail.bin downloaded");
    return 1;
}

int Download::check_integrity(const uint8_t* digest)
{
    if (!digest)
    {
        LOG("no integrity provided, skipping check");
        return 1;
    }

    uint8_t check[SHA256_DIGEST_SIZE];
    sha256_finish(&sha, check);

    LOG("checking integrity of pkg");
    if (!pkgi_memequ(digest, check, SHA256_DIGEST_SIZE))
    {
        LOG("pkg integrity is wrong, removing head.bin & resume data");

        pkgi_rm(fmt::format("{}/sce_sys/package/head.bin", root).c_str());

        throw DownloadError("pkg integrity failed, try downloading again");
    }

    LOG("pkg integrity check succeeded");
    return 1;
}

int Download::create_stat()
{
    LOG("creating stat.bin");
    update_status("Creating stat.bin");

    const auto path = fmt::format("{}/sce_sys/package/stat.bin", root);

    uint8_t stat[768] = {0};
    if (!pkgi_save(path.c_str(), stat, sizeof(stat)))
        throw formatEx<DownloadError>("cannot save zeros to {}", path);

    LOG("stat.bin created");
    return 1;
}

int Download::create_rif(const uint8_t* rif)
{
    LOG("creating work.bin");
    update_status("Creating work.bin");

    const auto path = fmt::format("{}/sce_sys/package/work.bin", root);

    if (!pkgi_save(path.c_str(), rif, PKGI_RIF_SIZE))
        throw formatEx<DownloadError>("cannot save rif to {}", path);

    LOG("work.bin created");
    return 1;
}

int Download::create_psm_rif(const uint8_t* rif)
{
    LOG("creating RO/License");
    update_status("Creating RO/License");
    const auto folder = fmt::format("{}/RO/License", root);
    pkgi_mkdirs(folder.c_str());

    LOG("creating work.bin");
    update_status("Creating work.bin");

    const auto path = fmt::format("{}/RO/License/FAKE.rif", root);

    if (!pkgi_save(path.c_str(), rif, PKGI_PSM_RIF_SIZE))
        throw formatEx<DownloadError>("cannot save rif to {}", path);

    LOG("FAKE.rif created");
    return 1;
}

int Download::adjust_psm_files(void)
{
    update_status("Creating additional psm files");
    LOG("Removing sce_sys");
    const auto sce_sys = fmt::format("{}/sce_sys", root);
    pkgi_delete_dir(sce_sys);

    const auto documents = fmt::format("{}/RW/Documents", root);
    pkgi_mkdirs(documents.c_str());

    LOG("creating RW/Temp");
    const auto temp = fmt::format("{}/RW/Temp", root);
    pkgi_mkdirs(temp.c_str());

    LOG("creating RW/System");
    const auto system = fmt::format("{}/RW/System", root);
    pkgi_mkdirs(system.c_str());

    LOG("creating RW/System/content_id");
    char content_data[0x30];
    strncpy(content_data, download_content, sizeof(content_data));
    const auto content_id = fmt::format("{}/RW/System/content_id", root);
    if (!pkgi_save(content_id.c_str(), content_data, 0x30))
        throw formatEx<DownloadError>("cannot save content_id to {}", content_id);

    LOG("creating RW/System/pm.dat");
    const auto pm_dat = fmt::format("{}/RW/System/pm.dat", root);
    uint8_t *pm_data = (uint8_t *)calloc(1 << 16, 1);
    if (!pkgi_save(pm_dat.c_str(), pm_data, 1 << 16))
    {
        free(pm_data);
        throw formatEx<DownloadError>("cannot save pm.dat to {}", pm_dat);
    }
    free(pm_data);

    return 1;
}

int Download::pkgi_download(
        const char* partition,
        const char* content,
        const char* url,
        const uint8_t* rif,
        const uint8_t* digest)
{
    root = fmt::format("{}pkgi/{}", partition, content);
    LOGF("temp installation folder: {}", root);

    try
    {
        update_status("Downloading");
        sha256_init(&sha);

        resuming = false;
        item_file = NULL;
        item_index = 0;
        last_state_save = 0;
        download_size = 0;
        download_offset = 0;
        download_content = content;
        download_url = url;

        info_start = pkgi_time_msec();
        info_update = info_start + 1000;

        deserialize_state();

        if (!resuming)
            pkgi_delete_dir(root);

        if (!resuming)
            if (!download_head(rif))
                return 0;
        if (!download_files())
            return 0;
        if (!download_tail())
            return 0;
        if (content_type != CONTENT_TYPE_PSX_GAME &&
            content_type != CONTENT_TYPE_PSP_GAME &&
            content_type != CONTENT_TYPE_PSP_MINI_GAME &&
            content_type != CONTENT_TYPE_PSM_GAME &&
            content_type != CONTENT_TYPE_PSM_GAME_ALT )
        {
            if (!create_stat())
                return 0;
        }
        if (!check_integrity(digest))
            return 0;
        if (content_type == CONTENT_TYPE_PSM_GAME ||
            content_type == CONTENT_TYPE_PSM_GAME_ALT)
        {
            if (!adjust_psm_files())
                return 0;
        }
        if (rif)
        {
            if (content_type == CONTENT_TYPE_PSM_GAME ||
                content_type == CONTENT_TYPE_PSM_GAME_ALT)
            {
                if (!create_psm_rif(rif))
                    return 0;
            }
            else
            {
                if (!create_rif(rif))
                    return 0;
            }
        }
        return 1;
    }
    catch (const ResumeError& e)
    {
        LOGF("deleting resume file");
        try
        {
            pkgi_rm(fmt::format("{}.resume", root).c_str());
            pkgi_delete_dir(root);
        }
        catch (const std::exception& e)
        {
            LOGF("failed to delete resume file");
        }
        throw;
    }
}

void Download::serialize_state() const
{
    std::ofstream ss(
            fmt::format("{}.resume", root), std::ios::out | std::ios::trunc);
    cereal::BinaryOutputArchive oarchive(ss);

    oarchive(static_cast<uint8_t>(1));

    oarchive(save_as_iso);
    oarchive(download_offset, download_size);

    oarchive(iv);
    oarchive.saveBinary(&aes, sizeof(aes));
    oarchive.saveBinary(&sha, sizeof(sha));

    oarchive(item_index);

    oarchive(index_count);
    oarchive(total_size);
    oarchive(enc_offset);
    oarchive(enc_size);

    oarchive(content_type);

    oarchive(encrypted_base);
    oarchive(encrypted_offset);
    oarchive(decrypted_size);
}

void Download::deserialize_state()
{
    const auto state_file = fmt::format("{}.resume", root);

    if (!pkgi_file_exists(state_file.c_str()))
        return;

    try
    {
        LOG("download resume file found");

        const auto head_path = fmt::format("{}/sce_sys/package/head.bin", root);
        head = pkgi_load(head_path);

        std::ifstream ss(state_file);
        cereal::BinaryInputArchive iarchive(ss);

        uint8_t version;
        iarchive(version);
        if (version != 1)
            throw std::runtime_error("invalid resume data version");

        iarchive(save_as_iso);
        iarchive(download_offset, download_size);

        iarchive(iv);
        iarchive.loadBinary(&aes, sizeof(aes));
        iarchive.loadBinary(&sha, sizeof(sha));

        iarchive(item_index);

        iarchive(index_count);
        iarchive(total_size);
        iarchive(enc_offset);
        iarchive(enc_size);

        iarchive(content_type);

        iarchive(encrypted_base);
        iarchive(encrypted_offset);
        iarchive(decrypted_size);

        resuming = true;

        LOGF("resuming download from {}/{}", download_offset, download_size);
    }
    catch (const std::exception& e)
    {
        throw formatEx<ResumeError>(
                "can't resume download:\n{}\nResume data deleted.", e.what());
    }
}
