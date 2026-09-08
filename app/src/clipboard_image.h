#ifndef SC_CLIPBOARD_IMAGE_H
#define SC_CLIPBOARD_IMAGE_H

#include "common.h"

#include <stdbool.h>

struct sc_file_pusher;

/**
 * If the computer clipboard contains an image (and no text), save it to a
 * temporary file and request the file pusher to push it to the device
 * Pictures folder (/sdcard/Pictures/scrcpy/).
 *
 * If the clipboard contains text, then the normal text paste is expected, so
 * this function returns false.
 *
 * Return true if the clipboard contained an image (even if it could not be
 * saved or pushed), false if the clipboard contained no image.
 */
bool
sc_clipboard_image_paste(struct sc_file_pusher *fp);

#endif
