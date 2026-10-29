#include "file_pusher.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_init.h>

#include "adb/adb.h"
#include "control_msg.h"
#include "controller.h"
#include "util/log.h"
#include "util/str.h"

#define DEFAULT_PUSH_TARGET "/sdcard/Download/"

// Images from the computer clipboard are pushed to the device Pictures folder
#define IMAGE_PUSH_TARGET "/sdcard/Pictures/scrcpy/"
// --clipboard-image: the server moves the file where the device clipboard can
// share it
#define CLIPBOARD_IMAGE_PUSH_TARGET "/data/local/tmp/"

static void
sc_file_pusher_request_destroy(struct sc_file_pusher_request *req) {
    free(req->file);
}

bool
sc_file_pusher_init(struct sc_file_pusher *fp, struct sc_controller *controller,
                    const char *serial, const char *push_target,
                    bool clipboard_image) {
    assert(controller);
    assert(serial);

    sc_vecdeque_init(&fp->queue);

    bool ok = sc_mutex_init(&fp->mutex);
    if (!ok) {
        return false;
    }

    ok = sc_cond_init(&fp->event_cond);
    if (!ok) {
        sc_mutex_destroy(&fp->mutex);
        return false;
    }

    ok = sc_intr_init(&fp->intr);
    if (!ok) {
        sc_cond_destroy(&fp->event_cond);
        sc_mutex_destroy(&fp->mutex);
        return false;
    }

    fp->serial = strdup(serial);
    if (!fp->serial) {
        LOG_OOM();
        sc_intr_destroy(&fp->intr);
        sc_cond_destroy(&fp->event_cond);
        sc_mutex_destroy(&fp->mutex);
        return false;
    }

    // lazy initialization
    fp->initialized = false;

    fp->stopped = false;

    fp->push_target = push_target ? push_target : DEFAULT_PUSH_TARGET;
    fp->clipboard_image = clipboard_image;
    fp->controller = controller;

    return true;
}

void
sc_file_pusher_destroy(struct sc_file_pusher *fp) {
    sc_cond_destroy(&fp->event_cond);
    sc_mutex_destroy(&fp->mutex);
    sc_intr_destroy(&fp->intr);
    free(fp->serial);

    while (!sc_vecdeque_is_empty(&fp->queue)) {
        struct sc_file_pusher_request *req = sc_vecdeque_popref(&fp->queue);
        assert(req);
        sc_file_pusher_request_destroy(req);
    }
    sc_vecdeque_destroy(&fp->queue);
}

bool
sc_file_pusher_request(struct sc_file_pusher *fp,
                       enum sc_file_pusher_action action, char *file) {
    // start file_pusher if it's used for the first time
    if (!fp->initialized) {
        if (!sc_file_pusher_start(fp)) {
            return false;
        }
        fp->initialized = true;
    }

    LOGI("Request to %s %s", action == SC_FILE_PUSHER_ACTION_INSTALL_APK
                                 ? "install" : "push",
                             file);
    struct sc_file_pusher_request req = {
        .action = action,
        .file = file,
    };

    sc_mutex_lock(&fp->mutex);
    bool was_empty = sc_vecdeque_is_empty(&fp->queue);
    bool res = sc_vecdeque_push(&fp->queue, req);
    if (!res) {
        LOG_OOM();
        sc_mutex_unlock(&fp->mutex);
        return false;
    }

    if (was_empty) {
        sc_cond_signal(&fp->event_cond);
    }
    sc_mutex_unlock(&fp->mutex);

    return true;
}

static bool
request_scan_file(struct sc_file_pusher *fp, const char *path) {
    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_SCAN_FILE;
    msg.scan_file.path = strdup(path);
    if (!msg.scan_file.path) {
        LOG_OOM();
        return false;
    }

    if (!sc_controller_push_msg(fp->controller, &msg)) {
        LOGW("Could not request 'scan file'");
        return false;
    }

    return true;
}

static bool
request_set_clipboard_image(struct sc_file_pusher *fp, const char *file) {
    // The temporary file name (scrcpy-<date>.<ext>) is kept on the device
    const char *name = strrchr(file, '/');
#ifdef _WIN32
    const char *name2 = strrchr(file, '\\');
    if (name2 > name) {
        name = name2;
    }
#endif
    name = name ? name + 1 : file;

    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_SET_CLIPBOARD_IMAGE;
    msg.set_clipboard_image.paste = true;
    msg.set_clipboard_image.path =
        sc_str_concat(CLIPBOARD_IMAGE_PUSH_TARGET, name);
    if (!msg.set_clipboard_image.path) {
        LOG_OOM();
        return false;
    }

    if (!sc_controller_push_msg(fp->controller, &msg)) {
        LOGW("Could not request 'set clipboard image'");
        free(msg.set_clipboard_image.path);
        return false;
    }

    return true;
}

static int
run_file_pusher(void *data) {
    struct sc_file_pusher *fp = data;
    struct sc_intr *intr = &fp->intr;

    const char *serial = fp->serial;
    assert(serial);

    const char *push_target = fp->push_target;
    assert(push_target);

    for (;;) {
        sc_mutex_lock(&fp->mutex);
        while (!fp->stopped && sc_vecdeque_is_empty(&fp->queue)) {
            sc_cond_wait(&fp->event_cond, &fp->mutex);
        }
        if (fp->stopped) {
            // stop immediately, do not process further events
            sc_mutex_unlock(&fp->mutex);
            break;
        }

        assert(!sc_vecdeque_is_empty(&fp->queue));
        struct sc_file_pusher_request req = sc_vecdeque_pop(&fp->queue);
        sc_mutex_unlock(&fp->mutex);

        if (req.action == SC_FILE_PUSHER_ACTION_INSTALL_APK) {
            LOGI("Installing %s...", req.file);
            bool ok = sc_adb_install(intr, serial, req.file, 0);
            if (ok) {
                LOGI("%s successfully installed", req.file);
            } else {
                LOGE("Failed to install %s", req.file);
            }
        } else if (req.action == SC_FILE_PUSHER_ACTION_PUSH_IMAGE
                && fp->clipboard_image) {
            LOGI("Pushing %s...", req.file);
            bool ok = sc_adb_push(intr, serial, req.file,
                                  CLIPBOARD_IMAGE_PUSH_TARGET, 0);
            if (ok) {
                request_set_clipboard_image(fp, req.file); // any error already logged
            } else {
                LOGE("Failed to push %s to %s", req.file,
                     CLIPBOARD_IMAGE_PUSH_TARGET);
            }

            // The image is a temporary file, remove it in any case
            if (!SDL_RemovePath(req.file)) {
                LOGW("Could not remove temporary image file %s: %s",
                     req.file, SDL_GetError());
            }
        } else if (req.action == SC_FILE_PUSHER_ACTION_PUSH_IMAGE) {
            LOGI("Pushing %s...", req.file);
            // Make sure that the target directory exists (adb push does not
            // create it)
            bool ok = sc_adb_shell_mkdir(intr, serial, IMAGE_PUSH_TARGET, 0);
            if (ok) {
                ok = sc_adb_push(intr, serial, req.file, IMAGE_PUSH_TARGET, 0);
                if (ok) {
                    LOGI("%s successfully pushed to %s", req.file,
                         IMAGE_PUSH_TARGET);
                    request_scan_file(fp, IMAGE_PUSH_TARGET); // any error already logged
                } else {
                    LOGE("Failed to push %s to %s", req.file,
                         IMAGE_PUSH_TARGET);
                }
            } else {
                LOGE("Failed to create %s", IMAGE_PUSH_TARGET);
            }

            // The image is a temporary file, remove it in any case
            if (SDL_RemovePath(req.file)) {
                LOGD("Temporary image file %s removed", req.file);
            } else {
                LOGW("Could not remove temporary image file %s: %s",
                     req.file, SDL_GetError());
            }
        } else {
            LOGI("Pushing %s...", req.file);
            bool ok = sc_adb_push(intr, serial, req.file, push_target, 0);
            if (ok) {
                LOGI("%s successfully pushed to %s", req.file, push_target);
                request_scan_file(fp, push_target); // any error already logged
            } else {
                LOGE("Failed to push %s to %s", req.file, push_target);
            }
        }

        sc_file_pusher_request_destroy(&req);
    }
    return 0;
}

bool
sc_file_pusher_start(struct sc_file_pusher *fp) {
    LOGD("Starting file_pusher thread");

    bool ok = sc_thread_create(&fp->thread, run_file_pusher, "scrcpy-file", fp);
    if (!ok) {
        LOGE("Could not start file_pusher thread");
        return false;
    }

    return true;
}

void
sc_file_pusher_stop(struct sc_file_pusher *fp) {
    if (fp->initialized) {
        sc_mutex_lock(&fp->mutex);
        fp->stopped = true;
        sc_cond_signal(&fp->event_cond);
        sc_intr_interrupt(&fp->intr);
        sc_mutex_unlock(&fp->mutex);
    }
}

void
sc_file_pusher_join(struct sc_file_pusher *fp) {
    if (fp->initialized) {
        sc_thread_join(&fp->thread, NULL);
    }
}
