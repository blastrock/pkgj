#include "bgdl.hpp"

#include "file.hpp"
#include "log.hpp"

#include <taihen.h>
#include <vitasdk.h>

#include <fmt/format.h>

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <sys/syslimits.h>

// RE by @dots_tb with help from:
//       @CelesteBlue123 @SilicaDevs @possvkey and the NPS Team

namespace
{
// offsets for DC0 structure
// 0x70 URL
// 0x870 icon path
// 0x970 title
// 0xCAA license path (the bgldl will copy and paste it to bgdlid folder)

typedef struct ipmi_download_param
{
    int type[2];
    char unk_0x08[0x68];
    char url[0x800]; // size is 2048 ?
    char icon_path[0x100];
    char title[0x33A]; // size is 0x33A ?
    char license_path[0x100];
    char unk_0xDAA[0x16];
} ipmi_download_param;

typedef struct sce_ipmi_download_param
{
    union {
        struct
        {
            uint32_t* ptr_to_dc0_ptr;
            uint32_t* ptr_to_2e0_ptr;
            uint32_t unk_1; // 2
            uint32_t unk_2; //-1
            uint32_t unk_3; // 0
            ipmi_download_param* addr_DC0;
            uint32_t sizeDC0;
        } init;

        struct
        {
            int32_t* result;
            uint32_t unk_2; // 0
            uint32_t unk_3; // 0
            uint32_t unk_4; // 1
            uint32_t unk_5; // 0
            uint32_t unk_6;
            uint32_t unk_7; // 0x00000A0A
        } state;
    };
    void* addr_2E0;
    uint32_t size2E0;
    uint32_t unk_4;
    uint32_t* pBgdlId; // points to -1
    uint32_t unk_5; // 4
    int32_t* result; // points to 1
    uint32_t unk_4_2;
    uint32_t shell_func_8;
} sce_ipmi_download_param;

typedef struct ipmi_download_param_state
{
    int32_t* result;
    uint32_t unk_2; // 0
    uint32_t unk_3; // 0
    uint32_t unk_4; // 1
    uint32_t unk_5; // 0
    uint32_t unk_6;
    uint32_t unk_7; // 0x00000A0A
} ipmi_download_param_state;

typedef struct shellsvc_init_struct
{
    uint32_t unk_0;
    char name[0x10];
    void* unk_ptr;
    uint32_t unk_1; // 1
    uint32_t size1; // 0x1E00
    uint32_t size2; // 0x1E00
    uint32_t unk_2; // 1
    uint32_t unk_3; // 0x0F00
    uint32_t unk_4; // 0x0F00
    uint32_t unk_5; // 1
    char padding[0x84];
    uint32_t unk_7; // 2
    uint32_t unk_8; // -1
    void* unk_ptr_2;
    char padding2[0x88]; // 0x14c
} shellsvc_init_struct;

typedef struct scedownload_class_header
{
    uint32_t unk0;
    uint32_t unk1;
    uint32_t unk2;
    uint32_t** func_table;
    uint32_t unk3;
    uint32_t* bufC4;
    uint32_t* buf10000;
} scedownload_class_header;

typedef int (*SceDownloadInit)(
        uint32_t** ipmi_sce_download_ptr,
        void* ipmi_sce_download_ptr_deref,
        int unk_1,
        shellsvc_init_struct* bufc8,
        int unk_2);
typedef int (*SceDownloadChangeState)(
        uint32_t** ipmi_sce_download_ptr,
        int cmd,
        void* ptr_to_dc0_ptr,
        int unk_1,
        sce_ipmi_download_param r5);

typedef struct scedownload_class
{
    shellsvc_init_struct init_header;
    scedownload_class_header* class_header;
    SceDownloadInit init;
    SceDownloadChangeState change_state;
} scedownload_class;

// sceIpmiCreateDownloadTask?
int (*SceIpmi_4E255C31)(const char* name, int unk);

// sceIpmiInitDownloadTask?
int (*SceIpmi_B282B430)(
        uint32_t*** func_table,
        const char* name,
        scedownload_class_header* class_header,
        uint32_t* buf10000);

void init_download_class(scedownload_class* sceDownloadObj)
{
    memset(sceDownloadObj, 0, sizeof(scedownload_class));

    strncpy(sceDownloadObj->init_header.name, "SceDownload", 0x10);
    sceDownloadObj->init_header.unk_1 = 1;
    sceDownloadObj->init_header.size1 = 0x1E00;
    sceDownloadObj->init_header.size2 = 0x1E00;
    sceDownloadObj->init_header.unk_2 = 1;
    sceDownloadObj->init_header.unk_3 = 0x0F00;
    sceDownloadObj->init_header.unk_4 = 0x0F00;
    sceDownloadObj->init_header.unk_5 = 1;
    sceDownloadObj->init_header.unk_7 = 2;
    sceDownloadObj->init_header.unk_8 = -1;

    int res = SceIpmi_4E255C31(
            (char*)&(sceDownloadObj->init_header.name), 0x1E00);

    if (res != 0xc4)
        throw formatEx<std::runtime_error>(
                "SceIpmi_4E255C31 failed: {:#08x}", static_cast<uint32_t>(res));

    sceDownloadObj->class_header = (scedownload_class_header*)new char[0x18]();

    sceDownloadObj->class_header->bufC4 = (uint32_t*)new char[res]();

    sceDownloadObj->class_header->buf10000 = (uint32_t*)new char[0x1000]();

    res = SceIpmi_B282B430(
            &(sceDownloadObj->class_header->func_table),
            (char*)&(sceDownloadObj->init_header.name),
            sceDownloadObj->class_header,
            sceDownloadObj->class_header->buf10000);
    if (res != 0)
        throw formatEx<std::runtime_error>(
                "SceIpmi_B282B430 init failed: {:#08x}",
                static_cast<uint32_t>(res));

    sceDownloadObj->init =
            (SceDownloadInit)(*(sceDownloadObj->class_header->func_table))[1];
    sceDownloadObj->change_state = (SceDownloadChangeState)(
            *(sceDownloadObj->class_header->func_table))[5];

    res = sceDownloadObj->init(
            sceDownloadObj->class_header->func_table,
            *(sceDownloadObj->class_header->func_table),
            0x14,
            &(sceDownloadObj->init_header),
            2);
    if (res != 0)
        throw formatEx<std::runtime_error>(
                "SceDownload init failed: {:#08x}", static_cast<uint32_t>(res));
}

void scedownload_start_with_rif(
        scedownload_class* sceDownloadObj,
        const char* partition,
        const char* title,
        const char* url,
        const char* rif,
        int type)
{
    int32_t result = 0;
    int32_t bgdlid = 1;

    sce_ipmi_download_param params;
    memset(&params, 0, sizeof(params));

    params.init.ptr_to_dc0_ptr = (uint32_t*)&params.init.addr_DC0;
    params.init.ptr_to_2e0_ptr = (uint32_t*)&params.addr_2E0;
    params.init.unk_1 = 2;
    params.init.unk_2 = -1;
    params.init.unk_3 = 0;

    params.init.sizeDC0 = 0xDC0;
    std::vector<uint8_t> buf_dc0(params.init.sizeDC0);
    params.init.addr_DC0 = (ipmi_download_param*)buf_dc0.data();

    params.size2E0 = 0x2E0;
    std::vector<uint8_t> buf_2e0(params.size2E0);
    params.addr_2E0 = buf_2e0.data();

    params.pBgdlId = (uint32_t*)&bgdlid; // points to -1
    params.unk_5 = 4;
    params.result = &result;
    params.shell_func_8 = (*(sceDownloadObj->class_header->func_table))[8];

    auto icon_path = fmt::format("{}bgdl/icon0.png", partition);
    strcpy((char*)params.init.addr_DC0->url, url);
    strcpy((char*)params.init.addr_DC0->license_path, rif);
    strcpy((char*)params.init.addr_DC0->title, title);
    strcpy((char*)params.init.addr_DC0->icon_path, icon_path.c_str());

    params.init.addr_DC0->type[0] = params.init.addr_DC0->type[1] = type;

    int res = sceDownloadObj->change_state(
            sceDownloadObj->class_header->func_table,
            0x12340012,
            params.init.ptr_to_dc0_ptr,
            1,
            params);

    if (res < 0)
        throw formatEx<std::runtime_error>(
                "SceDownload change_state failed: {:#08x}",
                static_cast<uint32_t>(res));
    if (result < 0)
        throw formatEx<std::runtime_error>(
                "SceDownload change_state result failed: {:#08x}",
                static_cast<uint32_t>(result));
    if (bgdlid < 0)
        throw formatEx<std::runtime_error>(
                "SceDownload change_state bgdlid failed: {:#08x}",
                static_cast<uint32_t>(res));
    if (reinterpret_cast<intptr_t>(params.init.addr_DC0) < 0)
        throw formatEx<std::runtime_error>(
                "SceDownload change_state DC0 failed: {:#08x}",
                reinterpret_cast<uintptr_t>(params.init.addr_DC0));

    result = 0;

    memset(&params, 0, sizeof(params));

    params.state.result = &result;
    params.state.unk_4 = 1;
    params.state.unk_7 = 0x00000A0A;

    res = sceDownloadObj->change_state(
            sceDownloadObj->class_header->func_table, 0x12340007, 0, 0, params);
    if (res < 0)
        throw formatEx<std::runtime_error>(
                "SceDownload second change_state failed: {:#08x}",
                static_cast<uint32_t>(res));
    if (result < 0)
        throw formatEx<std::runtime_error>(
                "SceDownload second change_state result failed: {:#08x}",
                static_cast<uint32_t>(result));
    buf_dc0.clear();
    buf_2e0.clear();
}

std::unique_ptr<scedownload_class> new_scedownload()
{
    char lib_path[] = "vs0:sys/external/libshellsvc.suprx";

    sceKernelLoadStartModule(lib_path, 0, NULL, 0, NULL, NULL);

    taiGetModuleExportFunc(
            "SceShellSvc",
            0xF4E34EDB,
            0x4E255C31,
            (uintptr_t*)&SceIpmi_4E255C31);
    taiGetModuleExportFunc(
            "SceShellSvc",
            0xF4E34EDB,
            0xB282B430,
            (uintptr_t*)&SceIpmi_B282B430);

    auto example_class = std::make_unique<scedownload_class>();
    init_download_class(example_class.get());
    return example_class;
}
}

void pkgi_start_bgdl(
        const int type,
        const std::string& partition,
        const std::string& title,
        const std::string& url,
        const std::vector<uint8_t>& rif)
{
    if (pkgi_list_dir_contents(fmt::format("{}bgdl/t", partition)).size() >= 32)
        throw std::runtime_error(
                "There are too many pending installation on your device, "
                "install them from LiveArea's notifications or delete them to "
                "be able to download more.");

    static auto example_class = new_scedownload();

    const std::string license_path = fmt::format("{}bgdl/temp.dat", partition);
    pkgi_save(license_path, rif.data(), rif.size());

    scedownload_start_with_rif(
            example_class.get(),
            partition.c_str(),
            title.c_str(),
            url.c_str(),
            license_path.c_str(),
            type);
}
