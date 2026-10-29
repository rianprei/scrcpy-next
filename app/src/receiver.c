#include "receiver.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_clipboard.h>

#include "device_msg.h"
#include "events.h"
#include "util/log.h"
#include "util/str.h"
#include "util/thread.h"

struct sc_uhid_output_task_data {
    struct sc_uhid_devices *uhid_devices;
    uint16_t id;
    uint16_t size;
    uint8_t *data;
};

bool
sc_receiver_init(struct sc_receiver *receiver, sc_socket control_socket,
                 const struct sc_receiver_callbacks *cbs, void *cbs_userdata) {
    bool ok = sc_mutex_init(&receiver->mutex);
    if (!ok) {
        return false;
    }

    receiver->control_socket = control_socket;
    receiver->acksync = NULL;
    receiver->uhid_devices = NULL;
    receiver->media_listener = NULL;

    assert(cbs && cbs->on_ended);
    receiver->cbs = cbs;
    receiver->cbs_userdata = cbs_userdata;

    return true;
}

void
sc_receiver_destroy(struct sc_receiver *receiver) {
    sc_mutex_destroy(&receiver->mutex);
}

static void
task_set_clipboard(void *userdata) {
    assert(sc_thread_is_main());

    char *text = userdata;

    char *current = SDL_GetClipboardText();
    bool same = current && !strcmp(current, text);
    SDL_free(current);
    if (same) {
        LOGD("Computer clipboard unchanged");
    } else {
        bool ok = SDL_SetClipboardText(text);
        if (ok) {
            LOGI("Device clipboard copied");
            // The content is only logged on explicit request (-Vverbose)
            LOGV("Device clipboard content: \"%s\"", text);
        } else {
            LOGE("Could not set clipboard: %s", SDL_GetError());
        }
    }

    free(text);
}

struct sc_clipboard_image {
    char *mime_type;
    uint8_t *data;
    uint32_t size;
};

static const void *
clipboard_image_get_data(void *userdata, const char *mime_type, size_t *size) {
    struct sc_clipboard_image *image = userdata;
    if (strcmp(mime_type, image->mime_type)) {
        *size = 0;
        return NULL;
    }
    *size = image->size;
    return image->data;
}

static void
clipboard_image_cleanup(void *userdata) {
    struct sc_clipboard_image *image = userdata;
    free(image->mime_type);
    free(image->data);
    free(image);
}

static void
task_set_clipboard_image(void *userdata) {
    assert(sc_thread_is_main());

    struct sc_clipboard_image *image = userdata;

    // The image now belongs to SDL, even on error: it calls the cleanup
    // callback once the data is replaced or cleared (not before this function
    // returns on success)
    const char *mime_types[] = {image->mime_type};
    bool ok = SDL_SetClipboardData(clipboard_image_get_data,
                                   clipboard_image_cleanup, image, mime_types,
                                   1);
    if (ok) {
        LOGI("Device clipboard image copied (%s, %" PRIu32 " bytes)",
             image->mime_type, image->size);
    } else {
        LOGE("Could not set clipboard image: %s", SDL_GetError());
    }
}

static void
task_uhid_output(void *userdata) {
    assert(sc_thread_is_main());

    struct sc_uhid_output_task_data *data = userdata;

    sc_uhid_devices_process_hid_output(data->uhid_devices, data->id, data->data,
                                       data->size);

    free(data->data);
    free(data);
}

static void
process_msg(struct sc_receiver *receiver, struct sc_device_msg *msg) {
    switch (msg->type) {
        case DEVICE_MSG_TYPE_CLIPBOARD: {
            // Take ownership of the text (do not destroy the msg)
            char *text = msg->clipboard.text;

            bool ok = sc_run_on_main_thread(task_set_clipboard, text, false);
            if (!ok) {
                LOGW("Could not post clipboard to main thread");
                free(text);
                return;
            }

            break;
        }
        case DEVICE_MSG_TYPE_CLIPBOARD_IMAGE: {
            struct sc_clipboard_image *image = malloc(sizeof(*image));
            if (!image) {
                LOG_OOM();
                sc_device_msg_destroy(msg);
                return;
            }
            // Take ownership of the data (do not destroy the msg)
            image->mime_type = msg->clipboard_image.mime_type;
            image->data = msg->clipboard_image.data;
            image->size = msg->clipboard_image.size;

            bool ok = sc_run_on_main_thread(task_set_clipboard_image, image,
                                            false);
            if (!ok) {
                LOGW("Could not post clipboard image to main thread");
                clipboard_image_cleanup(image);
                return;
            }

            break;
        }
        case DEVICE_MSG_TYPE_ACK_CLIPBOARD:
            LOGD("Ack device clipboard sequence=%" PRIu64_,
                 msg->ack_clipboard.sequence);

            // This is a programming error to receive this message if there is
            // no ACK synchronization mechanism
            assert(receiver->acksync);

            // Also check at runtime (do not trust the server)
            if (!receiver->acksync) {
                LOGE("Received unexpected ack");
                return;
            }

            sc_acksync_ack(receiver->acksync, msg->ack_clipboard.sequence);
            // No allocation to free in the msg
            break;
        case DEVICE_MSG_TYPE_UHID_OUTPUT:
            if (sc_get_log_level() <= SC_LOG_LEVEL_VERBOSE) {
                char *hex = sc_str_to_hex_string(msg->uhid_output.data,
                                                 msg->uhid_output.size);
                if (hex) {
                    LOGV("UHID output [%" PRIu16 "] %s",
                         msg->uhid_output.id, hex);
                    free(hex);
                } else {
                    LOGV("UHID output [%" PRIu16 "] size=%" PRIu16,
                         msg->uhid_output.id, msg->uhid_output.size);
                }
            }

            if (!receiver->uhid_devices) {
                LOGE("Received unexpected HID output message");
                sc_device_msg_destroy(msg);
                return;
            }

            struct sc_uhid_output_task_data *data = malloc(sizeof(*data));
            if (!data) {
                LOG_OOM();
                return;
            }

            // It is guaranteed that these pointers will still be valid when
            // the main thread will process them (the main thread will stop
            // processing on exit, when everything gets deinitialized)
            data->uhid_devices = receiver->uhid_devices;
            data->id = msg->uhid_output.id;
            data->data = msg->uhid_output.data; // take ownership
            data->size = msg->uhid_output.size;

            bool ok = sc_run_on_main_thread(task_uhid_output, data, false);
            if (!ok) {
                LOGW("Could not post UHID output to main thread");
                free(data->data);
                free(data);
                return;
            }

            break;
        case DEVICE_MSG_TYPE_MEDIA_UPDATE: {
            struct sc_media_listener *listener = receiver->media_listener;
            if (!listener) {
                LOGE("Received unexpected media update message");
                sc_device_msg_destroy(msg);
                return;
            }

            // The listener takes ownership of the strings (do not destroy
            // the msg)
            listener->ops->on_update(listener, msg->media_update.state,
                                     msg->media_update.position_ms,
                                     msg->media_update.capabilities,
                                     msg->media_update.duration_ms,
                                     msg->media_update.title,
                                     msg->media_update.artist,
                                     msg->media_update.album);
            break;
        }
    }
}

static ssize_t
process_msgs(struct sc_receiver *receiver, const uint8_t *buf, size_t len) {
    size_t head = 0;
    for (;;) {
        struct sc_device_msg msg;
        ssize_t r = sc_device_msg_deserialize(&buf[head], len - head, &msg);
        if (r == -1) {
            return -1;
        }
        if (r == 0) {
            return head;
        }

        process_msg(receiver, &msg);
        // the device msg must be destroyed by process_msg()

        head += r;
        assert(head <= len);
        if (head == len) {
            return head;
        }
    }
}

static int
run_receiver(void *data) {
    struct sc_receiver *receiver = data;

    // The buffer grows to receive a larger message (a clipboard image, with
    // --clipboard-image), then shrinks back
    size_t cap = DEVICE_MSG_MAX_SIZE;
    uint8_t *buf = malloc(cap);
    if (!buf) {
        LOG_OOM();
        receiver->cbs->on_ended(receiver, true, receiver->cbs_userdata);
        return 0;
    }
    size_t head = 0;

    bool error = false;

    for (;;) {
        if (head == cap) {
            // The buffer is full with an incomplete message
            uint8_t *grown = cap <= SIZE_MAX / 2 ? realloc(buf, cap * 2) : NULL;
            if (!grown) {
                LOG_OOM();
                error = true;
                break;
            }
            buf = grown;
            cap *= 2;
        }
        assert(head < cap);
        ssize_t r = net_recv(receiver->control_socket, buf + head, cap - head);
        if (r <= 0) {
            LOGD("Receiver stopped");
            // device disconnected: keep error=false
            break;
        }

        head += r;
        ssize_t consumed = process_msgs(receiver, buf, head);
        if (consumed == -1) {
            // an error occurred
            error = true;
            break;
        }

        if (consumed) {
            head -= consumed;
            // shift the remaining data in the buffer
            memmove(buf, &buf[consumed], head);

            if (cap > DEVICE_MSG_MAX_SIZE && head < DEVICE_MSG_MAX_SIZE) {
                uint8_t *shrunk = realloc(buf, DEVICE_MSG_MAX_SIZE);
                if (shrunk) {
                    // On failure, just keep the larger buffer
                    buf = shrunk;
                    cap = DEVICE_MSG_MAX_SIZE;
                }
            }
        }
    }

    free(buf);
    receiver->cbs->on_ended(receiver, error, receiver->cbs_userdata);

    return 0;
}

bool
sc_receiver_start(struct sc_receiver *receiver) {
    LOGD("Starting receiver thread");

    bool ok = sc_thread_create(&receiver->thread, run_receiver,
                               "scrcpy-receiver", receiver);
    if (!ok) {
        LOGE("Could not start receiver thread");
        return false;
    }

    return true;
}

void
sc_receiver_join(struct sc_receiver *receiver) {
    sc_thread_join(&receiver->thread, NULL);
}
