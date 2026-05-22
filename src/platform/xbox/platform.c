#include <stdio.h>
#include <xboxkrnl/xboxkrnl.h>
#include <nxdk/format.h>
#include <nxdk/mount.h>
#include <nxdk/path.h>
#include <nxdk/net.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <hal/debug.h>
#include <windows.h>

#include <time.h>
#include <stdlib.h>
#include <setjmp.h>
#include <jpeglib.h>
#include <lvgl.h>
#include "lithiumx.h"
#include "../platform.h"
#include "lvgl_drivers/lv_port_disp.h"
#include "lvgl_drivers/lv_port_indev.h"

#include "lwip/api.h"
#include "lwip/tcpip.h"
#include "lwip/apps/sntp.h"
#include "lwip/netif.h"
#include "ftpd/ftp.h"
#include "xbox_info.h"

ULONG platform_cpu_temp, platform_mb_temp, platform_tray_state = 0x70;
UCHAR platform_temp_unit;

// Splash overlay state
uint32_t *splash_fb_buf = NULL;   // was: static uint32_t *splash_fb_buf
int splash_screen_w = 0;          // was: static int splash_screen_w
int splash_screen_h = 0;          // was: static int splash_screen_h
volatile bool splash_active = false;

static char splash_status_msg[128] = {0};

void platform_splash_set_status(const char *msg)
{
    strncpy(splash_status_msg, msg, sizeof(splash_status_msg) - 1);
}

static void splash_draw_status(int sw, int sh)
{
    if (!splash_status_msg[0]) return;
    int text_x = sw / 2 - ((int)strlen(splash_status_msg) / 2 * 8);
    int text_y = sh * 80 / 100;  
    debugMoveCursor(text_x, text_y);
    debugPrint("%s", splash_status_msg);
}

void platform_splash_overlay(void)
{
    if (!splash_active || !splash_fb_buf) return;

    while (pb_busy()) {}
    while (pb_finished()) {}

    uint32_t *bb = (uint32_t *)pb_back_buffer();
    if (!bb) return;

    memcpy(bb, splash_fb_buf, splash_screen_w * splash_screen_h * 4);
    splash_draw_status(splash_screen_w, splash_screen_h);
    pb_wait_for_vbl();   // swap backbuffer → frontbuffer
}

void platform_splash_done(void)
{
    splash_active = false;
    if (splash_fb_buf) {
        free(splash_fb_buf);
        splash_fb_buf = NULL;
    }
}

// Pre-LVGL: herstel splash + toon status tekst direct op framebuffer
static void splash_print_status(int sw, int sh, const char *msg)
{
    if (!splash_fb_buf) return;
    uint32_t *fb = (uint32_t *)XVideoGetFB();
    if (!fb) return;
    memcpy(fb, splash_fb_buf, sw * sh * 4);
    strncpy(splash_status_msg, msg, sizeof(splash_status_msg) - 1);
    splash_draw_status(sw, sh);
    //Sleep(500);
}


void xbox_sntp_set_time(uint32_t ntp_s)
{
    DbgPrint("GOT TIME\n");
    static const LONGLONG NT_EPOCH_TIME_OFFSET = ((LONGLONG)(369 * 365 + 89) * 24 * 3600);
    LARGE_INTEGER xbox_nt_time, ntp_nt_time;

    KeQuerySystemTime(&xbox_nt_time);
    ntp_nt_time.QuadPart = ((uint64_t)ntp_s + NT_EPOCH_TIME_OFFSET) * 10000000;
    NtSetSystemTime(&ntp_nt_time, NULL);
}

static void ftp_startup(void *param)
{
    DbgPrint("STARTING FTP\n");
    ftp_server();
}

static void sntp_startup(void *param)
{
    DbgPrint("STARTING SNTP\n");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

static WINAPI DWORD autolaunch_dvd(LPVOID param)
{
    char targetPath[MAX_PATH];
    while (1)
    {
        Sleep(1000);
        // Check if we have media inserted in the DVD ROM
        HalReadSMCTrayState(&platform_tray_state, NULL);
        xbox_get_temps(&platform_cpu_temp, &platform_mb_temp, &platform_temp_unit);

        if (dash_settings.auto_launch_dvd == false)
        {
            continue;
        }
        if (platform_tray_state == 0x60)
        {
            // Prevent recursive launch by checking the current xbe isnt launched from the DVD itself
            nxGetCurrentXbeNtPath(targetPath);
            static const char *cd_path = "\\Device\\CdRom0";
            if (strncmp(targetPath, cd_path, strlen(cd_path)) == 0)
            {
                continue;
            }

            // Prep to launch
            lvgl_getlock();
            strcpy(dash_launch_path, "__DVD__");
            lv_set_quit(LV_QUIT_OTHER);
            lvgl_removelock();
        }
    }
    return 0;
}

static WINAPI DWORD network_startup(LPVOID param)
{
    DbgPrint("STARTING NETWORK\n");
    nxNetInit(NULL);
    sys_thread_new("ftp_startup", ftp_startup, NULL, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
    //SNTP should be started in TCPIP thread
    tcpip_callback(sntp_startup, NULL);
    return 0;
}

struct splash_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

static void splash_error_exit(j_common_ptr cinfo)
{
    struct splash_error_mgr *err = (struct splash_error_mgr *)cinfo->err;
    longjmp(err->setjmp_buffer, 1);
}

// Laad Q:\splash.jpg en teken het gecentreerd op de framebuffer.
// Wordt aangeroepen direct na XVideoSetMode(), vóór enige andere init.
// Geen LVGL of SDL nodig. Stille fallback als het bestand ontbreekt.
static void show_splash(int screen_w, int screen_h)
{
    // CD-ROM driver staat maar 1 handle tegelijk toe.
    // FindFirstFile openen en direct sluiten voor we fopen aanroepen.
    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile("D:\\splash.jpg", &fd);
    if (h == INVALID_HANDLE_VALUE) {
        debugPrint("splash: niet gevonden\n");
        return;
    }
    FindClose(h);

    FILE *fp = fopen("D:\\splash.jpg", "rb");
    debugPrint("splash fp=%p\n", fp);
    if (!fp) return;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);
    debugPrint("splash size=%ld\n", size);
    if (size <= 0) { fclose(fp); return; }

    struct jpeg_decompress_struct jinfo;
    struct splash_error_mgr jerr;

    jinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = splash_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&jinfo);
        fclose(fp);
        return;
    }

    jpeg_create_decompress(&jinfo);
    jpeg_stdio_src(&jinfo, fp);

    if (jpeg_read_header(&jinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&jinfo);
        fclose(fp);
        return;
    }

    // Gebruik libjpeg ingebouwde schaling (1/8 t/m 8/8) om zo dicht mogelijk
    // bij de schermresolutie te komen — snel en zonder extra geheugen.
    // We kiezen de kleinste schaalfactor waarbij de afbeelding nog groter is
    // dan het scherm, zodat de software-scale daarna alleen hoeft te verkleinen.
    jinfo.scale_num = 1;
    jinfo.scale_denom = 8;
    for (int denom = 8; denom >= 1; denom--) {
        jinfo.scale_num = denom;
        jinfo.scale_denom = 8;
        jpeg_calc_output_dimensions(&jinfo);
        if ((int)jinfo.output_width >= screen_w || (int)jinfo.output_height >= screen_h) {
            break;
        }
    }

    // Decodeer naar BGRA (32-bit), zelfde als jpg_decoder.c
    jinfo.out_color_space = JCS_EXT_BGRA;
    jinfo.do_fancy_upsampling = FALSE;
    jinfo.do_block_smoothing = FALSE;
    jinfo.dct_method = JDCT_FASTEST;
    jpeg_start_decompress(&jinfo);

    int img_w = (int)jinfo.output_width;
    int img_h = (int)jinfo.output_height;
    int row_stride = img_w * 4; // 4 bytes per pixel (BGRA)

    uint8_t *img_buf = malloc(img_w * img_h * 4);
    if (!img_buf) {
        jpeg_destroy_decompress(&jinfo);
        fclose(fp);
        return;
    }

    while ((int)jinfo.output_scanline < img_h) {
        uint8_t *row = img_buf + jinfo.output_scanline * row_stride;
        jpeg_read_scanlines(&jinfo, &row, 1);
    }

    jpeg_finish_decompress(&jinfo);
    jpeg_destroy_decompress(&jinfo);
    fclose(fp);

    // Schrijf naar Xbox framebuffer
    // XVideoGetFB() geeft een pointer naar het actieve framebuffer (XRGB8888)
    // BGRA van jpeglib → XRGB van Xbox: bytes zijn [B,G,R,A] vs [B,G,R,X] — volgorde is identiek
    uint32_t *fb = (uint32_t *)XVideoGetFB();
    if (!fb) {
        free(img_buf);
        return;
    }

    // Centreer de afbeelding op het scherm
    int offset_x = (screen_w - img_w) / 2;
    int offset_y = (screen_h - img_h) / 2;

    // Clamp zodat we niet buiten het scherm schrijven
    int src_x = 0, src_y = 0;
    int dst_x = offset_x, dst_y = offset_y;
    int copy_w = img_w, copy_h = img_h;

    if (dst_x < 0) { src_x = -dst_x; copy_w += dst_x; dst_x = 0; }
    if (dst_y < 0) { src_y = -dst_y; copy_h += dst_y; dst_y = 0; }
    if (dst_x + copy_w > screen_w) copy_w = screen_w - dst_x;
    if (dst_y + copy_h > screen_h) copy_h = screen_h - dst_y;

    for (int y = 0; y < copy_h; y++) {
        uint32_t *src_row = (uint32_t *)(img_buf + (src_y + y) * row_stride) + src_x;
        uint32_t *dst_row = fb + (dst_y + y) * screen_w + dst_x;
        memcpy(dst_row, src_row, copy_w * 4);
    }


    // Bereken schaalfactor met behoud van aspect ratio (letterbox/pillarbox)
    // Gebruik integer fixed-point (16.16) voor snelheid op de Xbox CPU
    int scale_x = (img_w << 16) / screen_w;
    int scale_y = (img_h << 16) / screen_h;
    int scale = scale_x > scale_y ? scale_x : scale_y; // grootste = volledige scherm vullen

    int out_w = (img_w << 16) / scale;
    int out_h = (img_h << 16) / scale;

    // Centreer op het scherm
    int dst_x0 = (screen_w - out_w) / 2;
    int dst_y0 = (screen_h - out_h) / 2;

    // Teken scherm rij voor rij met nearest-neighbor sampling
    for (int dy = 0; dy < screen_h; dy++) {
        uint32_t *dst_row = fb + dy * screen_w;

        // Buiten de afbeelding: zwart
        if (dy < dst_y0 || dy >= dst_y0 + out_h) {
            memset(dst_row, 0, screen_w * 4);
            continue;
        }

        int sy = ((dy - dst_y0) * scale) >> 16;
        if (sy >= img_h) sy = img_h - 1;
        uint32_t *src_row = (uint32_t *)(img_buf + sy * row_stride);

        // Links van de afbeelding: zwart
        for (int dx = 0; dx < dst_x0 && dx < screen_w; dx++) {
            dst_row[dx] = 0;
        }

        // Afbeelding pixels
        for (int dx = dst_x0; dx < dst_x0 + out_w && dx < screen_w; dx++) {
            int sx = ((dx - dst_x0) * scale) >> 16;
            if (sx >= img_w) sx = img_w - 1;
            dst_row[dx] = src_row[sx];
        }

        // Rechts van de afbeelding: zwart
        for (int dx = dst_x0 + out_w; dx < screen_w; dx++) {
            dst_row[dx] = 0;
        }
    }
    // Bewaar de geschaalde framebuffer inhoud als splash overlay.
    splash_fb_buf = malloc(screen_w * screen_h * 4);
    if (splash_fb_buf) {
        memcpy(splash_fb_buf, fb, screen_w * screen_h * 4);
        splash_screen_w = screen_w;
        splash_screen_h = screen_h;
        splash_active = true;
    }
    free(img_buf);
}


void platform_init(int *w, int *h)
{
    // First try 720p. This is the preferred resolution
    *w = 1280;
    *h = 720;
    if (XVideoSetMode(*w, *h, LV_COLOR_DEPTH, REFRESH_DEFAULT) == false)
    {
        // Fall back to 640*480
        *w = 640;
        *h = 480;
        if (XVideoSetMode(*w, *h, LV_COLOR_DEPTH, REFRESH_DEFAULT) == false)
        {
            // Try whatever else the xbox is happy with
            VIDEO_MODE xmode;
            void *p = NULL;
            while (XVideoListModes(&xmode, 0, 0, &p))
            {
                if (xmode.width == 1080) continue;
                if (xmode.width == 720) continue; // 720x480 doesnt work on pbkit for some reason
                XVideoSetMode(xmode.width, xmode.height, xmode.bpp, xmode.refresh);;
                break;
            }
        }
    }

    VIDEO_MODE xmode = XVideoGetMode();
    *w = xmode.width;
    *h = xmode.height;

    // Toon splash zo vroeg mogelijk — vóór partities mounten
    char xbe_path[256];
    nxGetCurrentXbeNtPath(xbe_path);
    Sleep(500);
    show_splash(*w, *h);

    splash_print_status(*w, *h, "Mounting Partitions...");

    // nxdk automounts D to the root xbe path. Lets undo that
    if (nxIsDriveMounted('D'))
    {
        nxUnmountDrive('D');
    }

    // Mount the DVD drive
    nxMountDrive('D', "\\Device\\CdRom0");

    // Mount root of LithiumX xbe to Q:
    char targetPath[MAX_PATH];
    nxGetCurrentXbeNtPath(targetPath);
    *(strrchr(targetPath, '\\') + 1) = '\0';
    nxMountDrive('Q', targetPath);

    // Mount stock partitions
    nxMountDrive('C', "\\Device\\Harddisk0\\Partition2\\");
    nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\");
    nxMountDrive('X', "\\Device\\Harddisk0\\Partition3\\");
    nxMountDrive('Y', "\\Device\\Harddisk0\\Partition4\\");
    nxMountDrive('Z', "\\Device\\Harddisk0\\Partition5\\");

    // Mount extended partitions
    // NOTE: Both the retail kernel and modified kernels will mount these partitions
    // if they exist and silently fail if they don't. So we can just try to mount them
    // and not worry about checking if they exist.
    nxMountDrive('F', "\\Device\\Harddisk0\\Partition6\\");
    nxMountDrive('G', "\\Device\\Harddisk0\\Partition7\\");
    nxMountDrive('R', "\\Device\\Harddisk0\\Partition8\\");
    nxMountDrive('S', "\\Device\\Harddisk0\\Partition9\\");
    nxMountDrive('V', "\\Device\\Harddisk0\\Partition10\\");
    nxMountDrive('W', "\\Device\\Harddisk0\\Partition11\\");
    nxMountDrive('A', "\\Device\\Harddisk0\\Partition12\\");
    nxMountDrive('B', "\\Device\\Harddisk0\\Partition13\\");
    nxMountDrive('P', "\\Device\\Harddisk0\\Partition14\\");

    splash_print_status(*w, *h, "Starting Services...");

    CreateDirectoryA("E:\\UDATA", NULL);
    CreateDirectoryA("E:\\UDATA\\LithiumX", NULL);
    FILE *fp = fopen("E:\\UDATA\\LithiumX\\TitleMeta.xbx", "wb");
    if (fp)
    {
        fprintf(fp, "TitleName=LithiumX Dashboard\r\n");
        fclose(fp);
    }
    // CreateThread for autolaunch_dvd
    CreateThread(NULL, 0, autolaunch_dvd, NULL, 0, NULL);
    CreateThread(NULL, 0, network_startup, NULL, 0, NULL);
}

void nvnetdrv_stop_txrx (void);
int XboxGetFullLaunchPath(const char *input, char *output);
void usbh_core_deinit();
void platform_launch_iso(const char *path);

void platform_quit(lv_quit_event_t event)
{
    nvnetdrv_stop();
    usbh_core_deinit();
    debugClearScreen();

    if (event == LV_REBOOT)
    {
        printf("LV_REBOOT\n");
        HalReturnToFirmware(HalRebootRoutine);
    }
    else if (event == LV_SHUTDOWN)
    {
        printf("SHUTDOWN\n");
        HalInitiateShutdown();
    }
    else if (event == LV_QUIT_OTHER)
    {
        if (strcmp(dash_launch_path, "__MSDASH__") == 0)
        {
            // FIXME: Do we need to eject disk?
            strcpy(dash_launch_path, "C:\\xboxdash.xbe");
        }
        else if (strcmp(dash_launch_path, "__DVD__") == 0)
        {
            strcpy(dash_launch_path, "\\Device\\CdRom0\\default.xbe");
        }
        if (dash_launcher_is_iso(dash_launch_path, NULL, NULL))
        {
            platform_launch_iso(dash_launch_path);
        }
        else
        {
            char xbox_launch_path[MAX_PATH];
            XboxGetFullLaunchPath(dash_launch_path, xbox_launch_path);

            DbgPrint("Launching %s\n", dash_launch_path);
            DbgPrint("Launching %s\n", xbox_launch_path);
            XLaunchXBE(xbox_launch_path);
            DbgPrint("Error launching. Reboot\n");
            Sleep(500);
            DbgPrint("ERROR: Could not launch %s\n", dash_launch_path);
            HalReturnToFirmware(HalRebootRoutine);
        }
    }
}

void info_update_callback(lv_timer_t *timer)
{
    static CHAR info_text[1024];
    lv_obj_t *window = timer->user_data;
    lv_obj_t *label = lv_obj_get_child(window, 0);
    lv_label_set_recolor(label, true);

    const CHAR *encoder;
    static UCHAR mac_address[0x06], serial_number[0x0D];
    static ULONG video_region, game_region;
    ULONG type, encoder_check;

    static int clock_calc = 0;
    static ULONG cpu_speed, gpu_speed;
    if (clock_calc ^= 1)
    {
        static ULONGLONG f_rdtsc = 0;
        static DWORD f_ticks = 0;
        cpu_speed = xbox_get_cpu_frequency(&f_rdtsc, &f_ticks) / 1000;
        gpu_speed = xbox_get_gpu_frequency();
    }

    static bool first = 1;
    if (first)
    {
        first = 0;
        ExQueryNonVolatileSetting(XC_FACTORY_SERIAL_NUMBER, &type, &serial_number, sizeof(serial_number), NULL);
        ExQueryNonVolatileSetting(XC_FACTORY_ETHERNET_ADDR, &type, &mac_address, sizeof(mac_address), NULL);
        ExQueryNonVolatileSetting(XC_FACTORY_AV_REGION, &type, &video_region, sizeof(video_region), NULL);
        ExQueryNonVolatileSetting(XC_FACTORY_GAME_REGION, &type, &game_region, sizeof(game_region), NULL);
    }

    encoder = get_encoder_str();

    lv_snprintf(info_text, sizeof(info_text),
                "%s Date/Time:# %s\n"
                "%s IP:# %s\n"
                "%s Tray State:# %s\n"
                "%s RAM:# %s MB\n"
                "%s CPU:# %lu%c, %s MB:# %lu%c\n"
                "%s CPU Freq:# %luMHz, %s GPU Freq:# %luMHz\n"
                "%s Hardware Version:# %s\n"
                "%s Serial Number:# %s\n"
                "%s Mac Address :# %02x:%02x:%02x:%02x:%02x:%02x\n"
                "%s Encoder:# %s\n"
                "%s Kernel:# %u.%u.%u.%u\n"
                "%s Video Region:# %s\n"
                "%s Game Region:# %s\n"
                "%s Build Commit:# %s\n",
                DASH_MENU_COLOR, xbox_get_date_time(),
                DASH_MENU_COLOR, xbox_get_ip_address(),
                DASH_MENU_COLOR, tray_state_str(platform_tray_state),
                DASH_MENU_COLOR, xbox_get_ram_usage(),
                DASH_MENU_COLOR, platform_cpu_temp, platform_temp_unit, DASH_MENU_COLOR, platform_mb_temp, platform_temp_unit,
                DASH_MENU_COLOR, cpu_speed, DASH_MENU_COLOR, gpu_speed,
                DASH_MENU_COLOR, xbox_get_verion(),
                DASH_MENU_COLOR, serial_number,
                DASH_MENU_COLOR, mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5],
                DASH_MENU_COLOR, encoder,
                DASH_MENU_COLOR, XboxKrnlVersion.Major, XboxKrnlVersion.Minor, XboxKrnlVersion.Build, XboxKrnlVersion.Qfe,
                DASH_MENU_COLOR, video_region_str(video_region),
                DASH_MENU_COLOR, game_region_str(game_region),
                DASH_MENU_COLOR, BUILD_VERSION);
    lv_label_set_text_static(label, info_text);
}

static void window_closed(lv_event_t *event)
{
    lv_obj_t *window = lv_event_get_target(event);
    lv_timer_del(lv_event_get_user_data(event));
}

void platform_system_info(lv_obj_t *window)
{
    lv_timer_t *timer = lv_timer_create(info_update_callback, 1000, window);
    lv_obj_t *label = lv_label_create(window);
    lv_obj_add_event_cb(window, window_closed, LV_EVENT_DELETE, timer);
    lv_timer_ready(timer);
}

static void recursive_empty_folder(const char *folderPath) {
    char searchPath[MAX_PATH];
    lv_snprintf(searchPath, MAX_PATH, "%s\\*", folderPath);

    WIN32_FIND_DATA findData;
    HANDLE hFind = FindFirstFile(searchPath, &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        printf("Failed to open directory: %s\n", folderPath);
        return;
    }

    do {
        // Ignore `.` and `..`
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) {
            continue;
        }

        char filePath[MAX_PATH];
        lv_snprintf(filePath, MAX_PATH, "%s\\%s", folderPath, findData.cFileName);

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Recursively delete subdirectory
            recursive_empty_folder(filePath);
            RemoveDirectory(filePath);
        } else {
            // Delete file
            if (!DeleteFile(filePath)) {
                printf("Failed to delete file: %s\n", filePath);
            }
        }

    } while (FindNextFile(hFind, &findData));

    FindClose(hFind);
}

void platform_flush_cache()
{
    // Source: https://github.com/dracc/NevolutionX/blob/master/Sources/wipeCache.cpp
    const char *partitions[] = {
        "\\Device\\Harddisk0\\Partition3", // "X"
        "\\Device\\Harddisk0\\Partition4", // "Y"
        "\\Device\\Harddisk0\\Partition5"  // "Z"
    };
    const int partition_cnt = sizeof(partitions) / sizeof(partitions[0]);
    for (int i = 0; i < partition_cnt; i++)
    {
        if (nxFormatVolume(partitions[i], 0) == false)
        {
            DbgPrint("ERROR: Could not format %s\n", partitions[i]);
        }
        else
        {
            DbgPrint("TRACE: Formatted %s ok!\n", partitions[i]);
        }
    }

    // Delete E:\CACHE too
    recursive_empty_folder("E:\\CACHE");
}

// YYYY-MM-DD HH:MM:SS
void platform_get_iso8601_time(char time_str[20])
{
    static SYSTEMTIME st = {.wHour = 255};
    static DWORD tick_cnt = 0;

    DWORD change = KeTickCount - tick_cnt;
    if (change > 1000)
    {
        st.wSecond += change / 1000;
        tick_cnt = KeTickCount - (change % 1000);
        while (st.wSecond > 59)
        {
            st.wMinute++;
            st.wSecond -= 60;
        }
        while (st.wMinute > 59)
        {
            st.wHour++;
            st.wMinute -= 60;
        }
    }
    if (st.wHour > 23)
    {
        // This is quite slow (reads from EEPROM etc) so we only read once then
        // work out time based on tick count. Will read from EEPROM once first time or at midnight
        GetLocalTime(&st);
        tick_cnt = KeTickCount;
    }

    lv_snprintf(time_str, 20, "%04d-%02d-%02d %02d:%02d:%02d",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

/*
 * Copyright (C) 2014, Galois, Inc.
 * This sotware is distributed under a standard, three-clause BSD license.
 * Please see the file LICENSE, distributed with this software, for specific
 * terms and conditions.
 */

#define isdigit(c) (c >= '0' && c <= '9')

double atof(const char *s)
{
    // This function stolen from either Rolf Neugebauer or Andrew Tolmach.
    // Probably Rolf.
    double a = 0.0;
    int e = 0;
    int c;
    while ((c = *s++) != '\0' && isdigit(c))
    {
        a = a * 10.0 + (c - '0');
    }
    if (c == '.')
    {
        while ((c = *s++) != '\0' && isdigit(c))
        {
            a = a * 10.0 + (c - '0');
            e = e - 1;
        }
    }
    if (c == 'e' || c == 'E')
    {
        int sign = 1;
        int i = 0;
        c = *s++;
        if (c == '+')
            c = *s++;
        else if (c == '-')
        {
            c = *s++;
            sign = -1;
        }
        while (isdigit(c))
        {
            i = i * 10 + (c - '0');
            c = *s++;
        }
        e += i * sign;
    }
    while (e > 0)
    {
        a *= 10.0;
        e--;
    }
    while (e < 0)
    {
        a *= 0.1;
        e++;
    }
    return a;
}

size_t strnlen(const char *s, size_t count)
{
    const char *sc;

    for (sc = s; count-- && *sc != '\0'; ++sc)
        /* nothing */;
    return sc - s;
}

int strcasecmp(const char *s1, const char *s2)
{
  int result;

  while (1) {
    result = tolower(*s1) - tolower(*s2);
    if (result != 0 || *s1 == '\0')
      break;

    ++s1;
    ++s2;
  }

  return result;
}