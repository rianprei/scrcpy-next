#include "common.h"

#include <assert.h>
#include <string.h>
#include <SDL3/SDL_keycode.h>

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
        "--autofocus-window",
        "--video-bit-rate", "5M",
        "--crop", "100:200:300:400",
        "--fullscreen",
        "--keyboard-grab", "fullscreen",
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
    assert(opts->autofocus_window);
    assert(opts->video_bit_rate == 5000000);
    assert(!strcmp(opts->crop, "100:200:300:400"));
    assert(opts->fullscreen);
    assert(opts->keyboard_grab);
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

    struct scrcpy_cli_args args6 = {
        .opts = scrcpy_options_default,
    };
    char *argv6[] = {"scrcpy", "--otg-automatic-screenshot"};
    assert(!scrcpy_parse_args(&args6, ARRAY_LEN(argv6), argv6)); // requires --otg

    struct scrcpy_cli_args args7 = {
        .opts = scrcpy_options_default,
    };
    char *argv7[] = {"scrcpy", "--shortcut-key=v=d", "--shortcut-key=PageUp=u"};
    assert(scrcpy_parse_args(&args7, ARRAY_LEN(argv7), argv7));
    assert(args7.opts.shortcut_remaps.count == 2);
    assert(args7.opts.shortcut_remaps.data[0].from == SDLK_V);
    assert(args7.opts.shortcut_remaps.data[0].to == SDLK_D);
    assert(args7.opts.shortcut_remaps.data[1].from == SDLK_PAGEUP);
    assert(args7.opts.shortcut_remaps.data[1].to == SDLK_U);

    char *bad[][2] = {{"scrcpy", "--shortcut-key=v"}, {"scrcpy", "--shortcut-key==d"},
                      {"scrcpy", "--shortcut-key=v=nokey"}};
    for (size_t i = 0; i < ARRAY_LEN(bad); ++i) {
        struct scrcpy_cli_args a = {
            .opts = scrcpy_options_default,
        };
        assert(!scrcpy_parse_args(&a, 2, bad[i]));
    }
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

    struct scrcpy_cli_args args3 = {
        .opts = scrcpy_options_default,
    };
    char *argv3[] = {"scrcpy", "--video-source=camera", "--camera-iso=400",
                     "--camera-exposure=10000", "--camera-ev=-1.5",
                     "--camera-ae-lock", "--camera-awb-lock", "--camera-ois",
                     "--camera-eis"};
    assert(scrcpy_parse_args(&args3, ARRAY_LEN(argv3), argv3));
    assert(args3.opts.camera_iso == 400);
    assert(args3.opts.camera_exposure_us == 10000);
    assert(!strcmp(args3.opts.camera_ev, "-1.5"));
    assert(args3.opts.camera_ae_lock && args3.opts.camera_awb_lock);
    assert(args3.opts.camera_ois && args3.opts.camera_eis);

    struct scrcpy_cli_args args5 = {
        .opts = scrcpy_options_default,
    };
    char *argv5[] = {"scrcpy", "--record=rec-%03d.mkv", "--record-segment=60"};
    assert(scrcpy_parse_args(&args5, ARRAY_LEN(argv5), argv5));
    assert(args5.opts.record_segment == 60);

    // the filename needs an index pattern
    struct scrcpy_cli_args args6 = {
        .opts = scrcpy_options_default,
    };
    char *argv6[] = {"scrcpy", "--record=rec.mkv", "--record-segment=60"};
    assert(!scrcpy_parse_args(&args6, ARRAY_LEN(argv6), argv6));

    // ISO without exposure time
    struct scrcpy_cli_args args4 = {
        .opts = scrcpy_options_default,
    };
    char *argv4[] = {"scrcpy", "--video-source=camera", "--camera-iso=400"};
    assert(!scrcpy_parse_args(&args4, ARRAY_LEN(argv4), argv4));
}

static void test_show_axis(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
    };
    char *argv[] = {"scrcpy", "--show-axis", "--max-size=1024",
                    "--no-control"};
    bool ok = scrcpy_parse_args(&args, ARRAY_LEN(argv), argv);
    assert(ok);
    assert(args.opts.show_axis);
    assert(args.opts.max_size == 1024);
    assert(!args.opts.control);

    struct scrcpy_cli_args invalid = {
        .opts = scrcpy_options_default,
    };
    char *invalid_argv[] = {"scrcpy", "--show-axis", "--crop=100:200:0:0"};
    ok = scrcpy_parse_args(&invalid, ARRAY_LEN(invalid_argv), invalid_argv);
    assert(!ok);
}

static void test_ime_option(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
    };
    char *argv[] = {"scrcpy", "--ime", "--no-cleanup"};

    bool ok = scrcpy_parse_args(&args, ARRAY_LEN(argv), argv);
    assert(ok);
    assert(args.opts.ime);
    assert(!args.opts.cleanup);
    assert(args.opts.keyboard_input_mode == SC_KEYBOARD_INPUT_MODE_SDK);
    assert(args.opts.key_inject_mode == SC_KEY_INJECT_MODE_TEXT);
}

static void test_ime_invalid_options(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
    };
    char *no_control[] = {"scrcpy", "--ime", "--no-control"};
    assert(!scrcpy_parse_args(&args, ARRAY_LEN(no_control), no_control));

    args.opts = scrcpy_options_default;
    char *uhid[] = {"scrcpy", "--ime", "--keyboard=uhid"};
    assert(!scrcpy_parse_args(&args, ARRAY_LEN(uhid), uhid));

    args.opts = scrcpy_options_default;
    char *raw[] = {"scrcpy", "--ime", "--raw-key-events"};
    assert(!scrcpy_parse_args(&args, ARRAY_LEN(raw), raw));

    args.opts = scrcpy_options_default;
    char *secondary_display[] = {"scrcpy", "--ime", "--display-id=1"};
    assert(!scrcpy_parse_args(&args, ARRAY_LEN(secondary_display),
                              secondary_display));

    args.opts = scrcpy_options_default;
    char *new_display[] = {"scrcpy", "--ime", "--new-display=1920x1080"};
    assert(!scrcpy_parse_args(&args, ARRAY_LEN(new_display), new_display));
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

    ok = sc_parse_shortcut_mods(",lctrl", &mods);
    assert(!ok);

    ok = sc_parse_shortcut_mods("lctrl,,rctrl", &mods);
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

static void test_latency_probe(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
    };
    char *argv[] = {"scrcpy", "--latency-probe=0.25,1"};
    assert(scrcpy_parse_args(&args, ARRAY_LEN(argv), argv));
    assert(args.opts.latency_probe);
    assert(args.opts.latency_probe_x == 0.25f);
    assert(args.opts.latency_probe_y == 1.0f);

    struct scrcpy_cli_args args2 = {
        .opts = scrcpy_options_default,
    };
    char *argv2[] = {"scrcpy", "--latency-probe"};
    assert(scrcpy_parse_args(&args2, ARRAY_LEN(argv2), argv2));
    assert(args2.opts.latency_probe_x == 0.5f);

    char *bad[][2] = {
        {"scrcpy", "--latency-probe=0.5"},
        {"scrcpy", "--latency-probe=0.5,1.5"},
        {"scrcpy", "--latency-probe=x,0.5"},
        {"scrcpy", "--latency-probe=0.5,0.5,"},
    };
    for (size_t i = 0; i < ARRAY_LEN(bad); ++i) {
        struct scrcpy_cli_args a = {
            .opts = scrcpy_options_default,
        };
        assert(!scrcpy_parse_args(&a, 2, bad[i]));
    }

    struct scrcpy_cli_args args3 = {
        .opts = scrcpy_options_default,
    };
    char *argv3[] = {"scrcpy", "--latency-probe", "--no-control"};
    assert(!scrcpy_parse_args(&args3, ARRAY_LEN(argv3), argv3));
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
    test_show_axis();
    test_latency_probe();
    test_ime_option();
    test_ime_invalid_options();
    test_parse_shortcut_mods();
    test_preparse();
    test_profile();
    return 0;
}
