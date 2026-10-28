#include "darkmode.h"

#include <stdbool.h>
#include <windows.h>
#include <dwmapi.h>
#include <SDL3/SDL_properties.h>

#include "util/log.h"

// Only defined in Windows SDK 10.0.22000.0+ (supported by Windows 10 20H1+)
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
# define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static bool
is_dark_mode_enabled(void) {
    // AppsUseLightTheme: 0 = dark, 1 = light (default)
    DWORD value = 1;
    DWORD size = sizeof(value);
    LSTATUS r = RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &value, &size);
    return r == ERROR_SUCCESS && value == 0;
}

void
sc_darkmode_apply_to_window(SDL_Window *window) {
    if (!is_dark_mode_enabled()) {
        return;
    }

    HWND hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                       SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                       NULL);
    if (!hwnd) {
        LOGW("Could not get the window handle: %s", SDL_GetError());
        return;
    }

    BOOL dark = TRUE;
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                       &dark, sizeof(dark));
    if (FAILED(hr)) {
        // Expected before Windows 10 20H1
        LOGD("Could not use a dark title bar");
    }
}
