extern "C" {
#include "pkgi.h"
#include "pkgi_style.h"
}
#include "pkgi_http.hpp"

#include <fmt/format.h>

#include <boost/scope_exit.hpp>

#include <string>

#include <vita2d.h>

#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/ime_dialog.h>
#include <psp2/io/devctl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/libssl.h>
#include <psp2/net/http.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/power.h>
#include <psp2/promoterutil.h>
#include <psp2/shellutil.h>
#include <psp2/sysmodule.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

char partition[]="ux0:";
static vita2d_pgf* g_font;

static SceKernelLwMutexWork g_dialog_lock;
static volatile int g_power_lock;

static int g_ok_button;
static int g_cancel_button;
static uint32_t g_button_frame_count;

static SceUInt64 g_time;

static char g_pkgi_folder[32];

#ifdef PKGI_ENABLE_LOGGING
static int g_log_socket;
#endif

#define ANALOG_CENTER 128
#define ANALOG_THRESHOLD 64
#define ANALOG_SENSITIVITY 16

#define VITA_COLOR(c) RGBA8((c)&0xff, (c >> 8) & 0xff, (c >> 16) & 0xff, 255)

#define PKGI_ERRNO_EEXIST (int)(0x80010000 + SCE_NET_EEXIST)
#define PKGI_ERRNO_ENOENT (int)(0x80010000 + SCE_NET_ENOENT)

#define PKGI_USER_AGENT "libhttp/3.65 (PS Vita)"

#ifdef PKGI_ENABLE_LOGGING
void pkgi_log(const char* msg, ...)
{
    char buffer[512];

    va_list args;
    va_start(args, msg);
    // TODO: why sceClibVsnprintf doesn't work here?
    int len = vsnprintf(buffer, sizeof(buffer) - 1, msg, args);
    va_end(args);
    buffer[len] = '\n';

    sceNetSend(g_log_socket, buffer, len + 1, 0);
    // sceKernelDelayThread(10);
}
#endif

int pkgi_snprintf(char* buffer, uint32_t size, const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    // TODO: why sceClibVsnprintf doesn't work here?
    int len = vsnprintf(buffer, size - 1, msg, args);
    va_end(args);
    buffer[len] = 0;
    return len;
}

void pkgi_vsnprintf(char* buffer, uint32_t size, const char* msg, va_list args)
{
    // TODO: why sceClibVsnprintf doesn't work here?
    int len = vsnprintf(buffer, size - 1, msg, args);
    buffer[len] = 0;
}

char* pkgi_strstr(const char* str, const char* sub)
{
    return strstr(str, sub);
}

int pkgi_stricontains(const char* str, const char* sub)
{
    return strcasestr(str, sub) != NULL;
}

int pkgi_stricmp(const char* a, const char* b)
{
    return strcasecmp(a, b);
}

void pkgi_strncpy(char* dst, uint32_t size, const char* src)
{
    sceClibStrncpy(dst, src, size);
}

char* pkgi_strrchr(const char* str, char ch)
{
    return strrchr(str, ch);
}

void pkgi_memcpy(void* dst, const void* src, uint32_t size)
{
    sceClibMemcpy(dst, src, size);
}

void pkgi_memmove(void* dst, const void* src, uint32_t size)
{
    sceClibMemmove(dst, src, size);
}

int pkgi_memequ(const void* a, const void* b, uint32_t size)
{
    return memcmp(a, b, size) == 0;
}

static void pkgi_start_debug_log(void)
{
#ifdef PKGI_ENABLE_LOGGING
    g_log_socket = sceNetSocket(
            "log_socket",
            SCE_NET_AF_INET,
            SCE_NET_SOCK_DGRAM,
            SCE_NET_IPPROTO_UDP);

    SceNetSockaddrIn addr{};
    addr.sin_family = SCE_NET_AF_INET;
    addr.sin_port = sceNetHtons(30000);
    sceNetInetPton(SCE_NET_AF_INET, "239.255.0.100", &addr.sin_addr);

    sceNetConnect(g_log_socket, (SceNetSockaddr*)&addr, sizeof(addr));
    LOG("debug logging socket initialized");
#endif
}

static void pkgi_stop_debug_log(void)
{
#ifdef PKGI_ENABLE_LOGGING
    sceNetSocketClose(g_log_socket);
#endif
}

// TODO: this is from VitaShell
// no idea why, but this seems to be required for promoter utility functions to
// work
static void pkgi_load_sce_paf()
{
    uint32_t args[] = {
            0x00400000,
            0x0000ea60,
            0x00040000,
            0x00000000,
            0x00000001,
            0x00000000,
    };

    uint32_t result = 0xDEADBEEF;

    uint32_t buffer[4] = {};
    buffer[1] = (uint32_t)&result;

    sceSysmoduleLoadModuleInternalWithArg(
            SCE_SYSMODULE_INTERNAL_PAF, sizeof(args), args, buffer);
}

int pkgi_is_unsafe_mode(void)
{
    return sceIoDevctl("ux0:", 0x3001, NULL, 0, NULL, 0) != (int)0x80010030;
}

int pkgi_ok_button(void)
{
    return g_ok_button;
}

int pkgi_cancel_button(void)
{
    return g_cancel_button;
}

static int pkgi_power_thread(SceSize args, void* argp)
{
    PKGI_UNUSED(args);
    PKGI_UNUSED(argp);
    for (;;)
    {
        int lock;
        __atomic_load(&g_power_lock, &lock, __ATOMIC_SEQ_CST);
        if (lock > 0)
        {
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
        }

        sceKernelDelayThread(10 * 1000 * 1000);
    }
    return 0;
}

void pkgi_dialog_lock(void)
{
    int res = sceKernelLockLwMutex(&g_dialog_lock, 1, NULL);
    if (res < 0)
    {
        LOG("dialog unlock failed error=0x%08x", res);
    }
}

void pkgi_dialog_unlock(void)
{
    int res = sceKernelUnlockLwMutex(&g_dialog_lock, 1);
    if (res < 0)
    {
        LOG("dialog lock failed error=0x%08x", res);
    }
}

static int g_ime_active;

static uint16_t g_ime_title[SCE_IME_DIALOG_MAX_TITLE_LENGTH];
static uint16_t g_ime_text[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
static uint16_t g_ime_input[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

static int convert_to_utf16(
        const char* utf8, uint16_t* utf16, uint32_t available)
{
    int count = 0;
    while (*utf8)
    {
        uint8_t ch = (uint8_t)*utf8++;
        uint32_t code;
        uint32_t extra;

        if (ch < 0x80)
        {
            code = ch;
            extra = 0;
        }
        else if ((ch & 0xe0) == 0xc0)
        {
            code = ch & 31;
            extra = 1;
        }
        else if ((ch & 0xf0) == 0xe0)
        {
            code = ch & 15;
            extra = 2;
        }
        else
        {
            // TODO: this assumes there won't be invalid utf8 codepoints
            code = ch & 7;
            extra = 3;
        }

        for (uint32_t i = 0; i < extra; i++)
        {
            uint8_t next = (uint8_t)*utf8++;
            if (next == 0 || (next & 0xc0) != 0x80)
            {
                return count;
            }
            code = (code << 6) | (next & 0x3f);
        }

        if (code < 0xd800 || code >= 0xe000)
        {
            if (available < 1)
                return count;
            utf16[count++] = (uint16_t)code;
            available--;
        }
        else // surrogate pair
        {
            if (available < 2)
                return count;
            code -= 0x10000;
            utf16[count++] = 0xd800 | (code >> 10);
            utf16[count++] = 0xdc00 | (code & 0x3ff);
            available -= 2;
        }
    }
    return count;
}

static int convert_from_utf16(const uint16_t* utf16, char* utf8, uint32_t size)
{
    int count = 0;
    while (*utf16)
    {
        uint32_t code;
        uint16_t ch = *utf16++;
        if (ch < 0xd800 || ch >= 0xe000)
        {
            code = ch;
        }
        else // surrogate pair
        {
            uint16_t ch2 = *utf16++;
            if (ch < 0xdc00 || ch > 0xe000 || ch2 < 0xd800 || ch2 > 0xdc00)
            {
                return count;
            }
            code = 0x10000 + ((ch & 0x03FF) << 10) + (ch2 & 0x03FF);
        }

        if (code < 0x80)
        {
            if (size < 1)
                return count;
            utf8[count++] = (char)code;
            size--;
        }
        else if (code < 0x800)
        {
            if (size < 2)
                return count;
            utf8[count++] = (char)(0xc0 | (code >> 6));
            utf8[count++] = (char)(0x80 | (code & 0x3f));
            size -= 2;
        }
        else if (code < 0x10000)
        {
            if (size < 3)
                return count;
            utf8[count++] = (char)(0xe0 | (code >> 12));
            utf8[count++] = (char)(0x80 | ((code >> 6) & 0x3f));
            utf8[count++] = (char)(0x80 | (code & 0x3f));
            size -= 3;
        }
        else
        {
            if (size < 4)
                return count;
            utf8[count++] = (char)(0xf0 | (code >> 18));
            utf8[count++] = (char)(0x80 | ((code >> 12) & 0x3f));
            utf8[count++] = (char)(0x80 | ((code >> 6) & 0x3f));
            utf8[count++] = (char)(0x80 | (code & 0x3f));
            size -= 4;
        }
    }
    return count;
}

void pkgi_dialog_input_text(const char* title, const char* text)
{
    SceImeDialogParam param;
    sceImeDialogParamInit(&param);

    int title_len =
            convert_to_utf16(title, g_ime_title, PKGI_COUNTOF(g_ime_title) - 1);
    int text_len =
            convert_to_utf16(text, g_ime_text, PKGI_COUNTOF(g_ime_text) - 1);
    g_ime_title[title_len] = 0;
    g_ime_text[text_len] = 0;

    param.supportedLanguages = 0x0001FFFF;
    param.languagesForced = SCE_TRUE;
    param.type = SCE_IME_TYPE_DEFAULT;
    param.option = 0;
    param.title = g_ime_title;
    param.maxTextLength = 128;
    param.initialText = g_ime_text;
    param.inputTextBuffer = g_ime_input;

    int res = sceImeDialogInit(&param);
    if (res < 0)
    {
        LOG("sceImeDialogInit failed, error 0x%08x", res);
    }
    else
    {
        g_ime_active = 1;
    }
}

int pkgi_dialog_input_update(void)
{
    if (!g_ime_active)
    {
        return 0;
    }

    SceCommonDialogStatus status = sceImeDialogGetStatus();
    if (status == SCE_COMMON_DIALOG_STATUS_FINISHED)
    {
        SceImeDialogResult result{};
        sceImeDialogGetResult(&result);

        g_ime_active = 0;
        sceImeDialogTerm();

        if (result.button == SCE_IME_DIALOG_BUTTON_ENTER)
        {
            return 1;
        }
    }

    return 0;
}

void pkgi_dialog_input_get_text(char* text, uint32_t size)
{
    int count = convert_from_utf16(g_ime_input, text, size - 1);
    text[count] = 0;
}

void pkgi_start(void)
{
    pkgi_load_sce_paf();
    sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
    sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);

    static uint8_t netmem[1024 * 1024];
    SceNetInitParam net = {
            .memory = netmem,
            .size = sizeof(netmem),
            .flags = 0,
    };

    sceNetInit(&net);
    sceNetCtlInit();

    pkgi_start_debug_log();

    LOG("initializing SSL");
    sceSslInit(1024 * 1024);
    LOG("initializing HTTP");
    sceHttpInit(1024 * 1024);
    LOG("network initialized");

    sceHttpsDisableOption(SCE_HTTPS_FLAG_SERVER_VERIFY);

    sceKernelCreateLwMutex(&g_dialog_lock, "dialog_lock", 2, 0, NULL);

    scePowerSetArmClockFrequency(444);

    sceShellUtilInitEvents(0);
    sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_USB_CONNECTION);

    SceAppUtilInitParam init{};
    SceAppUtilBootParam boot{};
    sceAppUtilInit(&init, &boot);

    SceCommonDialogConfigParam config;
    sceCommonDialogConfigParamInit(&config);
    sceAppUtilSystemParamGetInt(
            SCE_SYSTEM_PARAM_ID_LANG, (int*)&config.language);
    sceAppUtilSystemParamGetInt(
            SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, (int*)&config.enterButtonAssign);
    sceCommonDialogSetConfigParam(&config);

    if (config.enterButtonAssign == SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE)
    {
        g_ok_button = PKGI_BUTTON_O;
        g_cancel_button = PKGI_BUTTON_X;
    }
    else
    {
        g_ok_button = PKGI_BUTTON_X;
        g_cancel_button = PKGI_BUTTON_O;
    }

    SceIoStat stat;
    if (sceIoGetstat("ur0:pkgi", &stat) >= 0 && SCE_S_ISDIR(stat.st_mode))
    {
        pkgi_strncpy(g_pkgi_folder, sizeof(g_pkgi_folder), "ur0:pkgi");
    }
    else
    {
        pkgi_strncpy(g_pkgi_folder, sizeof(g_pkgi_folder), "ux0:pkgi");
    }

    if (scePromoterUtilityInit() < 0)
    {
        LOG("cannot initialize promoter utility");
    }

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    g_power_lock = 0;
    SceUID power_thread = sceKernelCreateThread(
            "power_thread",
            &pkgi_power_thread,
            0x10000100,
            0x40000,
            0,
            0,
            NULL);
    if (power_thread >= 0)
    {
        sceKernelStartThread(power_thread, 0, NULL);
    }

    vita2d_init_advanced(4 * 1024 * 1024);
    g_font = vita2d_load_default_pgf();

    g_time = sceKernelGetProcessTimeWide();
}

int pkgi_update(pkgi_input* input)
{
    SceCtrlData pad{};
    sceCtrlPeekBufferPositive(0, &pad, 1);

    uint32_t previous = input->down;

    input->down = pad.buttons;
    if (pad.lx < ANALOG_CENTER - ANALOG_THRESHOLD)
        input->down |= PKGI_BUTTON_LEFT;
    if (pad.lx > ANALOG_CENTER + ANALOG_THRESHOLD)
        input->down |= PKGI_BUTTON_RIGHT;
    if (pad.ly < ANALOG_CENTER - ANALOG_THRESHOLD)
        input->down |= PKGI_BUTTON_UP;
    if (pad.ly > ANALOG_CENTER + ANALOG_THRESHOLD)
        input->down |= PKGI_BUTTON_DOWN;

    input->pressed = input->down & ~previous;
    input->active = input->pressed;

    if (input->down == previous)
    {
        if (g_button_frame_count >= 10)
        {
            input->active = input->down;
        }
        g_button_frame_count++;
    }
    else
    {
        g_button_frame_count = 0;
    }

    vita2d_start_drawing();

    uint64_t time = sceKernelGetProcessTimeWide();
    input->delta = time - g_time;
    g_time = time;

    return 1;
}

void pkgi_swap(void)
{
    // LOG("vita2d pool free space = %u KB", vita2d_pool_free_space() / 1024);

    vita2d_end_drawing();
    vita2d_common_dialog_update();
    vita2d_swap_buffers();
    sceDisplayWaitVblankStart();
}

void pkgi_end(void)
{
    pkgi_stop_debug_log();

    vita2d_fini();
    vita2d_free_pgf(g_font);

    scePromoterUtilityExit();

    sceAppUtilShutdown();

    sceKernelDeleteLwMutex(&g_dialog_lock);

    sceHttpTerm();
    // sceSslTerm();
    sceNetCtlTerm();
    sceNetTerm();

    sceSysmoduleUnloadModule(SCE_SYSMODULE_SSL);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTP);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);

    sceKernelExitProcess(0);
}

int pkgi_battery_present()
{
    return sceKernelGetModel() == SCE_KERNEL_MODEL_VITA;
}

int pkgi_bettery_get_level()
{
    return scePowerGetBatteryLifePercent();
}

int pkgi_battery_is_low()
{
    return scePowerIsLowBattery() && !scePowerIsBatteryCharging();
}

int pkgi_battery_is_charging()
{
    return scePowerIsBatteryCharging();
}

const char* pkgi_get_partition(void)
{

    if ((partition != NULL) && (partition[0] == '\0')) {
        return "ux0:";
    }else{
        return partition;
    }

}
void pkgi_set_partition_ux0()
{
    strcpy(partition, "ux0:");
}
void pkgi_set_partition_ur0()
{
    strcpy(partition, "ur0:");
}
void pkgi_set_partition_uma0()
{
    strcpy(partition, "uma0:");
}
uint64_t pkgi_get_free_space(const char* RequestedPartition)
{
    if (pkgi_is_unsafe_mode())
    {
        SceIoDevInfo info{};
        sceIoDevctl(RequestedPartition, 0x3001, NULL, 0, &info, sizeof(info));
        return info.free_size;
    }
    else
    {
        uint64_t free, max;
        char *dev;
		strcpy(dev,RequestedPartition);
        sceAppMgrGetDevInfo(dev, &max, &free);
        return free;
    }
}

const char* pkgi_get_config_folder(void)
{
    return g_pkgi_folder;
}

const char* pkgi_get_temp_folder(void)
{

	//cant find a proper way to return pkgi_get_partition() + "pkgi" as it goes null when accesed by Download::pkgi_download on pkgi_download.cpp
	if(strcmp(pkgi_get_partition(),"ux0:") == 0){
		return "ux0:pkgi";
	} else if(strcmp(pkgi_get_partition(),"ur0:") == 0){
		return "ur0:pkgi";
	} else if(strcmp(pkgi_get_partition(),"uma0:") == 0){
		return "uma0:pkgi";
	} else{
		return "ux0:pkgi";
	}

}

const char* pkgi_get_app_folder(void)
{
    return "ux0:app";
}

int pkgi_is_incomplete(const char* contentid)
{
    char path[256];
    pkgi_snprintf(
            path,
            sizeof(path),
            "%s/%s.resume",
            pkgi_get_temp_folder(),
            contentid);

    SceIoStat stat;
    int res = sceIoGetstat(path, &stat);
    return res == 0;
}

int pkgi_is_installed(const char* titleid)
{
    int ret = -1;
    LOG("calling scePromoterUtilityCheckExist on %s", titleid);
    int res = scePromoterUtilityCheckExist(titleid, &ret);
    LOG("res=%d ret=%d", res, ret);
    return res == 0;
}

int pkgi_dlc_is_installed(const char* content)
{
    char path[128];
    snprintf(
            path,
            sizeof(path),
            "ux0:addcont/%.9s/%.16s",
            content + 7,
            content + 20);

    SceIoStat stat;
    return sceIoGetstat(path, &stat) >= 0;
}

int pkgi_psp_is_installed(const char* content)
{
    char path[128];
    //snprintf(path, sizeof(path), "ux0:pspemu/ISO/%.9s.iso", content + 7);
	snprintf(path, sizeof(path), "%spspemu/ISO/%.9s.iso", pkgi_get_partition(),content + 7);
    SceIoStat stat;
    return sceIoGetstat(path, &stat) >= 0;
}

int pkgi_psx_is_installed(const char* content)
{
    char path[128];
    //snprintf(path, sizeof(path), "ux0:pspemu/PSP/GAME/%.9s", content + 7);
	snprintf(path, sizeof(path), "%spspemu/PSP/GAME/%.9s", pkgi_get_partition(),content + 7);
    SceIoStat stat;
    return sceIoGetstat(path, &stat) >= 0;
}

void pkgi_install(const char* contentid)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", pkgi_get_temp_folder(), contentid);

    LOG("calling scePromoterUtilityPromotePkgWithRif on %s", path);
    int res = scePromoterUtilityPromotePkgWithRif(path, 1);
    if (res < 0)
        throw std::runtime_error(fmt::format(
                "scePromoterUtilityPromotePkgWithRif failed: {:#08x}",
                static_cast<uint32_t>(res)));
}

static int pkgi_delete_dir(const std::string& path)
{
    SceUID dfd = sceIoDopen(path.c_str());
    if (dfd == PKGI_ERRNO_ENOENT)
        return 1;

    if (dfd < 0)
    {
        LOG("failed to Dopen %s: %x", path.c_str(), dfd);
        return 0;
    }

    int res = 0;
    SceIoDirent dir;
    memset(&dir, 0, sizeof(SceIoDirent));
    while ((res = sceIoDread(dfd, &dir)) > 0)
    {
        std::string new_path =
                path + (path[path.size() - 1] == '/' ? "" : "/") + dir.d_name;

        if (SCE_S_ISDIR(dir.d_stat.st_mode))
        {
            int ret = pkgi_delete_dir(new_path);
            if (!ret)
            {
                sceIoDclose(dfd);
                return 0;
            }
        }
        else
        {
            int ret = sceIoRemove(new_path.c_str());
            if (ret < 0)
            {
                LOG("failed to remove %s: %x", new_path.c_str(), ret);
                sceIoDclose(dfd);
                return 0;
            }
        }
    }

    sceIoDclose(dfd);

    res = sceIoRmdir(path.c_str());
    if (res < 0)
    {
        LOG("failed to rmdir %s: %x", path.c_str(), res);
        return 0;
    }

    return 1;
}

void pkgi_install_update(const char* contentid)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", pkgi_get_temp_folder(), contentid);

    char dest[128];
    snprintf(dest, sizeof(dest), "ux0:patch/%.9s", contentid + 7);

    LOG("deleting previous patch");
    if (!pkgi_delete_dir(dest))
        throw std::runtime_error(
                fmt::format("can't delete previous patch to install this one"));

    LOG("installing update at %s", path);
    int res = sceIoRename(path, dest);
    if (res < 0)
        throw std::runtime_error(fmt::format(
                "failed to rename: {:#08x}", static_cast<uint32_t>(res)));
}

void pkgi_install_pspgame(const char* contentid)
{
    char path[128];
    snprintf(
            path,
            sizeof(path),
            "%s/%s/EBOOT.PBP",
            pkgi_get_temp_folder(),
            contentid);

    char dest[128];
    snprintf(dest, sizeof(dest), "%spspemu/ISO",pkgi_get_partition());
    pkgi_mkdirs(dest);

    snprintf(dest, sizeof(dest), "%spspemu/ISO/%.9s.iso",pkgi_get_partition(), contentid + 7);

    LOG("installing psp game at %s to %s", path, dest);
    int res = sceIoRename(path, dest);
    if (res < 0)
        throw std::runtime_error(fmt::format(
                "failed to rename: {:#08x}", static_cast<uint32_t>(res)));

    snprintf(path, sizeof(path), "%s/%s", pkgi_get_temp_folder(), contentid);
    pkgi_delete_dir(path);
}

void pkgi_install_psxgame(const char* contentid)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", pkgi_get_temp_folder(), contentid);

    char dest[128];
    snprintf(dest, sizeof(dest), "%spspemu/PSP/GAME",pkgi_get_temp_folder());
    pkgi_mkdirs(dest);

    snprintf(dest, sizeof(dest), "%spspemu/PSP/GAME/%.9s",pkgi_get_partition(), contentid + 7);

    LOG("installing psx game at %s to %s", path,dest);
    int res = sceIoRename(path, dest);
    if (res < 0)
        throw std::runtime_error(fmt::format(
                "failed to rename: {:#08x}", static_cast<uint32_t>(res)));
}

uint32_t pkgi_time_msec()
{
    return sceKernelGetProcessTimeLow() / 1000;
}

static int pkgi_vita_thread(SceSize args, void* argp)
{
    PKGI_UNUSED(args);
    pkgi_thread_entry* start = *((pkgi_thread_entry**)argp);
    start();
    return sceKernelExitDeleteThread(0);
}

void pkgi_start_thread(const char* name, pkgi_thread_entry* start)
{
    SceUID id = sceKernelCreateThread(
            name, &pkgi_vita_thread, 0x40, 1024 * 1024, 0, 0, NULL);
    if (id < 0)
    {
        LOG("failed to start %s thread", name);
    }
    else
    {
        sceKernelStartThread(id, sizeof(start), &start);
    }
}

void pkgi_sleep(uint32_t msec)
{
    sceKernelDelayThread(msec * 1000);
}

int pkgi_load(const char* name, void* data, uint32_t max)
{
    SceUID fd = sceIoOpen(name, SCE_O_RDONLY, 0777);
    if (fd < 0)
    {
        return -1;
    }

    char* data8 = static_cast<char*>(data);

    int total = 0;
    while (max != 0)
    {
        int read = sceIoRead(fd, data8 + total, max);
        if (read < 0)
        {
            total = -1;
            break;
        }
        else if (read == 0)
        {
            break;
        }
        total += read;
        max -= read;
    }

    sceIoClose(fd);
    return total;
}

int pkgi_save(const char* name, const void* data, uint32_t size)
{
    SceUID fd = sceIoOpen(name, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0)
    {
        return 0;
    }

    int ret = 1;
    const char* data8 = static_cast<const char*>(data);
    while (size != 0)
    {
        int written = sceIoWrite(fd, data8, size);
        if (written <= 0)
        {
            ret = 0;
            break;
        }
        data8 += written;
        size -= written;
    }

    sceIoClose(fd);
    return ret;
}

void pkgi_lock_process(void)
{
    if (__atomic_fetch_add(&g_power_lock, 1, __ATOMIC_SEQ_CST) == 0)
    {
        LOG("locking shell functionality");
        if (sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN) < 0)
        {
            LOG("sceShellUtilLock failed");
        }
    }
}

void pkgi_unlock_process(void)
{
    if (__atomic_sub_fetch(&g_power_lock, 1, __ATOMIC_SEQ_CST) == 0)
    {
        LOG("unlocking shell functionality");
        if (sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN) < 0)
        {
            LOG("sceShellUtilUnlock failed");
        }
    }
}

pkgi_texture pkgi_load_png_raw(const void* data, uint32_t size)
{
    (void)size;
    vita2d_texture* tex = vita2d_load_PNG_buffer((const char*)data);
    if (!tex)
    {
        LOG("failed to load texture");
    }
    return tex;
}

void pkgi_draw_texture(pkgi_texture texture, int x, int y)
{
    vita2d_texture* tex = static_cast<vita2d_texture*>(texture);
    vita2d_draw_texture(tex, (float)x, (float)y);
}

void pkgi_clip_set(int x, int y, int w, int h)
{
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x, y, x + w - 1, y + h - 1);
}

void pkgi_clip_remove(void)
{
    vita2d_disable_clipping();
}

void pkgi_draw_rect(int x, int y, int w, int h, uint32_t color)
{
    vita2d_draw_rectangle(
            (float)x, (float)y, (float)w, (float)h, VITA_COLOR(color));
}

void pkgi_draw_text(int x, int y, uint32_t color, const char* text)
{
    vita2d_pgf_draw_text(g_font, x, y + 20, VITA_COLOR(color), 1.f, text);
}

int pkgi_text_width(const char* text)
{
    return vita2d_pgf_text_width(g_font, 1.f, text);
}

int pkgi_text_height(const char* text)
{
    PKGI_UNUSED(text);
    // return vita2d_pgf_text_height(g_font, 1.f, text);
    return 23;
}

#define USE_LOCAL 0

struct pkgi_http
{
    int used;
    int local;

    SceUID fd;
    uint64_t size;
    uint64_t offset;

    int tmpl;
    int conn;
    int req;
};

static pkgi_http g_http[4];

pkgi_http* pkgi_http_get(const char* url, const char* content, uint64_t offset)
{
    LOG("http get");

    pkgi_http* http = NULL;
    for (size_t i = 0; i < 4; i++)
    {
        if (g_http[i].used == 0)
        {
            http = g_http + i;
            break;
        }
    }

    if (!http)
        throw HttpError("internal error: too many simultaneous http requests");

    pkgi_http* result = NULL;

    char path[256];

    if (content)
    {
        strcpy(path, pkgi_get_temp_folder());
        const char* lastslash = strrchr(url, '/');
        if (lastslash)
            strcat(path, lastslash);
        else
        {
            strcat(path, "/");
            strcat(path, url);
        }

        http->fd = sceIoOpen(path, SCE_O_RDONLY, 0777);
        if (http->fd < 0)
        {
            LOG("%s not found, trying shorter path", path);
            pkgi_snprintf(
                    path,
                    sizeof(path),
                    "%s/%s.pkg",
                    pkgi_get_temp_folder(),
                    content);
        }

        http->fd = sceIoOpen(path, SCE_O_RDONLY, 0777);
    }
    else
    {
        http->fd = -1;
    }

    if (http->fd >= 0)
    {
        LOG("%s found, using it", path);

        SceIoStat stat;
        if (sceIoGetstatByFd(http->fd, &stat) < 0)
        {
            LOG("cannot get size of file %s", path);
            sceIoClose(http->fd);
            return NULL;
        }

        http->used = 1;
        http->local = 1;
        http->offset = 0;
        http->size = stat.st_size;

        result = http;
    }
    else
    {
        if (content)
        {
            LOG("%s not found, downloading url", path);
        }

        int tmpl = -1;
        int conn = -1;
        int req = -1;

        LOG("starting http GET request for %s", url);

        if ((tmpl = sceHttpCreateTemplate(
                     PKGI_USER_AGENT, SCE_HTTP_VERSION_1_1, SCE_TRUE)) < 0)
            throw HttpError(fmt::format(
                    "sceHttpCreateTemplate failed: {:#08x}",
                    static_cast<uint32_t>(tmpl)));
        BOOST_SCOPE_EXIT_ALL(&)
        {
            if (tmpl < 0)
                sceHttpDeleteTemplate(tmpl);
        };
        // sceHttpSetRecvTimeOut(tmpl, 10 * 1000 * 1000);

        if ((conn = sceHttpCreateConnectionWithURL(tmpl, url, SCE_FALSE)) < 0)
            throw HttpError(fmt::format(
                    "sceHttpCreateConnectionWithURL failed: {:#08x}",
                    static_cast<uint32_t>(conn)));
        BOOST_SCOPE_EXIT_ALL(&)
        {
            if (conn < 0)
                sceHttpDeleteConnection(conn);
        };

        if ((req = sceHttpCreateRequestWithURL(
                     conn, SCE_HTTP_METHOD_GET, url, 0)) < 0)
            throw HttpError(fmt::format(
                    "sceHttpCreateRequestWithURL failed: {:#08x}",
                    static_cast<uint32_t>(req)));
        BOOST_SCOPE_EXIT_ALL(&)
        {
            if (req < 0)
                sceHttpDeleteRequest(req);
        };

        int err;

        if (offset != 0)
        {
            char range[64];
            pkgi_snprintf(range, sizeof(range), "bytes=%llu-", offset);
            if ((err = sceHttpAddRequestHeader(
                         req, "Range", range, SCE_HTTP_HEADER_ADD)) < 0)
                throw HttpError(fmt::format(
                        "sceHttpAddRequestHeader failed: {:#08x}",
                        static_cast<uint32_t>(err)));
        }

        if ((err = sceHttpSendRequest(req, NULL, 0)) < 0)
            throw HttpError(fmt::format(
                    "sceHttpSendRequest failed: {:#08x}",
                    static_cast<uint32_t>(err)));

        http->used = 1;
        http->local = 0;
        http->tmpl = tmpl;
        http->conn = conn;
        http->req = req;
        tmpl = conn = req = -1;

        result = http;
    }

    return result;
}

void pkgi_http_response_length(pkgi_http* http, int64_t* length)
{
    if (http->local)
    {
        *length = (int64_t)http->size;
        return;
    }

    int res;
    int status;
    if ((res = sceHttpGetStatusCode(http->req, &status)) < 0)
        throw HttpError(fmt::format(
                "sceHttpGetStatusCode failed: {:#08x}",
                static_cast<uint32_t>(res)));

    LOG("http status code = %d", status);

    if (status != 200 && status != 206)
        throw HttpError(fmt::format("bad http status: {}", status));

    char* headers;
    unsigned int size;
    if (sceHttpGetAllResponseHeaders(http->req, &headers, &size) >= 0)
    {
        LOG("response headers:");
        LOG("%.*s", (int)size, headers);
    }

    uint64_t content_length;
    res = sceHttpGetResponseContentLength(http->req, &content_length);
    if (res < 0)
        throw HttpError(fmt::format(
                "sceHttpGetResponseContentLength failed: {:#08x}",
                static_cast<uint32_t>(res)));
    if (res == (int)SCE_HTTP_ERROR_NO_CONTENT_LENGTH ||
        res == (int)SCE_HTTP_ERROR_CHUNK_ENC)
    {
        LOG("http response has no content length (or chunked "
            "encoding)");
        *length = 0;
    }
    else
    {
        LOG("http response length = %llu", content_length);
        *length = (int64_t)content_length;
    }
}

int pkgi_http_read(pkgi_http* http, void* buffer, uint32_t size)
{
    if (http->local)
    {
        int read = sceIoPread(http->fd, buffer, size, http->offset);
        http->offset += read;
        return read;
    }
    else
    {
        // LOG("http asking to read %u bytes", size);
        int read = sceHttpReadData(http->req, buffer, size);
        // LOG("http read %d bytes", size, read);
        if (read < 0)
        {
            LOG("sceHttpReadData failed: 0x%08x", read);
        }
        return read;
    }
}

void pkgi_http_close(pkgi_http* http)
{
    LOG("http close");
    if (http->local)
    {
        sceIoClose(http->fd);
    }
    else
    {
        sceHttpDeleteRequest(http->req);
        sceHttpDeleteConnection(http->conn);
        sceHttpDeleteTemplate(http->tmpl);
    }
    http->used = 0;
}

void pkgi_mkdirs(char* path)
{
    char* ptr = path;
    while (*ptr)
    {
        while (*ptr && *ptr != '/')
            ptr++;

        char last = *ptr;
        *ptr = 0;
        LOG("mkdir %s", path);
        int err = sceIoMkdir(path, 0777);
        if (err < 0 && err != PKGI_ERRNO_EEXIST)
            throw std::runtime_error(fmt::format(
                    "sceIoMkdir({}) failed: {:#08x}",
                    path,
                    static_cast<uint32_t>(err)));
        *ptr++ = last;
        if (last == 0)
            break;
    }
}

void pkgi_rm(const char* file)
{
    int err = sceIoRemove(file);
    if (err < 0)
    {
        LOG("error removing %s file, err=0x%08x", file, err);
    }
}

int64_t pkgi_get_size(const char* path)
{
    SceIoStat stat;
    int err = sceIoGetstat(path, &stat);
    if (err < 0)
    {
        LOG("cannot get size of %s, err=0x%08x", path, err);
        return -1;
    }
    return stat.st_size;
}

void* pkgi_create(const char* path)
{
    LOG("sceIoOpen create on %s", path);
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0)
    {
        LOG("cannot create %s, err=0x%08x", path, fd);
        return NULL;
    }
    LOG("sceIoOpen returned fd=%d", fd);

    return (void*)(intptr_t)fd;
}

void* pkgi_openrw(const char* path)
{
    LOG("sceIoOpen openrw on %s", path);
    SceUID fd = sceIoOpen(path, SCE_O_RDWR, 0777);
    if (fd < 0)
    {
        LOG("cannot openrw %s, err=0x%08x", path, fd);
        return NULL;
    }
    LOG("sceIoOpen returned fd=%d", fd);

    return (void*)(intptr_t)fd;
}

void* pkgi_append(const char* path)
{
    LOG("sceIoOpen append on %s", path);
    SceUID fd =
            sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd < 0)
    {
        LOG("cannot append %s, err=0x%08x", path, fd);
        return NULL;
    }
    LOG("sceIoOpen returned fd=%d", fd);

    return (void*)(intptr_t)fd;
}

int pkgi_read(void* f, void* buffer, uint32_t size)
{
    LOG("asking to read %u bytes", size);
    int read = sceIoRead((SceUID)(intptr_t)f, buffer, size);
    if (read < 0)
    {
        LOG("sceIoRead error 0x%08x", read);
    }
    else
    {
        LOG("read %d bytes", read);
    }
    return read;
}

int pkgi_write(void* f, const void* buffer, uint32_t size)
{
    // LOG("asking to write %u bytes", size);
    int write = sceIoWrite((SceUID)(intptr_t)f, buffer, size);
    if (write < 0)
    {
        LOG("sceIoWrite error 0x%08x", write);
        return -1;
    }

    // LOG("wrote %d bytes", write);
    return (uint32_t)write == size;
}

void pkgi_close(void* f)
{
    SceUID fd = (SceUID)(intptr_t)f;
    LOG("closing file %d", fd);
    int err = sceIoClose(fd);
    if (err < 0)
    {
        LOG("close error 0x%08x", err);
    }
}
