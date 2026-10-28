#include "embed.h"

#include <windows.h>
#include <SDL3/SDL_properties.h>

#include "util/log.h"

bool
sc_win_embed_window(SDL_Window *window, uint64_t parent) {
    HWND hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                       SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                       NULL);
    if (!hwnd) {
        LOGE("Could not get the window handle: %s", SDL_GetError());
        return false;
    }

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU
               | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    style |= WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);

    LONG_PTR exstyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    exstyle |= WS_EX_NOPARENTNOTIFY;
    exstyle &= ~WS_EX_APPWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exstyle);

    if (!SetParent(hwnd, (HWND) (uintptr_t) parent)) {
        LOGE("Could not set the parent window (error %lu)", GetLastError());
        return false;
    }

    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    return true;
}
