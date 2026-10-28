#ifndef SC_SCREENSHOT_H
#define SC_SCREENSHOT_H

#include "common.h"

#include <stdbool.h>
#include <libavutil/frame.h>

#include "options.h"

// Save the current frame as a PNG screenshot.
// The frame may be a software frame (any format supported by swscale) or a
// hardware frame (downloaded first).
// The orientation is applied to rotate/flip the output to match the display.
bool
sc_screenshot_save(const AVFrame *frame, enum sc_orientation orientation);

#endif
