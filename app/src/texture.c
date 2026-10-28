#include "texture.h"

#include <assert.h>
#include <string.h>

#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>

#include "util/log.h"

bool
sc_texture_init(struct sc_texture *tex, SDL_Renderer *renderer, bool mipmaps,
                enum sc_hwdec_mode hwdec_mode) {
    const char *renderer_name = SDL_GetRendererName(renderer);
    LOGI("Renderer: %s", renderer_name ? renderer_name : "(unknown)");

    tex->has_gl = false;
    tex->mipmaps = false;

    struct sc_opengl *gl = NULL;

    // starts with "opengl"
    bool use_opengl = renderer_name && !strncmp(renderer_name, "opengl", 6);
    if (use_opengl) {
        gl = &tex->gl;
        sc_opengl_init(gl);
        tex->has_gl = true;

        LOGI("OpenGL version: %s", gl->version);

        if (mipmaps) {
            bool supports_mipmaps =
                sc_opengl_version_at_least(gl, 3, 0, /* OpenGL 3.0+ */
                                               2, 0  /* OpenGL ES 2.0+ */);
            if (supports_mipmaps) {
                LOGI("Trilinear filtering enabled");
                tex->mipmaps = true;
            } else {
                LOGW("Trilinear filtering disabled "
                     "(OpenGL 3.0+ or ES 2.0+ required)");
            }
        } else {
            LOGI("Trilinear filtering disabled");
        }
    } else if (mipmaps) {
        LOGD("Trilinear filtering disabled (not an OpenGL renderer)");
    }

    tex->renderer = renderer;
    tex->sws = NULL;
    tex->converted = NULL;
    tex->downloaded = NULL;
    tex->interop = sc_interop_new(hwdec_mode, tex->renderer, gl, tex->mipmaps);
    if (!tex->interop) {
        return false;
    }

    LOGI("Interop: %s", tex->interop->name);

    return true;
}

void
sc_texture_destroy(struct sc_texture *tex) {
    sc_interop_delete(tex->interop);
    sws_freeContext(tex->sws);
    av_frame_free(&tex->converted);
    av_frame_free(&tex->downloaded);
}

SDL_Texture *
sc_texture_get(struct sc_texture *tex) {
    assert(tex->interop);
    return tex->interop->texture;
}

enum AVHWDeviceType
sc_texture_get_hw_type(struct sc_texture *tex) {
    return tex->interop->hw_type;
}

const char *
sc_texture_get_hw_device(struct sc_texture *tex) {
    return tex->interop->hw_device;
}

struct sc_size
sc_texture_get_frame_size(struct sc_texture *tex) {
    assert(tex->interop && tex->interop->texture);
    return tex->interop->frame_size;
}

bool
sc_texture_disable_hwdec(struct sc_texture *tex) {
    assert(tex->interop);
    assert(tex->interop->hw_type != AV_HWDEVICE_TYPE_NONE);

    struct sc_opengl *gl = tex->has_gl ? &tex->gl : NULL;
    struct sc_interop *interop =
        sc_interop_new(SC_HWDEC_MODE_DISABLED, tex->renderer, gl, tex->mipmaps);
    if (!interop) {
        return false;
    }

    LOGD("Interop: %s dropped", tex->interop->name);
    sc_interop_delete(tex->interop);

    tex->interop = interop;

    LOGI("Interop: %s", tex->interop->name);
    return true;
}

static const AVFrame *
sc_texture_convert(struct sc_texture *tex, const AVFrame *frame,
                   enum AVPixelFormat dst_format) {
    if (!tex->sws) {
        const char *name = av_get_pix_fmt_name(frame->format);
        LOGI("Converting %s frames to %s", name ? name : "(unknown)",
             av_get_pix_fmt_name(dst_format));
    }

    tex->sws = sws_getCachedContext(tex->sws, frame->width, frame->height,
                                    frame->format, frame->width, frame->height,
                                    dst_format, SWS_BILINEAR, NULL, NULL, NULL);
    if (!tex->sws) {
        LOGE("Could not initialize the frame conversion");
        return NULL;
    }

    AVFrame *dst = tex->converted;
    if (!dst || dst->width != frame->width || dst->height != frame->height) {
        av_frame_free(&tex->converted);
        dst = av_frame_alloc();
        if (!dst) {
            LOG_OOM();
            return NULL;
        }
        dst->format = dst_format;
        dst->width = frame->width;
        dst->height = frame->height;
        if (av_frame_get_buffer(dst, 0) < 0) {
            av_frame_free(&dst);
            LOG_OOM();
            return NULL;
        }
        tex->converted = dst;
    }

    sws_scale(tex->sws, (const uint8_t *const *) frame->data, frame->linesize,
              0, frame->height, dst->data, dst->linesize);

    if (av_frame_copy_props(dst, frame) < 0) {
        LOG_OOM();
        return NULL;
    }
    return dst;
}

static const AVFrame *
sc_texture_download(struct sc_texture *tex, const AVFrame *frame) {
    if (!tex->downloaded) {
        tex->downloaded = av_frame_alloc();
        if (!tex->downloaded) {
            LOG_OOM();
            return NULL;
        }
    }

    AVFrame *dst = tex->downloaded;
    av_frame_unref(dst);
    if (av_hwframe_transfer_data(dst, frame, 0) < 0
            || av_frame_copy_props(dst, frame) < 0) {
        LOGE("Could not download the hardware frame");
        return NULL;
    }
    return dst;
}

bool
sc_texture_update(struct sc_texture *tex, const AVFrame *frame) {
    assert(tex->interop);

    if (frame->hw_frames_ctx
            && tex->interop->hw_type == AV_HWDEVICE_TYPE_NONE) {
        // The hardware interop failed (see below): render from memory
        frame = sc_texture_download(tex, frame);
        if (!frame) {
            return false;
        }
    }

    enum AVPixelFormat format = frame->format;
    if (format == AV_PIX_FMT_YUVJ420P) {
        // Deprecated full-range alias of YUV420P, with the same layout (the
        // color range is read from the frame)
        format = AV_PIX_FMT_YUV420P;
    }

    if (tex->interop->hw_type != AV_HWDEVICE_TYPE_NONE
            && tex->interop->pix_fmt != format) {
        LOGI("Incompatible frame, switching to software interop");
        if (!sc_texture_disable_hwdec(tex)) {
            return false;
        }
    }

    struct sc_interop *interop = tex->interop;

    if (interop->pix_fmt != format) {
        if (frame->hw_frames_ctx) {
            const char *name = av_get_pix_fmt_name(format);
            LOGE("Unsupported frames format: %s", name ? name : "(unknown)");
            return false;
        }
        frame = sc_texture_convert(tex, frame, interop->pix_fmt);
        if (!frame) {
            return false;
        }
    }

    assert(interop->ops && interop->ops->import);
    bool ok = interop->ops->import(interop, frame);
    if (!ok && frame->hw_frames_ctx) {
        // For example, the DMA-BUF cannot be imported as an EGLImage (no
        // DRI on the X server): keep the hardware decoder, but download the
        // frames instead of showing nothing
        LOGW("Could not import the hardware frame, rendering from memory");
        if (!sc_texture_disable_hwdec(tex)) {
            return false;
        }
        return sc_texture_update(tex, frame);
    }
    return ok;
}

void
sc_texture_reset(struct sc_texture *tex) {
    struct sc_interop *interop = tex->interop;
    if (interop) {
        assert(interop->ops && interop->ops->reset);
        interop->ops->reset(interop);
    }
}
