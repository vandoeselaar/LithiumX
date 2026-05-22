#define NANOPRINTF_IMPLEMENTATION
#define NANOPRINTF_SNPRINTF_SAFE_TRIM_STRING_ON_OVERFLOW
#include <lvgl.h>
#include "lithiumx.h"

static CRITICAL_SECTION tlsf_crit_sec;
static tlsf_t mem_pool;
static uint8_t mem_pool_data[3U * 1024U * 1024U];

static SDL_mutex *lvgl_mutex;

// Actual definitions (allocates memory for them)
//uint32_t *splash_fb_buf = NULL;
//int splash_screen_w = 0; 
//int splash_screen_h = 0;

volatile bool lvgl_frame_ready = false;

keyboard_map_t lvgl_keyboard_map[] =
{
    {.sdl_map = SDLK_ESCAPE, .lvgl_map = DASH_SETTINGS_PAGE},
    {.sdl_map = SDLK_BACKSPACE, .lvgl_map = LV_KEY_ESC},
    {.sdl_map = SDLK_RETURN, .lvgl_map = LV_KEY_ENTER},
    {.sdl_map = SDLK_PAGEDOWN, .lvgl_map = DASH_PREV_PAGE},
    {.sdl_map = SDLK_PAGEUP, .lvgl_map = DASH_NEXT_PAGE},
    {.sdl_map = SDLK_UP, .lvgl_map = LV_KEY_UP},
    {.sdl_map = SDLK_DOWN, .lvgl_map = LV_KEY_DOWN},
    {.sdl_map = SDLK_LEFT, .lvgl_map = LV_KEY_LEFT},
    {.sdl_map = SDLK_RIGHT, .lvgl_map = LV_KEY_RIGHT},
    {.sdl_map = 0, .lvgl_map = 0}
};

gamecontroller_map_t lvgl_gamecontroller_map[] =
{
    {.sdl_map = SDL_CONTROLLER_BUTTON_A, .lvgl_map = LV_KEY_ENTER},
    {.sdl_map = SDL_CONTROLLER_BUTTON_B, .lvgl_map = LV_KEY_ESC},
    {.sdl_map = SDL_CONTROLLER_BUTTON_X, .lvgl_map = LV_KEY_BACKSPACE},
    {.sdl_map = SDL_CONTROLLER_BUTTON_Y, .lvgl_map = DASH_INFO_PAGE},
    {.sdl_map = SDL_CONTROLLER_BUTTON_BACK, .lvgl_map = DASH_INFO_PAGE},
    {.sdl_map = SDL_CONTROLLER_BUTTON_GUIDE, .lvgl_map = 0},
    {.sdl_map = SDL_CONTROLLER_BUTTON_START, .lvgl_map = DASH_SETTINGS_PAGE},
    {.sdl_map = SDL_CONTROLLER_BUTTON_LEFTSTICK, .lvgl_map = 0},
    {.sdl_map = SDL_CONTROLLER_BUTTON_RIGHTSTICK, .lvgl_map = 0},
    {.sdl_map = SDL_CONTROLLER_BUTTON_LEFTSHOULDER, .lvgl_map = DASH_PREV_PAGE},
    {.sdl_map = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, .lvgl_map = DASH_NEXT_PAGE},
    {.sdl_map = SDL_CONTROLLER_BUTTON_DPAD_UP, .lvgl_map = LV_KEY_UP},
    {.sdl_map = SDL_CONTROLLER_BUTTON_DPAD_DOWN, .lvgl_map = LV_KEY_DOWN},
    {.sdl_map = SDL_CONTROLLER_BUTTON_DPAD_LEFT, .lvgl_map = LV_KEY_LEFT},
    {.sdl_map = SDL_CONTROLLER_BUTTON_DPAD_RIGHT, .lvgl_map = LV_KEY_RIGHT},
    {.sdl_map = 0, .lvgl_map = 0}
};

// lvgl isn't thread safe, but we can somewhat make it
// by wrapping task handler and any other interactions with these locks
void lvgl_getlock(void)
{
    if (SDL_LockMutex(lvgl_mutex))
    {
        assert(0);
    }
}

void lvgl_removelock(void)
{
    if (SDL_UnlockMutex(lvgl_mutex))
    {
        assert(0);
    }
}

// Output handler for lvgl
void lvgl_putstring(const char *buf)
{
    printf("%s", buf);
}

size_t tlsf_usage = 0;
// Replace lvgls internal allocator with basically the same thing
// but wrapped in crit sec for thread safety.
void *lx_mem_alloc(size_t size)
{
    EnterCriticalSection(&tlsf_crit_sec);
    void *ptr = tlsf_malloc(mem_pool, size);
    tlsf_usage += tlsf_block_size(ptr);
    LeaveCriticalSection(&tlsf_crit_sec);
    return ptr;
}

void *lx_mem_realloc(void *data, size_t new_size)
{
    EnterCriticalSection(&tlsf_crit_sec);
    tlsf_usage -= tlsf_block_size(data);
    void *ptr = tlsf_realloc(mem_pool, data, new_size);
    tlsf_usage += tlsf_block_size(ptr);
    LeaveCriticalSection(&tlsf_crit_sec);
    return ptr;
}

void lx_mem_free(void *data)
{
    EnterCriticalSection(&tlsf_crit_sec);
    tlsf_usage -= tlsf_block_size(data);
    tlsf_free(mem_pool, data);
    LeaveCriticalSection(&tlsf_crit_sec);
}

void lx_mem_usage(uint32_t *used, uint32_t *capacity)
{
    if (used)
    {
        EnterCriticalSection(&tlsf_crit_sec);
        *used = tlsf_usage;
        LeaveCriticalSection(&tlsf_crit_sec);
    }
    if (capacity)
    {
        *capacity = sizeof(mem_pool_data);
    }
}

static void npf_putchar(int c, void *ctx)
{
    (void)ctx;
    #ifdef NXDK
    DbgPrint("%c", c);
    #else
    printf("%c", c);
    #endif
}

void dash_printf(dash_debug_level_t level, const char *format, ...)
{
    if (level < NANO_DEBUG_LEVEL)
    {
        return;
    }
    va_list argList;
    va_start(argList, format);
    npf_vpprintf(npf_putchar, NULL, format, argList);
    va_end(argList);
}

int main(int argc, char* argv[]) {
    (void) argc;
    (void) argv;

    int w,h;
    InitializeCriticalSection(&tlsf_crit_sec);
    mem_pool = tlsf_create_with_pool(mem_pool_data, sizeof(mem_pool_data));

    toml_set_memutil(lx_mem_alloc, lx_mem_free);

    dash_printf(LEVEL_TRACE, "Initialising Platform\n");
    platform_init(&w, &h);

    lvgl_mutex = SDL_CreateMutex();
    assert(lvgl_mutex);

    // ==========================================
    // DEONTBREKENDE LVGL INIT BLOK TERUGGEZET:
    // ==========================================
    dash_printf(LEVEL_TRACE, "Initialising LVGL\n");
    lv_init();
    lv_log_register_print_cb(lvgl_putstring);
    
    // Start de display driver (dit tekent ook de eerste splash in de backbuffer)
    lv_port_disp_init(w, h);
    platform_splash_overlay();   // Herstel direct na init, vóór indev en dash_init
    pb_wait_for_vbl();           // Wacht op VBL zodat de splash zeker zichtbaar is
    
    // Start de controller / input driver
    lv_port_indev_init(false);
    platform_splash_overlay();   // Herstel na LVGL display init
    // ==========================================

    dash_printf(LEVEL_TRACE, "Creating dash\n");
    platform_splash_set_status("Loading Database...");
    dash_init();
    platform_splash_overlay(); 
    dash_printf(LEVEL_TRACE, "Enter dash busy loop\n");

#ifdef NXDK
    lv_disp_t *disp = lv_obj_get_disp(lv_scr_act());
    lv_timer_del(disp->refr_timer);
    disp->refr_timer = NULL;
#endif

    // 1. Deactivate the splash flag so disp_flush is allowed to process the UI
    extern volatile bool splash_active;
    splash_active = false;

    // 2. Force LVGL to mark the entire screen as dirty so it definitely 
    // renders the dashboard components on its very first loop pass.
    lv_obj_invalidate(lv_scr_act());

    extern volatile bool lvgl_frame_ready;

    while (lv_get_quit() == LV_QUIT_NONE)
    {
        int s,e,t;
        s = SDL_GetTicks();
        lvgl_getlock();
        lv_task_handler();
        lvgl_removelock();
        
        #ifdef NXDK
        lvgl_getlock();
        _lv_disp_refr_timer(NULL);
        lvgl_removelock();
        
        // 3. Only swap hardware video registers if a new dashboard frame was fully built
        if (lvgl_frame_ready)
        {
            pb_wait_for_vbl();         // Flip the clean dashboard frame onto the TV
            lvgl_frame_ready = false;  // Reset token for the next frame
        }
        else
        {
            SDL_Delay(1);              // Idle safely to prevent pinning the CPU to 100% when UI is static
        }
        #else
        e = SDL_GetTicks();
        t = e - s;
        if (t < LV_DISP_DEF_REFR_PERIOD)
        {
            SDL_Delay(LV_DISP_DEF_REFR_PERIOD - t);
        }
        #endif
    }
    
    dash_printf(LEVEL_TRACE, "Quitting dash with quit event %d\n", lv_get_quit());
    lv_port_disp_deinit();
    lv_port_indev_deinit();
    platform_quit(lv_get_quit());
    return 0;
}