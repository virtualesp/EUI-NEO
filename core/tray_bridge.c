#include "core/tray_bridge.h"

#if defined(EUI_TRAY_WINAPI)
#define TRAY_WINAPI 1
#define EUI_TRAY_HAS_BACKEND 1
#elif defined(EUI_TRAY_APPKIT)
#define TRAY_APPKIT 1
#define EUI_TRAY_HAS_BACKEND 1
#elif defined(EUI_TRAY_APPINDICATOR)
#define TRAY_APPINDICATOR 1
#define EUI_TRAY_HAS_BACKEND 1
#else
#define EUI_TRAY_HAS_BACKEND 0
#endif

#if EUI_TRAY_HAS_BACKEND
#if defined(EUI_TRAY_APPKIT)
#include <objc/message.h>
#include <string.h>
#define objc_msgSend ((id (*)(id, SEL, ...))objc_msgSend)
#endif

#include "tray.h"

#if defined(EUI_TRAY_APPKIT)
#undef objc_msgSend
#endif

static int g_initialized = 0;
static int g_show_requested = 0;
static int g_exit_requested = 0;
static struct tray g_tray;

static void eui_tray_show(struct tray_menu* item) {
    (void)item;
    g_show_requested = 1;
}

static void eui_tray_exit(struct tray_menu* item) {
    (void)item;
    g_exit_requested = 1;
}

static struct tray_menu g_menu[] = {
    {"Show", 0, 0, eui_tray_show, 0},
    {"-", 0, 0, 0, 0},
    {"Exit", 0, 0, eui_tray_exit, 0},
    {0, 0, 0, 0, 0}
};

int eui_tray_init(const char* icon_path) {
    if (g_initialized) {
        return 1;
    }

    g_show_requested = 0;
    g_exit_requested = 0;
    g_tray.icon = (char*)(icon_path != 0 ? icon_path : "");
    g_tray.menu = g_menu;

    if (tray_init(&g_tray) != 0) {
        return 0;
    }

    g_initialized = 1;
    tray_update(&g_tray);
    return 1;
}

int eui_tray_is_initialized(void) {
    return g_initialized;
}

void eui_tray_poll(int blocking) {
    if (!g_initialized) {
        return;
    }

#if defined(EUI_TRAY_WINAPI)
    (void)blocking;
#else
    if (tray_loop(blocking ? 1 : 0) != 0) {
        g_exit_requested = 1;
    }
#endif
}

int eui_tray_consume_show_requested(void) {
    int requested = g_show_requested;
    g_show_requested = 0;
    return requested;
}

int eui_tray_consume_exit_requested(void) {
    int requested = g_exit_requested;
    g_exit_requested = 0;
    return requested;
}

void eui_tray_shutdown(void) {
    if (!g_initialized) {
        return;
    }

    tray_exit();
    g_initialized = 0;
    g_show_requested = 0;
    g_exit_requested = 0;
}

#else

int eui_tray_init(const char* icon_path) {
    (void)icon_path;
    return 0;
}

int eui_tray_is_initialized(void) {
    return 0;
}

void eui_tray_poll(int blocking) {
    (void)blocking;
}

int eui_tray_consume_show_requested(void) {
    return 0;
}

int eui_tray_consume_exit_requested(void) {
    return 0;
}

void eui_tray_shutdown(void) {
}

#endif
