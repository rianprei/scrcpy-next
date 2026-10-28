#include "common.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>

#include "screenshot.h"

static unsigned
count_png(const char *dir) {
    DIR *d = opendir(dir);
    assert(d);
    unsigned n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        n += strstr(e->d_name, ".png") != NULL;
    }
    closedir(d);
    return n;
}

static void
remove_png(const char *dir) {
    DIR *d = opendir(dir);
    assert(d);
    struct dirent *e;
    char path[600];
    while ((e = readdir(d))) {
        if (strstr(e->d_name, ".png")) {
            snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
            unlink(path);
        }
    }
    closedir(d);
}

// Decode the only PNG in dir, return its size and the RGB of its center pixel
static void
decode_png(const char *dir, int *w, int *h, uint8_t rgb[3]) {
    DIR *d = opendir(dir);
    assert(d);
    char path[600] = "";
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strstr(e->d_name, ".png")) {
            snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        }
    }
    closedir(d);
    assert(*path);

    FILE *f = fopen(path, "rb");
    assert(f);
    static uint8_t buf[1 << 20];
    size_t len = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    assert(len > 0);

    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_PNG);
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    assert(ctx && avcodec_open2(ctx, codec, NULL) == 0);
    AVPacket *pkt = av_packet_alloc();
    pkt->data = buf;
    pkt->size = len;
    AVFrame *frame = av_frame_alloc();
    assert(avcodec_send_packet(ctx, pkt) == 0);
    assert(avcodec_receive_frame(ctx, frame) == 0);
    assert(frame->format == AV_PIX_FMT_RGB24);
    *w = frame->width;
    *h = frame->height;
    const uint8_t *p = frame->data[0] + (frame->height / 2) * frame->linesize[0]
                     + (frame->width / 2) * 3;
    memcpy(rgb, p, 3);
    unlink(path);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
}

static AVFrame *
red_frame(int w, int h) {
    AVFrame *frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = w;
    frame->height = h;
    assert(av_frame_get_buffer(frame, 0) == 0);
    // pure red in BT.601 limited range: Y=81 U=90 V=240
    for (int y = 0; y < h; ++y) {
        memset(frame->data[0] + y * frame->linesize[0], 81, w);
    }
    for (int y = 0; y < h / 2; ++y) {
        memset(frame->data[1] + y * frame->linesize[1], 90, w / 2);
        memset(frame->data[2] + y * frame->linesize[2], 240, w / 2);
    }
    return frame;
}

static void
test_screenshot_saved_in_downloads(void) {
    char home[] = "/tmp/scrcpy-test-shot-XXXXXX";
    assert(mkdtemp(home));
    char downloads[600];
    snprintf(downloads, sizeof(downloads), "%s/Downloads", home);
    assert(mkdir(downloads, 0700) == 0);
    setenv("HOME", home, 1);

    AVFrame *frame = red_frame(64, 32);

    assert(sc_screenshot_save(frame, SC_ORIENTATION_0));
    int w, h;
    uint8_t rgb[3];
    decode_png(downloads, &w, &h, rgb);
    assert(w == 64 && h == 32);
    assert(rgb[0] > 200 && rgb[1] < 40 && rgb[2] < 40); // red

    // 90 degrees: width and height are swapped
    assert(sc_screenshot_save(frame, SC_ORIENTATION_90));
    decode_png(downloads, &w, &h, rgb);
    assert(w == 32 && h == 64);
    assert(rgb[0] > 200 && rgb[1] < 40 && rgb[2] < 40);

    assert(!sc_screenshot_save(NULL, SC_ORIENTATION_0));

    // two screenshots in the same second: two files
    assert(sc_screenshot_save(frame, SC_ORIENTATION_0));
    assert(sc_screenshot_save(frame, SC_ORIENTATION_0));
    assert(count_png(downloads) == 2);
    remove_png(downloads);

    // hardware frame (VA-API), as produced by the hardware decoder
    AVBufferRef *device = NULL;
    if (av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VAAPI, NULL, NULL, 0)) {
        printf("VA-API not available: hardware frame path skipped\n");
    } else {
        AVBufferRef *frames_ref = av_hwframe_ctx_alloc(device);
        AVHWFramesContext *fc = (AVHWFramesContext *) frames_ref->data;
        fc->format = AV_PIX_FMT_VAAPI;
        fc->sw_format = AV_PIX_FMT_NV12;
        fc->width = 64;
        fc->height = 32;
        assert(av_hwframe_ctx_init(frames_ref) == 0);

        AVFrame *nv12 = av_frame_alloc();
        nv12->format = AV_PIX_FMT_NV12;
        nv12->width = 64;
        nv12->height = 32;
        assert(av_frame_get_buffer(nv12, 0) == 0);
        for (int y = 0; y < 32; ++y) {
            memset(nv12->data[0] + y * nv12->linesize[0], 81, 64);
        }
        for (int y = 0; y < 16; ++y) {
            uint8_t *uv = nv12->data[1] + y * nv12->linesize[1];
            for (int x = 0; x < 32; ++x) {
                uv[2 * x] = 90;
                uv[2 * x + 1] = 240;
            }
        }

        AVFrame *hw = av_frame_alloc();
        assert(av_hwframe_get_buffer(frames_ref, hw, 0) == 0);
        assert(av_hwframe_transfer_data(hw, nv12, 0) == 0);
        assert(!hw->data[0]); // the content is in a GPU surface

        assert(sc_screenshot_save(hw, SC_ORIENTATION_0));
        decode_png(downloads, &w, &h, rgb);
        assert(w == 64 && h == 32);
        assert(rgb[0] > 200 && rgb[1] < 40 && rgb[2] < 40);
        printf("VA-API hardware frame path verified\n");

        av_frame_free(&hw);
        av_frame_free(&nv12);
        av_buffer_unref(&frames_ref);
        av_buffer_unref(&device);
    }

    av_frame_free(&frame);
    rmdir(downloads);
    rmdir(home);
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_screenshot_saved_in_downloads();
    return 0;
}
