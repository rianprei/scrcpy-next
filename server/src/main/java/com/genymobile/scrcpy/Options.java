package com.genymobile.scrcpy;

import com.genymobile.scrcpy.audio.AudioCodec;
import com.genymobile.scrcpy.audio.AudioSource;
import com.genymobile.scrcpy.device.Device;
import com.genymobile.scrcpy.model.CodecOption;
import com.genymobile.scrcpy.model.NewDisplay;
import com.genymobile.scrcpy.model.Orientation;
import com.genymobile.scrcpy.model.Size;
import com.genymobile.scrcpy.util.Ln;
import com.genymobile.scrcpy.video.CameraAspectRatio;
import com.genymobile.scrcpy.video.CameraFacing;
import com.genymobile.scrcpy.video.VideoCodec;
import com.genymobile.scrcpy.video.VideoSource;
import com.genymobile.scrcpy.wrappers.WindowManager;

import android.graphics.Rect;
import android.util.Pair;

import java.util.List;
import java.util.Locale;

public class Options {

    private Ln.Level logLevel = Ln.Level.DEBUG;
    private int scid = -1; // 31-bit non-negative value, or -1
    private boolean video = true;
    private boolean audio = true;
    private boolean clientAudio;
    private int maxSize;
    private int minSizeAlignment = 1;
    private VideoCodec videoCodec = VideoCodec.H264;
    private AudioCodec audioCodec = AudioCodec.OPUS;
    private VideoSource videoSource = VideoSource.DISPLAY;
    private AudioSource audioSource = AudioSource.OUTPUT;
    private boolean audioDup;
    private String[] audioMatchPackages = new String[0];
    private int videoBitRate = 8000000;
    private int audioBitRate = 128000;
    private float maxFps;
    private float angle;
    private boolean tunnelForward;
    private Rect crop;
    private boolean control = true;
    private int displayId;
    private String touchscreenId;
    private String cameraId;
    private Size cameraSize;
    private CameraFacing cameraFacing;
    private CameraAspectRatio cameraAspectRatio;
    private float cameraZoom = 1;
    private int cameraFps;
    private boolean cameraHighSpeed;
    private boolean cameraTorch;
    private boolean cameraNoAutofocus;
    private float cameraFocusDistance = Float.NaN;
    private int cameraIso; // 0 for auto
    private long cameraExposureUs; // 0 for auto
    private float cameraEv = Float.NaN;
    private boolean cameraAeLock;
    private boolean cameraAwbLock;
    private boolean cameraOis;
    private boolean cameraEis;
    private boolean showTouches;
    private boolean stayAwake;
    private int screenOffTimeout = -1;
    private int displayImePolicy = -1;
    private List<CodecOption> videoCodecOptions;
    private List<CodecOption> audioCodecOptions;

    private String videoEncoder;
    private String audioEncoder;
    private boolean powerOffScreenOnClose;
    private boolean clipboardAutosync = true;
    private boolean mediaControl;
    private boolean downsizeOnError = true;
    private boolean cleanup = true;
    private boolean keepServer;
    private boolean powerOn = true;
    private boolean latencyStats;

    private NewDisplay newDisplay;
    private boolean vdDestroyContent = true;
    private boolean vdSystemDecorations = true;
    private boolean flexDisplay;

    private boolean keepActive;
    private boolean ignoreVideoEncoderConstraints;

    private Orientation.Lock captureOrientationLock = Orientation.Lock.Unlocked;
    private Orientation captureOrientation = Orientation.Orient0;

    private boolean listEncoders;
    private boolean listDisplays;
    private boolean listCameras;
    private boolean listCameraSizes;
    private boolean listApps;

    // Options not used by the scrcpy client, but useful to use scrcpy-server directly
    private boolean sendDeviceMeta = true; // send device name and size
    private boolean sendFrameMeta = true; // send PTS so that the client may record properly
    private boolean sendDummyByte = true; // write a byte on start to detect connection issues
    private boolean sendStreamMeta = true; // write the stream metadata (codec and session)

    public Ln.Level getLogLevel() {
        return logLevel;
    }

    public int getScid() {
        return scid;
    }

    public boolean getVideo() {
        return video;
    }

    public boolean getAudio() {
        return audio;
    }

    public boolean getClientAudio() {
        return clientAudio;
    }

    public int getMaxSize() {
        return maxSize;
    }

    public int getMinSizeAlignment() {
        return minSizeAlignment;
    }

    public VideoCodec getVideoCodec() {
        return videoCodec;
    }

    public AudioCodec getAudioCodec() {
        return audioCodec;
    }

    public VideoSource getVideoSource() {
        return videoSource;
    }

    public AudioSource getAudioSource() {
        return audioSource;
    }

    public boolean getAudioDup() {
        return audioDup;
    }

    public String[] getAudioMatchPackages() {
        return audioMatchPackages;
    }

    public int getVideoBitRate() {
        return videoBitRate;
    }

    public int getAudioBitRate() {
        return audioBitRate;
    }

    public float getMaxFps() {
        return maxFps;
    }

    public float getAngle() {
        return angle;
    }

    public boolean isTunnelForward() {
        return tunnelForward;
    }

    public Rect getCrop() {
        return crop;
    }

    public boolean getControl() {
        return control;
    }

    public int getDisplayId() {
        return displayId;
    }

    public String getTouchscreenId() {
        return touchscreenId;
    }

    public String getCameraId() {
        return cameraId;
    }

    public Size getCameraSize() {
        return cameraSize;
    }

    public CameraFacing getCameraFacing() {
        return cameraFacing;
    }

    public CameraAspectRatio getCameraAspectRatio() {
        return cameraAspectRatio;
    }

    public float getCameraZoom() {
        return cameraZoom;
    }

    public int getCameraFps() {
        return cameraFps;
    }

    public boolean getCameraHighSpeed() {
        return cameraHighSpeed;
    }

    public boolean getCameraTorch() {
        return cameraTorch;
    }

    public boolean getCameraNoAutofocus() {
        return cameraNoAutofocus;
    }

    public float getCameraFocusDistance() {
        return cameraFocusDistance;
    }

    public int getCameraIso() {
        return cameraIso;
    }

    public long getCameraExposureUs() {
        return cameraExposureUs;
    }

    public float getCameraEv() {
        return cameraEv;
    }

    public boolean getCameraAeLock() {
        return cameraAeLock;
    }

    public boolean getCameraAwbLock() {
        return cameraAwbLock;
    }

    public boolean getCameraOis() {
        return cameraOis;
    }

    public boolean getCameraEis() {
        return cameraEis;
    }

    public boolean getShowTouches() {
        return showTouches;
    }

    public boolean getStayAwake() {
        return stayAwake;
    }

    public int getScreenOffTimeout() {
        return screenOffTimeout;
    }

    public int getDisplayImePolicy() {
        return displayImePolicy;
    }

    public List<CodecOption> getVideoCodecOptions() {
        return videoCodecOptions;
    }

    public List<CodecOption> getAudioCodecOptions() {
        return audioCodecOptions;
    }

    public String getVideoEncoder() {
        return videoEncoder;
    }

    public String getAudioEncoder() {
        return audioEncoder;
    }

    public boolean getPowerOffScreenOnClose() {
        return this.powerOffScreenOnClose;
    }

    public boolean getClipboardAutosync() {
        return clipboardAutosync;
    }

    public boolean getMediaControl() {
        return mediaControl;
    }

    public boolean getDownsizeOnError() {
        return downsizeOnError;
    }

    public boolean getCleanup() {
        return cleanup;
    }

    public boolean getKeepServer() {
        return keepServer;
    }

    public boolean getPowerOn() {
        return powerOn;
    }

    public boolean getLatencyStats() {
        return latencyStats;
    }

    public NewDisplay getNewDisplay() {
        return newDisplay;
    }

    public Orientation getCaptureOrientation() {
        return captureOrientation;
    }

    public Orientation.Lock getCaptureOrientationLock() {
        return captureOrientationLock;
    }

    public boolean getVDDestroyContent() {
        return vdDestroyContent;
    }

    public boolean getVDSystemDecorations() {
        return vdSystemDecorations;
    }

    public boolean getKeepActive() {
        return keepActive;
    }

    public boolean getFlexDisplay() {
        return flexDisplay;
    }

    public boolean getIgnoreVideoEncoderConstraints() {
        return ignoreVideoEncoderConstraints;
    }

    public boolean getList() {
        return listEncoders || listDisplays || listCameras || listCameraSizes || listApps;
    }

    public boolean getListEncoders() {
        return listEncoders;
    }

    public boolean getListDisplays() {
        return listDisplays;
    }

    public boolean getListCameras() {
        return listCameras;
    }

    public boolean getListCameraSizes() {
        return listCameraSizes;
    }

    public boolean getListApps() {
        return listApps;
    }

    public boolean getSendDeviceMeta() {
        return sendDeviceMeta;
    }

    public boolean getSendFrameMeta() {
        return sendFrameMeta;
    }

    public boolean getSendDummyByte() {
        return sendDummyByte;
    }

    public boolean getSendStreamMeta() {
        return sendStreamMeta;
    }

    @SuppressWarnings("MethodLength")
    public static Options parse(String... args) {
        if (args.length < 1) {
            throw new IllegalArgumentException("Missing client version");
        }

        String clientVersion = args[0];
        if (!clientVersion.equals(BuildConfig.VERSION_NAME)) {
            throw new IllegalArgumentException(
                    "The server version (" + BuildConfig.VERSION_NAME + ") does not match the client " + "(" + clientVersion + ")");
        }

        Options options = new Options();

        for (int i = 1; i < args.length; ++i) {
            String arg = args[i];
            int equalIndex = arg.indexOf('=');
            if (equalIndex == -1) {
                throw new IllegalArgumentException("Invalid key=value pair: \"" + arg + "\"");
            }
            String key = arg.substring(0, equalIndex);
            String value = arg.substring(equalIndex + 1);
            switch (key) {
                case "scid":
                    int scid = Integer.parseInt(value, 0x10);
                    if (scid < -1) {
                        throw new IllegalArgumentException("scid may not be negative (except -1 for 'none'): " + scid);
                    }
                    options.scid = scid;
                    break;
                case "log_level":
                    options.logLevel = Ln.Level.valueOf(value.toUpperCase(Locale.ENGLISH));
                    break;
                case "video":
                    options.video = Boolean.parseBoolean(value);
                    break;
                case "audio":
                    options.audio = Boolean.parseBoolean(value);
                    break;
                case "client_audio":
                    options.clientAudio = Boolean.parseBoolean(value);
                    break;
                case "video_codec":
                    VideoCodec videoCodec = VideoCodec.findByName(value);
                    if (videoCodec == null) {
                        throw new IllegalArgumentException("Video codec " + value + " not supported");
                    }
                    options.videoCodec = videoCodec;
                    break;
                case "audio_codec":
                    AudioCodec audioCodec = AudioCodec.findByName(value);
                    if (audioCodec == null) {
                        throw new IllegalArgumentException("Audio codec " + value + " not supported");
                    }
                    options.audioCodec = audioCodec;
                    break;
                case "video_source":
                    VideoSource videoSource = VideoSource.findByName(value);
                    if (videoSource == null) {
                        throw new IllegalArgumentException("Video source " + value + " not supported");
                    }
                    options.videoSource = videoSource;
                    break;
                case "audio_source":
                    AudioSource audioSource = AudioSource.findByName(value);
                    if (audioSource == null) {
                        throw new IllegalArgumentException("Audio source " + value + " not supported");
                    }
                    options.audioSource = audioSource;
                    break;
                case "audio_dup":
                    options.audioDup = Boolean.parseBoolean(value);
                    break;
                case "audio_match_packages":
                    options.audioMatchPackages = value.split(",");
                    break;
                case "max_size":
                    options.maxSize = Integer.parseInt(value);
                    break;
                case "min_size_alignment":
                    int align = Integer.parseInt(value);
                    if (align < 1 || align > 16 || (align & (align - 1)) != 0) {
                        throw new IllegalArgumentException("min_size_alignment (" + align + ") must be 1, 2, 4, 8 or 16");
                    }
                    options.minSizeAlignment = align;
                    break;
                case "video_bit_rate":
                    options.videoBitRate = Integer.parseInt(value);
                    break;
                case "audio_bit_rate":
                    options.audioBitRate = Integer.parseInt(value);
                    break;
                case "max_fps":
                    options.maxFps = parseFloat("max_fps", value);
                    break;
                case "angle":
                    options.angle = parseFloat("angle", value);
                    break;
                case "tunnel_forward":
                    options.tunnelForward = Boolean.parseBoolean(value);
                    break;
                case "crop":
                    if (!value.isEmpty()) {
                        options.crop = parseCrop(value);
                    }
                    break;
                case "control":
                    options.control = Boolean.parseBoolean(value);
                    break;
                case "display_id":
                    options.displayId = Integer.parseInt(value);
                    break;
                case "touchscreen_id":
                    if (!value.isEmpty()) {
                        options.touchscreenId = value;
                    }
                    break;
                case "show_touches":
                    options.showTouches = Boolean.parseBoolean(value);
                    break;
                case "stay_awake":
                    options.stayAwake = Boolean.parseBoolean(value);
                    break;
                case "screen_off_timeout":
                    options.screenOffTimeout = Integer.parseInt(value);
                    if (options.screenOffTimeout < -1) {
                        throw new IllegalArgumentException("Invalid screen off timeout: " + options.screenOffTimeout);
                    }
                    break;
                case "video_codec_options":
                    options.videoCodecOptions = CodecOption.parse(value);
                    break;
                case "audio_codec_options":
                    options.audioCodecOptions = CodecOption.parse(value);
                    break;
                case "video_encoder":
                    if (!value.isEmpty()) {
                        options.videoEncoder = value;
                    }
                    break;
                case "audio_encoder":
                    if (!value.isEmpty()) {
                        options.audioEncoder = value;
                    }
                    break;
                case "power_off_on_close":
                    options.powerOffScreenOnClose = Boolean.parseBoolean(value);
                    break;
                case "clipboard_autosync":
                    options.clipboardAutosync = Boolean.parseBoolean(value);
                    break;
                case "media_control":
                    options.mediaControl = Boolean.parseBoolean(value);
                    break;
                case "downsize_on_error":
                    options.downsizeOnError = Boolean.parseBoolean(value);
                    break;
                case "cleanup":
                    options.cleanup = Boolean.parseBoolean(value);
                    break;
                case "keep_server":
                    options.keepServer = Boolean.parseBoolean(value);
                    break;
                case "power_on":
                    options.powerOn = Boolean.parseBoolean(value);
                    break;
                case "latency_stats":
                    options.latencyStats = Boolean.parseBoolean(value);
                    break;
                case "list_encoders":
                    options.listEncoders = Boolean.parseBoolean(value);
                    break;
                case "list_displays":
                    options.listDisplays = Boolean.parseBoolean(value);
                    break;
                case "list_cameras":
                    options.listCameras = Boolean.parseBoolean(value);
                    break;
                case "list_camera_sizes":
                    options.listCameraSizes = Boolean.parseBoolean(value);
                    break;
                case "list_apps":
                    options.listApps = Boolean.parseBoolean(value);
                    break;
                case "camera_id":
                    if (!value.isEmpty()) {
                        options.cameraId = value;
                    }
                    break;
                case "camera_size":
                    if (!value.isEmpty()) {
                        options.cameraSize = parseSize(value);
                    }
                    break;
                case "camera_facing":
                    if (!value.isEmpty()) {
                        CameraFacing facing = CameraFacing.findByName(value);
                        if (facing == null) {
                            throw new IllegalArgumentException("Camera facing " + value + " not supported");
                        }
                        options.cameraFacing = facing;
                    }
                    break;
                case "camera_ar":
                    if (!value.isEmpty()) {
                        options.cameraAspectRatio = parseCameraAspectRatio(value);
                    }
                    break;
                case "camera_zoom":
                    if (!value.isEmpty()) {
                        options.cameraZoom = Float.parseFloat(value);
                    }
                    break;
                case "camera_focus_distance":
                    options.cameraFocusDistance = parseFloat("camera_focus_distance", value);
                    break;
                case "camera_iso":
                    options.cameraIso = Integer.parseInt(value);
                    break;
                case "camera_exposure_us":
                    options.cameraExposureUs = Long.parseLong(value);
                    break;
                case "camera_ev":
                    options.cameraEv = parseFloat("camera_ev", value);
                    break;
                case "camera_ae_lock":
                    options.cameraAeLock = Boolean.parseBoolean(value);
                    break;
                case "camera_awb_lock":
                    options.cameraAwbLock = Boolean.parseBoolean(value);
                    break;
                case "camera_ois":
                    options.cameraOis = Boolean.parseBoolean(value);
                    break;
                case "camera_eis":
                    options.cameraEis = Boolean.parseBoolean(value);
                    break;
                case "camera_fps":
                    options.cameraFps = Integer.parseInt(value);
                    break;
                case "camera_high_speed":
                    options.cameraHighSpeed = Boolean.parseBoolean(value);
                    break;
                case "camera_torch":
                    options.cameraTorch = Boolean.parseBoolean(value);
                    break;
                case "camera_no_autofocus":
                    options.cameraNoAutofocus = Boolean.parseBoolean(value);
                    break;
                case "new_display":
                    options.newDisplay = parseNewDisplay(value);
                    break;
                case "vd_destroy_content":
                    options.vdDestroyContent = Boolean.parseBoolean(value);
                    break;
                case "vd_system_decorations":
                    options.vdSystemDecorations = Boolean.parseBoolean(value);
                    break;
                case "flex_display":
                    options.flexDisplay = Boolean.parseBoolean(value);
                    break;
                case "capture_orientation":
                    Pair<Orientation.Lock, Orientation> pair = parseCaptureOrientation(value);
                    options.captureOrientationLock = pair.first;
                    options.captureOrientation = pair.second;
                    break;
                case "display_ime_policy":
                    options.displayImePolicy = parseDisplayImePolicy(value);
                    break;
                case "keep_active":
                    options.keepActive = Boolean.parseBoolean(value);
                    break;
                case "ignore_video_encoder_constraints":
                    options.ignoreVideoEncoderConstraints = Boolean.parseBoolean(value);
                    break;
                case "send_device_meta":
                    options.sendDeviceMeta = Boolean.parseBoolean(value);
                    break;
                case "send_frame_meta":
                    options.sendFrameMeta = Boolean.parseBoolean(value);
                    break;
                case "send_dummy_byte":
                    options.sendDummyByte = Boolean.parseBoolean(value);
                    break;
                case "send_stream_meta":
                    options.sendStreamMeta = Boolean.parseBoolean(value);
                    break;
                case "raw_stream":
                    boolean rawStream = Boolean.parseBoolean(value);
                    if (rawStream) {
                        options.sendDeviceMeta = false;
                        options.sendFrameMeta = false;
                        options.sendDummyByte = false;
                        options.sendStreamMeta = false;
                    }
                    break;
                default:
                    Ln.w("Unknown server option: " + key);
                    break;
            }
        }

        if (options.newDisplay != null) {
            assert options.displayId == 0 : "Must not set both displayId and newDisplay";
            options.displayId = Device.DISPLAY_ID_NONE;
        }

        if (options.clientAudio
                && (options.audioSource == AudioSource.VOICE_CALL
                || options.audioSource == AudioSource.VOICE_CALL_UPLINK)) {
            throw new IllegalArgumentException("client_audio is incompatible with audio_source=voice-call and audio_source=voice-call-uplink");
        }

        return options;
    }

    private static Rect parseCrop(String crop) {
        // input format: "width:height:x:y"
        String[] tokens = crop.split(":");
        if (tokens.length != 4) {
            throw new IllegalArgumentException("Crop must contains 4 values separated by colons: \"" + crop + "\"");
        }
        int width = Integer.parseInt(tokens[0]);
        int height = Integer.parseInt(tokens[1]);
        if (width <= 0 || height <= 0) {
            throw new IllegalArgumentException("Invalid crop size: " + width + "x" + height);
        }
        int x = Integer.parseInt(tokens[2]);
        int y = Integer.parseInt(tokens[3]);
        if (x < 0 || y < 0) {
            throw new IllegalArgumentException("Invalid crop offset: " + x + ":" + y);
        }
        return new Rect(x, y, x + width, y + height);
    }

    private static Size parseSize(String size) {
        // input format: "<width>x<height>"
        String[] tokens = size.split("x");
        if (tokens.length != 2) {
            throw new IllegalArgumentException("Invalid size format (expected <width>x<height>): \"" + size + "\"");
        }
        int width = Integer.parseInt(tokens[0]);
        int height = Integer.parseInt(tokens[1]);
        if (width <= 0 || height <= 0) {
            throw new IllegalArgumentException("Invalid non-positive size dimension: \"" + size + "\"");
        }
        return new Size(width, height);
    }

    private static CameraAspectRatio parseCameraAspectRatio(String ar) {
        if ("sensor".equals(ar)) {
            return CameraAspectRatio.sensorAspectRatio();
        }

        String[] tokens = ar.split(":");
        if (tokens.length == 2) {
            int w = Integer.parseInt(tokens[0]);
            int h = Integer.parseInt(tokens[1]);
            return CameraAspectRatio.fromFraction(w, h);
        }

        float floatAr = Float.parseFloat(tokens[0]);
        return CameraAspectRatio.fromFloat(floatAr);
    }

    private static float parseFloat(String key, String value) {
        try {
            return Float.parseFloat(value);
        } catch (NumberFormatException e) {
            throw new IllegalArgumentException("Invalid float value for " + key + ": \"" + value + "\"");
        }
    }

    static NewDisplay parseNewDisplay(String newDisplay) {
        // Input in the form "[<width>x<height>][/<dpi>][@<fps>]" (each [] block is optional)
        // For convenience, the order of dpi and fps does not matter.
        if (newDisplay.isEmpty()) {
            return new NewDisplay();
        }

        String sizeString = null;
        String dpiString = null;
        String fpsString = null;

        String s = newDisplay;
        while (true) {
            int slashIndex = s.indexOf('/');
            int atIndex = s.indexOf('@');
            int lastSepIndex = Math.max(slashIndex, atIndex);
            if (lastSepIndex == -1) {
                if (!s.isEmpty()) {
                    sizeString = s;
                }
                break;
            } else {
                char lastSep = newDisplay.charAt(lastSepIndex);
                if (lastSep == '@') {
                    if (fpsString != null) {
                        throw new IllegalArgumentException("Invalid new display format: '@' may not appear twice");
                    }
                    fpsString = s.substring(lastSepIndex + 1);
                } else {
                    assert lastSep == '/';
                    if (dpiString != null) {
                        throw new IllegalArgumentException("Invalid new display format: '/' may not appear twice");
                    }
                    dpiString = s.substring(lastSepIndex + 1);
                }
                s = s.substring(0, lastSepIndex);
            }
        }

        Size size = null;
        int dpi = 0;
        float fps = 0;

        if (sizeString != null) {
            size = parseSize(sizeString);
        }

        if (dpiString != null) {
            dpi = Integer.parseInt(dpiString);
            if (dpi <= 0) {
                throw new IllegalArgumentException("Invalid non-positive dpi: " + dpiString);
            }
        }

        if (fpsString != null) {
            fps = Float.parseFloat(fpsString);
            if (fps < 0) {
                throw new IllegalArgumentException("Invalid negative fps: " + fpsString);
            }
        }

        return new NewDisplay(size, dpi, fps);
    }

    private static Pair<Orientation.Lock, Orientation> parseCaptureOrientation(String value) {
        if (value.isEmpty()) {
            throw new IllegalArgumentException("Empty capture orientation string");
        }

        Orientation.Lock lock;
        if (value.charAt(0) == '@') {
            // Consume '@'
            value = value.substring(1);
            if (value.isEmpty()) {
                // Only '@': lock to the initial orientation (orientation is unused)
                return Pair.create(Orientation.Lock.LockedInitial, Orientation.Orient0);
            }
            lock = Orientation.Lock.LockedValue;
        } else {
            lock = Orientation.Lock.Unlocked;
        }

        return Pair.create(lock, Orientation.getByName(value));
    }

    private static int parseDisplayImePolicy(String value) {
        switch (value) {
            case "local":
                return WindowManager.DISPLAY_IME_POLICY_LOCAL;
            case "fallback":
                return WindowManager.DISPLAY_IME_POLICY_FALLBACK_DISPLAY;
            case "hide":
                return WindowManager.DISPLAY_IME_POLICY_HIDE;
            default:
                throw new IllegalArgumentException("Invalid display IME policy: " + value);
        }
    }
}
