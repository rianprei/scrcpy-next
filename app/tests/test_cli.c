#include "common.h"

#include <assert.h>
#include <string.h>

#include "cli.h"
#include "options.h"

static void test_flag_version(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
        .list_client_audio_sources = false,
    };

    char *argv[] = {"scrcpy", "-v"};

    bool ok = scrcpy_parse_args(&args, 2, argv);
    assert(ok);
    assert(!args.help);
    assert(args.version);
}

static void test_flag_help(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
        .list_client_audio_sources = false,
    };

    char *argv[] = {"scrcpy", "-v"};

    bool ok = scrcpy_parse_args(&args, 2, argv);
    assert(ok);
    assert(!args.help);
    assert(args.version);
}

static void test_options(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
        .list_client_audio_sources = false,
    };

    char *argv[] = {
        "scrcpy",
        "--always-on-top",
        "--video-bit-rate", "5M",
        "--crop", "100:200:300:400",
        "--fullscreen",
        "--max-fps", "30",
        "--max-size", "1024",
        // "--no-control" is not compatible with "--turn-screen-off"
        // "--no-playback" is not compatible with "--fulscreen"
        "--port", "1234:1236",
        "--push-target", "/sdcard/Movies",
        "--record", "file",
        "--record-pts", "file.pts.csv",
        "--record-format", "mkv",
        "--serial", "0123456789abcdef",
        "--show-touches",
        "--turn-screen-off",
        "--prefer-text",
        "--window-title", "my device",
        "--window-x", "100",
        "--window-y", "-1",
        "--window-width", "600",
        "--window-height", "0",
        "--window-borderless",
        "--touchscreen-id=abc123",
        "--vsync",
    };

    bool ok = scrcpy_parse_args(&args, ARRAY_LEN(argv), argv);
    assert(ok);

    const struct scrcpy_options *opts = &args.opts;
    assert(opts->always_on_top);
    assert(opts->video_bit_rate == 5000000);
    assert(!strcmp(opts->crop, "100:200:300:400"));
    assert(opts->fullscreen);
    assert(!strcmp(opts->max_fps, "30"));
    assert(opts->max_size == 1024);
    assert(opts->port_range.first == 1234);
    assert(opts->port_range.last == 1236);
    assert(!strcmp(opts->push_target, "/sdcard/Movies"));
    assert(!strcmp(opts->record_filename, "file"));
    assert(!strcmp(opts->record_pts_filename, "file.pts.csv"));
    assert(opts->record_format == SC_RECORD_FORMAT_MKV);
    assert(!strcmp(opts->serial, "0123456789abcdef"));
    assert(opts->show_touches);
    assert(opts->turn_screen_off);
    assert(opts->key_inject_mode == SC_KEY_INJECT_MODE_TEXT);
    assert(!strcmp(opts->window_title, "my device"));
    assert(opts->window_x == 100);
    assert(opts->window_y == -1);
    assert(opts->window_width == 600);
    assert(opts->window_height == 0);
    assert(opts->window_borderless);
    assert(!strcmp(opts->touchscreen_id, "abc123"));
    assert(opts->vsync);
}

static void test_options2(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
        .list_client_audio_sources = false,
    };

    char *argv[] = {
        "scrcpy",
        "--no-control",
        "--no-playback",
        "--record", "file.mp4", // cannot enable --no-playback without recording
    };

    bool ok = scrcpy_parse_args(&args, ARRAY_LEN(argv), argv);
    assert(ok);

    const struct scrcpy_options *opts = &args.opts;
    assert(!opts->control);
    assert(!opts->video_playback);
    assert(!opts->audio_playback);
    assert(!strcmp(opts->record_filename, "file.mp4"));
    assert(opts->record_format == SC_RECORD_FORMAT_MP4);
}

static void test_mouse_bind_info(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
    };
    char *argv[] = {"scrcpy", "--mouse-bind=bhsi"};
    assert(scrcpy_parse_args(&args, ARRAY_LEN(argv), argv));
    assert(args.opts.mouse_bindings.pri.click5 == SC_MOUSE_BINDING_INFO);

    struct scrcpy_cli_args args2 = {
        .opts = scrcpy_options_default,
    };
    char *argv2[] = {"scrcpy", "--video-decoder=libdav1d"};
    assert(scrcpy_parse_args(&args2, ARRAY_LEN(argv2), argv2));
    assert(!strcmp(args2.opts.video_decoder, "libdav1d"));

    struct scrcpy_cli_args args3 = {
        .opts = scrcpy_options_default,
    };
    char *argv3[] = {"scrcpy", "--disconnect-timeout=10", "--no-server-push"};
    assert(scrcpy_parse_args(&args3, ARRAY_LEN(argv3), argv3));
    assert(args3.opts.disconnect_timeout == SC_TICK_FROM_SEC(10));
    assert(!args3.opts.server_push);
    assert(scrcpy_options_default.server_push);

    struct scrcpy_cli_args args4 = {
        .opts = scrcpy_options_default,
    };
    char *argv4[] = {"scrcpy", "--video-codec=mjpeg", "--stream-sink=tcp://127.0.0.1:1234"};
    assert(!scrcpy_parse_args(&args4, ARRAY_LEN(argv4), argv4));

    struct scrcpy_cli_args args5 = {
        .opts = scrcpy_options_default,
    };
    char *argv5[] = {"scrcpy", "--video-codec=mjpeg"};
    assert(scrcpy_parse_args(&args5, ARRAY_LEN(argv5), argv5));
    assert(args5.opts.hwdec_mode == SC_HWDEC_MODE_DISABLED);
}

static void test_camera_focus(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
    };
    char *argv[] = {"scrcpy", "--video-source=camera", "--camera-focus-distance=5",
                    "--no-camera-autofocus"};
    assert(scrcpy_parse_args(&args, ARRAY_LEN(argv), argv));
    assert(!strcmp(args.opts.camera_focus_distance, "5"));
    assert(args.opts.camera_no_autofocus);

    // camera options are rejected for the display source
    struct scrcpy_cli_args args2 = {
        .opts = scrcpy_options_default,
    };
    char *argv2[] = {"scrcpy", "--no-camera-autofocus"};
    assert(!scrcpy_parse_args(&args2, ARRAY_LEN(argv2), argv2));
}

static void test_parse_shortcut_mods(void) {
    uint8_t mods;
    bool ok;

    ok = sc_parse_shortcut_mods("lctrl", &mods);
    assert(ok);
    assert(mods == SC_SHORTCUT_MOD_LCTRL);

    ok = sc_parse_shortcut_mods("rctrl,lalt", &mods);
    assert(ok);
    assert(mods == (SC_SHORTCUT_MOD_RCTRL | SC_SHORTCUT_MOD_LALT));

    ok = sc_parse_shortcut_mods("lsuper,rsuper,lctrl", &mods);
    assert(ok);
    assert(mods == (SC_SHORTCUT_MOD_LSUPER
                  | SC_SHORTCUT_MOD_RSUPER
                  | SC_SHORTCUT_MOD_LCTRL));

    ok = sc_parse_shortcut_mods("", &mods);
    assert(!ok);

    ok = sc_parse_shortcut_mods("lctrl+", &mods);
    assert(!ok);

    ok = sc_parse_shortcut_mods("lctrl,", &mods);
    assert(!ok);
}

static void test_preparse(void) {
    char *argv1[] = {
        "scrcpy",
        "--window-title", "--no-config",
        "--config=one.ini",
        "phone",
    };
    struct scrcpy_cli_preparse preparse;
    bool ok = scrcpy_preparse_args(ARRAY_LEN(argv1), argv1, &preparse);
    assert(ok);
    assert(!strcmp(preparse.config_path, "one.ini"));
    assert(!strcmp(preparse.profile, "phone"));
    assert(!preparse.config_disabled);
    assert(!strcmp(argv1[1], "--window-title"));
    assert(!strcmp(argv1[2], "--no-config"));

    char *argv2[] = {
        "scrcpy",
        "--config=one.ini",
        "--config-file", "two.ini",
        "--no-config",
        "tablet",
    };
    ok = scrcpy_preparse_args(ARRAY_LEN(argv2), argv2, &preparse);
    assert(ok);
    assert(!strcmp(preparse.config_path, "two.ini"));
    assert(!strcmp(preparse.profile, "tablet"));
    assert(preparse.config_disabled);

    char *argv3[] = {"scrcpy", "phone", "tablet"};
    ok = scrcpy_preparse_args(ARRAY_LEN(argv3), argv3, &preparse);
    assert(ok);
    assert(!preparse.profile);
}

static void test_profile(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .profile = "phone",
        .pause_on_exit = SC_PAUSE_ON_EXIT_UNDEFINED,
    };
    char *argv[] = {"scrcpy", "phone", "--max-size=1200"};
    bool ok = scrcpy_parse_args(&args, ARRAY_LEN(argv), argv);
    assert(ok);
    assert(args.opts.max_size == 1200);
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_flag_version();
    test_flag_help();
    test_options();
    test_options2();
    test_camera_focus();
    test_mouse_bind_info();
    test_parse_shortcut_mods();
    test_preparse();
    test_profile();
    return 0;
}
