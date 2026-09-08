#include "clipboard_image.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

#include "file_pusher.h"
#include "util/file.h"
#include "util/log.h"

// Try image/png first (smaller, supports transparency), then image/bmp as a
// fallback (on Windows, some applications only expose CF_DIB, which SDL
// converts to a complete BMP file).
static const char *const IMAGE_MIME_TYPES[] = {
    "image/png",
    "image/bmp",
};

static const char *const IMAGE_EXTENSIONS[] = {
    "png",
    "bmp",
};

// Generate a temporary file path "<dir>/scrcpy-YYYYMMDD-HHMMSS[-N].<ext>" in
// dir (which must already exist). The returned path is owned by the caller.
static char *
generate_tmp_file_path(const char *dir, const char *ext) {
    time_t t = time(NULL);
    struct tm *tm = gmtime(&t);
    if (!tm) {
        LOGW("Could not get current time");
        return NULL;
    }

    char base[32];
    if (!strftime(base, sizeof(base), "scrcpy-%Y%m%d-%H%M%S", tm)) {
        LOGW("Could not format timestamp");
        return NULL;
    }

    for (unsigned i = 0; i < 100; ++i) {
        char name[64];
        if (i == 0) {
            snprintf(name, sizeof(name), "%s.%s", base, ext);
        } else {
            snprintf(name, sizeof(name), "%s-%u.%s", base, i, ext);
        }

        char *path = sc_file_build_path(dir, name);
        if (!path) {
            // Error already logged
            return NULL;
        }

        if (!SDL_GetPathInfo(path, NULL)) {
            // The file does not exist (or an error occurred): use this path
            return path;
        }

        // The file already exists (a leftover from a previous run?), try
        // another name
        free(path);
    }

    LOGW("Could not generate a temporary file name");
    return NULL;
}

// Write the image data to a temporary file.
// The returned path is owned by the caller.
static char *
save_image_to_tmp_file(const void *data, size_t size, const char *ext) {
    // SDL_GetPrefPath() returns a directory where we can safely write files,
    // creating it if necessary. It is UTF-8-encoded, like all SDL paths (so
    // it may contain non-ASCII characters on Windows, use SDL file APIs to
    // handle it).
    char *dir = SDL_GetPrefPath(NULL, "scrcpy");
    if (!dir) {
        LOGW("Could not get a temporary directory: %s", SDL_GetError());
        return NULL;
    }

    // SDL_GetPrefPath() returns a trailing path separator, while
    // sc_file_build_path() adds one: strip it to avoid a doubled separator
    size_t dir_len = strlen(dir);
    while (dir_len > 0 && (dir[dir_len - 1] == '/'
                           || dir[dir_len - 1] == SC_PATH_SEPARATOR)) {
        dir[--dir_len] = '\0';
    }

    char *path = generate_tmp_file_path(dir, ext);
    SDL_free(dir);
    if (!path) {
        // Error already logged
        return NULL;
    }

    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) {
        LOGW("Could not open %s: %s", path, SDL_GetError());
        free(path);
        return NULL;
    }

    size_t written = SDL_WriteIO(io, data, size);
    if (written != size) {
        LOGW("Could not write %s: %s", path, SDL_GetError());
        SDL_CloseIO(io);
        SDL_RemovePath(path);
        free(path);
        return NULL;
    }

    if (!SDL_CloseIO(io)) {
        LOGW("Could not close %s: %s", path, SDL_GetError());
        SDL_RemovePath(path);
        free(path);
        return NULL;
    }

    return path;
}

bool
sc_clipboard_image_paste(struct sc_file_pusher *fp) {
    assert(fp);

    if (SDL_HasClipboardText()) {
        // Text takes priority: let the normal text paste occur (a clipboard
        // may contain both text and image, for example when copying from a
        // spreadsheet)
        return false;
    }

    void *data = NULL;
    size_t size = 0;
    const char *ext = NULL;
    for (size_t i = 0; i < ARRAY_LEN(IMAGE_MIME_TYPES); ++i) {
        if (!SDL_HasClipboardData(IMAGE_MIME_TYPES[i])) {
            continue;
        }

        data = SDL_GetClipboardData(IMAGE_MIME_TYPES[i], &size);
        if (!data) {
            LOGW("Could not get clipboard image data: %s", SDL_GetError());
            // Try the next mime type
            continue;
        }
        if (size == 0) {
            SDL_free(data);
            data = NULL;
            continue;
        }
        ext = IMAGE_EXTENSIONS[i];
        break;
    }

    if (!data) {
        // The clipboard contains no image
        return false;
    }

    LOGI("Clipboard image detected (%s)", ext);

    char *file = save_image_to_tmp_file(data, size, ext);
    SDL_free(data);
    if (!file) {
        // Error already logged
        return true;
    }

    // sc_file_pusher_request() takes ownership of file on success (the local
    // temporary file is removed once pushed)
    bool ok = sc_file_pusher_request(fp, SC_FILE_PUSHER_ACTION_PUSH_IMAGE,
                                     file);
    if (!ok) {
        SDL_RemovePath(file);
        free(file);
    }

    return true;
}
