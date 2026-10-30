package com.genymobile.scrcpy.video;

import com.genymobile.scrcpy.AndroidVersions;
import com.genymobile.scrcpy.FakeContext;
import com.genymobile.scrcpy.Options;
import com.genymobile.scrcpy.model.ConfigurationException;
import com.genymobile.scrcpy.model.Orientation;
import com.genymobile.scrcpy.model.Size;
import com.genymobile.scrcpy.opengl.AffineOpenGLFilter;
import com.genymobile.scrcpy.opengl.OpenGLFilter;
import com.genymobile.scrcpy.opengl.OpenGLRunner;
import com.genymobile.scrcpy.util.AffineMatrix;
import com.genymobile.scrcpy.util.HandlerExecutor;
import com.genymobile.scrcpy.util.Ln;
import com.genymobile.scrcpy.util.LogUtils;
import com.genymobile.scrcpy.wrappers.ServiceManager;

import android.annotation.SuppressLint;
import android.annotation.TargetApi;
import android.content.Context;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraConstrainedHighSpeedCaptureSession;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CameraMetadata;
import android.hardware.camera2.CaptureFailure;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.params.OutputConfiguration;
import android.hardware.camera2.params.SessionConfiguration;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.MediaCodec;
import android.os.Binder;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.util.Range;
import android.util.Rational;
import android.view.Surface;
import android.view.View;
import android.view.WindowManager;

import java.io.IOException;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Stream;

public class CameraCapture extends SurfaceCapture {

    public static final float[] VFLIP_MATRIX = {
            1, 0, 0, 0, // column 1
            0, -1, 0, 0, // column 2
            0, 0, 1, 0, // column 3
            0, 1, 0, 1, // column 4
    };

    private static final float ZOOM_FACTOR = 1 + 1 / 16f;

    private final String explicitCameraId;
    private final CameraFacing cameraFacing;
    private final Size explicitSize;
    private final CameraAspectRatio aspectRatio;
    private final int fps;
    private final boolean highSpeed;
    private final Rect crop;
    private final Orientation captureOrientation;
    private final float angle;
    private final boolean initialTorch;
    private final boolean noAutofocus;
    private final float focusDistance;
    private final int iso;
    private final long exposureUs;
    private final float ev;
    private final boolean aeLock;
    private final boolean awbLock;
    private final boolean ois;
    private final boolean eis;
    private float zoom;
    // --camera-pip: the camera is drawn over the screen capture, at its capture size (no filter)
    private final boolean overlay;

    private VideoConstraints videoConstraints;

    private String cameraId; // the camera to open (a logical camera if physicalCameraId is set)
    private String physicalCameraId; // optional physical camera of the logical camera cameraId
    private Size captureSize;
    private Size videoSize; // after OpenGL transforms
    private Range<Float> zoomRange;
    private Float minFocusDistance; // in diopters, 0 or null for a fixed-focus lens
    private float manualFocus = Float.NaN; // current manual focus distance, NaN in autofocus

    private AffineMatrix transform;
    private OpenGLRunner glRunner;

    private HandlerThread cameraThread;
    private Handler cameraHandler;
    private CameraDevice cameraDevice;
    private Executor cameraExecutor;

    private final AtomicBoolean disconnected = new AtomicBoolean();

    // The following fields must be accessed only from the camera thread
    private boolean started;
    private CaptureRequest.Builder requestBuilder;
    private CameraCaptureSession currentSession;

    public CameraCapture(Options options) {
        this(options, false);
    }

    public CameraCapture(Options options, boolean overlay) {
        this.overlay = overlay;
        this.explicitCameraId = options.getCameraId();
        this.cameraFacing = options.getCameraFacing();
        this.explicitSize = options.getCameraSize();
        this.aspectRatio = options.getCameraAspectRatio();
        this.fps = options.getCameraFps();
        this.highSpeed = options.getCameraHighSpeed();
        // In overlay mode, the crop, orientation and angle options apply to the screen
        this.crop = overlay ? null : options.getCrop();
        this.captureOrientation = overlay ? Orientation.Orient0 : options.getCaptureOrientation();
        assert captureOrientation != null;
        this.angle = overlay ? 0 : options.getAngle();
        this.initialTorch = options.getCameraTorch();
        this.noAutofocus = options.getCameraNoAutofocus();
        this.focusDistance = options.getCameraFocusDistance();
        this.iso = options.getCameraIso();
        this.exposureUs = options.getCameraExposureUs();
        this.ev = options.getCameraEv();
        this.aeLock = options.getCameraAeLock();
        this.awbLock = options.getCameraAwbLock();
        this.ois = options.getCameraOis();
        this.eis = options.getCameraEis();
        this.zoom = options.getCameraZoom();
    }

    @Override
    protected void init(VideoConstraints videoConstraints) throws ConfigurationException, IOException {
        this.videoConstraints = videoConstraints;

        cameraThread = new HandlerThread("camera");
        cameraThread.start();
        cameraHandler = new Handler(cameraThread.getLooper());
        cameraExecutor = new HandlerExecutor(cameraHandler);

        try {
            String[] ids = selectCamera(explicitCameraId, cameraFacing);
            if (ids == null) {
                Ln.e("No matching camera found");
                throw new ConfigurationException("No matching camera found");
            }
            cameraId = ids[0];
            physicalCameraId = ids[1];

            if (physicalCameraId != null) {
                Ln.i("Using physical camera '" + physicalCameraId + "' of logical camera '" + cameraId + "'");
            } else {
                Ln.i("Using camera '" + cameraId + "'");
            }
            int attempts = 1;
            if (Build.VERSION.SDK_INT == AndroidVersions.API_30_ANDROID_11) {
                // On Android 11, the camera service only serves clients it considers foreground;
                // an invisible overlay window makes the shell process count as one on many (not
                // all) ROMs. The camera may take a few tries to open while the window settles.
                prepareOverlayWindow();
                attempts = 4;
            }
            while (true) {
                try {
                    cameraDevice = openCamera(cameraId);
                    break;
                } catch (CameraAccessException e) {
                    if (--attempts == 0) {
                        throw e;
                    }
                    Thread.sleep(200);
                }
            }
        } catch (CameraAccessException | InterruptedException e) {
            throw new IOException(e);
        }
    }

    @Override
    public void prepare() throws IOException {
        try {
            int maxSize = videoConstraints.getMaxSize();
            // A physical camera may not support all the sizes of its logical camera
            captureSize = selectSize(getLensCameraId(), explicitSize, maxSize, aspectRatio, highSpeed);
            if (captureSize == null) {
                throw new IOException("Could not select camera size");
            }
        } catch (CameraAccessException e) {
            throw new IOException(e);
        }

        VideoFilter filter = new VideoFilter(captureSize);

        if (crop != null) {
            filter.addCrop(crop, false);
        }

        if (captureOrientation != Orientation.Orient0) {
            filter.addOrientation(captureOrientation);
        }

        filter.addAngle(angle);

        transform = filter.getInverseTransform();
        if (overlay) {
            // Rendered on an overlay texture, whatever its size
            assert transform == null;
            videoSize = captureSize;
            return;
        }
        videoSize = filter.getOutputSize().align(videoConstraints.getAlignment());

        if (!videoSize.equals(captureSize) && transform == null) {
            // The camera stream can only be rendered on a surface matching the input size.
            // If the video constraints change the size, an intermediate OpenGL filter is required.
            transform = AffineMatrix.IDENTITY;
        }
    }

    /**
     * Return the orientation to apply to the camera image to show it like a camera preview (upright, and mirrored for a front camera) on a
     * display with the given rotation (Display.getRotation()), see android.hardware.Camera.setDisplayOrientation().
     */
    public Orientation getPreviewOrientation(int displayRotation) throws IOException {
        CameraCharacteristics characteristics;
        try {
            characteristics = ServiceManager.getCameraManager().getCameraCharacteristics(getLensCameraId());
        } catch (CameraAccessException e) {
            throw new IOException(e);
        }
        Integer sensorOrientation = characteristics.get(CameraCharacteristics.SENSOR_ORIENTATION);
        Integer facing = characteristics.get(CameraCharacteristics.LENS_FACING);
        int sensor = sensorOrientation != null ? sensorOrientation : 0;
        int degrees = displayRotation * 90;
        if (facing != null && facing == CameraCharacteristics.LENS_FACING_FRONT) {
            // Flipping before a rotation by -θ is the same as flipping after a rotation by θ
            int cw = (360 - (sensor + degrees) % 360) % 360;
            return Orientation.values()[4 + cw / 90]; // flip<cw>
        }
        int cw = (sensor - degrees + 360) % 360;
        return Orientation.values()[cw / 90];
    }

    /**
     * The camera whose lens characteristics apply (the physical camera, if any).
     */
    private String getLensCameraId() {
        return physicalCameraId != null ? physicalCameraId : cameraId;
    }

    /**
     * Return {cameraId, physicalCameraId}: an explicit id may be a physical camera of a logical multi-camera, which
     * must be opened through its logical camera. Return null if no camera matches.
     */
    private static String[] selectCamera(String explicitCameraId, CameraFacing cameraFacing) throws CameraAccessException, ConfigurationException {
        CameraManager cameraManager = ServiceManager.getCameraManager();

        String[] cameraIds = cameraManager.getCameraIdList();
        if (explicitCameraId != null) {
            if (Arrays.asList(cameraIds).contains(explicitCameraId)) {
                return new String[] {explicitCameraId, null};
            }
            if (Build.VERSION.SDK_INT >= AndroidVersions.API_28_ANDROID_9) {
                for (String cameraId : cameraIds) {
                    CameraCharacteristics characteristics = cameraManager.getCameraCharacteristics(cameraId);
                    if (characteristics.getPhysicalCameraIds().contains(explicitCameraId)) {
                        return new String[] {cameraId, explicitCameraId};
                    }
                }
            }
            Ln.e("Camera with id " + explicitCameraId + " not found\n" + LogUtils.buildCameraListMessage(false));
            throw new ConfigurationException("Camera id not found");
        }

        if (cameraFacing == null) {
            // Use the first one
            return cameraIds.length > 0 ? new String[] {cameraIds[0], null} : null;
        }

        for (String cameraId : cameraIds) {
            CameraCharacteristics characteristics = cameraManager.getCameraCharacteristics(cameraId);

            int facing = characteristics.get(CameraCharacteristics.LENS_FACING);
            if (cameraFacing.value() == facing) {
                return new String[] {cameraId, null};
            }
        }

        // Not found
        return null;
    }

    @TargetApi(AndroidVersions.API_24_ANDROID_7_0)
    private static Size selectSize(String cameraId, Size explicitSize, int maxSize, CameraAspectRatio aspectRatio, boolean highSpeed)
            throws CameraAccessException {
        if (explicitSize != null) {
            return explicitSize;
        }

        CameraManager cameraManager = ServiceManager.getCameraManager();
        CameraCharacteristics characteristics = cameraManager.getCameraCharacteristics(cameraId);

        StreamConfigurationMap configs = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
        android.util.Size[] sizes = highSpeed ? configs.getHighSpeedVideoSizes() : configs.getOutputSizes(MediaCodec.class);
        if (sizes == null) {
            return null;
        }

        Stream<android.util.Size> stream = Arrays.stream(sizes);
        if (maxSize > 0) {
            stream = stream.filter(it -> it.getWidth() <= maxSize && it.getHeight() <= maxSize);
        }

        Float targetAspectRatio = resolveAspectRatio(aspectRatio, characteristics);
        if (targetAspectRatio != null) {
            stream = stream.filter(it -> {
                float ar = ((float) it.getWidth() / it.getHeight());
                float arRatio = ar / targetAspectRatio;
                // Accept if the aspect ratio is the target aspect ratio + or - 10%
                return arRatio >= 0.9f && arRatio <= 1.1f;
            });
        }

        Optional<android.util.Size> selected = stream.max((s1, s2) -> {
            // Greater width is better
            int cmp = Integer.compare(s1.getWidth(), s2.getWidth());
            if (cmp != 0) {
                return cmp;
            }

            if (targetAspectRatio != null) {
                // Closer to the target aspect ratio is better
                float ar1 = ((float) s1.getWidth() / s1.getHeight());
                float arRatio1 = ar1 / targetAspectRatio;
                float distance1 = Math.abs(1 - arRatio1);

                float ar2 = ((float) s2.getWidth() / s2.getHeight());
                float arRatio2 = ar2 / targetAspectRatio;
                float distance2 = Math.abs(1 - arRatio2);

                // Reverse the order because lower distance is better
                cmp = Float.compare(distance2, distance1);
                if (cmp != 0) {
                    return cmp;
                }
            }

            // Greater height is better
            return Integer.compare(s1.getHeight(), s2.getHeight());
        });

        if (selected.isPresent()) {
            android.util.Size size = selected.get();
            return new Size(size.getWidth(), size.getHeight());
        }

        // Not found
        return null;
    }

    private static Float resolveAspectRatio(CameraAspectRatio ratio, CameraCharacteristics characteristics) {
        if (ratio == null) {
            return null;
        }

        if (ratio.isSensor()) {
            Rect activeSize = characteristics.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE);
            return (float) activeSize.width() / activeSize.height();
        }

        return ratio.getAspectRatio();
    }

    @TargetApi(AndroidVersions.API_30_ANDROID_11)
    @Override
    public void start(Surface surface) throws IOException {
        if (transform != null) {
            assert glRunner == null;
            OpenGLFilter glFilter = new AffineOpenGLFilter(transform);
            // The transform matrix returned by SurfaceTexture is incorrect for camera capture (it often contains an additional unexpected 90°
            // rotation). Use a vertical flip transform matrix instead.
            glRunner = new OpenGLRunner(glFilter, VFLIP_MATRIX);
            surface = glRunner.start(captureSize, videoSize, surface);
        }

        cameraHandler.post(() -> {
            assertCameraThread();
            started = true;
        });

        Surface captureSurface = surface;
        OutputConfiguration outputConfig = new OutputConfiguration(captureSurface);
        if (physicalCameraId != null) {
            outputConfig.setPhysicalCameraId(physicalCameraId);
        }
        List<OutputConfiguration> outputs = Collections.singletonList(outputConfig);
        int sessionType = highSpeed ? SessionConfiguration.SESSION_HIGH_SPEED : SessionConfiguration.SESSION_REGULAR;
        SessionConfiguration sessionConfig = new SessionConfiguration(sessionType, outputs, cameraExecutor, new CameraCaptureSession.StateCallback() {
            @Override
            public void onConfigured(CameraCaptureSession session) {
                assertCameraThread();
                if (!started) {
                    // Stopped on the encoder thread between the call to start() and this callback
                    return;
                }

                CameraManager cameraManager = ServiceManager.getCameraManager();
                try {
                    CameraCharacteristics characteristics = cameraManager.getCameraCharacteristics(cameraId);
                    zoomRange = characteristics.get(CameraCharacteristics.CONTROL_ZOOM_RATIO_RANGE);
                    CameraCharacteristics lens = physicalCameraId != null ? cameraManager.getCameraCharacteristics(physicalCameraId) : characteristics;
                    minFocusDistance = lens.get(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE);
                } catch (CameraAccessException e) {
                    Ln.w("Could not get camera characteristics");
                }

                try {
                    requestBuilder = cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_RECORD);
                    requestBuilder.addTarget(captureSurface);

                    if (fps > 0) {
                        requestBuilder.set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, new Range<>(fps, fps));
                    }
                    if (initialTorch) {
                        Ln.i("Turn camera torch on");
                        requestBuilder.set(CaptureRequest.FLASH_MODE, CaptureRequest.FLASH_MODE_TORCH);
                    }
                    if (zoom != 1) {
                        zoom = clampZoom(zoom);
                        Ln.i("Set camera zoom: " + zoom);
                        requestBuilder.set(CaptureRequest.CONTROL_ZOOM_RATIO, zoom);
                    }
                    if (noAutofocus || !Float.isNaN(focusDistance)) {
                        configureManualFocus(requestBuilder);
                    }
                    configureExposureAndStabilization(requestBuilder);

                    CaptureRequest request = requestBuilder.build();
                    setRepeatingRequest(session, request);
                    currentSession = session;
                } catch (CameraAccessException e) {
                    Ln.e("Camera error", e);
                    disconnected.set(true);
                    getCaptureControl().reset(CaptureControl.RESET_REASON_TERMINATED);
                }
            }

            @Override
            public void onConfigureFailed(CameraCaptureSession session) {
                Ln.e("Camera configuration error");
                disconnected.set(true);
                getCaptureControl().reset(CaptureControl.RESET_REASON_TERMINATED);
            }
        });

        try {
            cameraDevice.createCaptureSession(sessionConfig);
        } catch (CameraAccessException e) {
            stop();
            throw new IOException(e);
        }
    }

    @Override
    public void stop() {
        cameraHandler.post(() -> {
            assertCameraThread();
            if (currentSession != null) {
                try {
                    currentSession.stopRepeating();
                } catch (CameraAccessException | IllegalStateException e) {
                    // The session may already be closed (for example if the camera has been disconnected)
                    Ln.d("Could not stop repeating capture request: " + e.getMessage());
                }
            }
            currentSession = null;
            requestBuilder = null;
            started = false;
        });

        if (glRunner != null) {
            glRunner.stopAndRelease();
            glRunner = null;
        }
    }

    @Override
    public void release() {
        if (cameraDevice != null) {
            cameraDevice.close();
        }
        if (cameraThread != null) {
            cameraThread.quitSafely();
        }
    }

    @Override
    public Size getSize() {
        return videoSize;
    }

    @Override
    protected boolean applyNewVideoConstraints(VideoConstraints videoConstraints) {
        if (explicitSize != null) {
            return false;
        }

        this.videoConstraints = videoConstraints;
        return true;
    }

    @SuppressLint({"PrivateApi", "SoonBlockedPrivateApi"})
    @TargetApi(AndroidVersions.API_30_ANDROID_11)
    private static void prepareOverlayWindow() {
        try {
            WindowManager wm = (WindowManager) FakeContext.get().getSystemService(Context.WINDOW_SERVICE);
            // WindowManagerImpl rejects windows without a parent token; give it a default one
            wm.getClass().getMethod("setDefaultToken", IBinder.class).invoke(wm, new Binder());
            int type = WindowManager.LayoutParams.class.getField("TYPE_SECURE_SYSTEM_OVERLAY").getInt(null);
            WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                    WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.WRAP_CONTENT,
                    type,
                    WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
                    PixelFormat.TRANSLUCENT);
            wm.addView(new View(FakeContext.get()), params);
        } catch (ReflectiveOperationException | RuntimeException e) {
            // ROM-dependent (see <https://github.com/Genymobile/scrcpy/pull/4213>); opening the
            // camera below will most likely fail, but let it report its own error
            Ln.w("Could not create foreground overlay window: " + e);
        }
    }

    @SuppressLint("MissingPermission")
    @TargetApi(AndroidVersions.API_31_ANDROID_12)
    private CameraDevice openCamera(String id) throws CameraAccessException, InterruptedException {
        CompletableFuture<CameraDevice> future = new CompletableFuture<>();
        ServiceManager.getCameraManager().openCamera(id, new CameraDevice.StateCallback() {
            @Override
            public void onOpened(CameraDevice camera) {
                Ln.d("Camera opened successfully");
                future.complete(camera);
            }

            @Override
            public void onDisconnected(CameraDevice camera) {
                Ln.w("Camera disconnected");
                disconnected.set(true);
                getCaptureControl().reset(CaptureControl.RESET_REASON_TERMINATED);
            }

            @Override
            public void onError(CameraDevice camera, int error) {
                int cameraAccessExceptionErrorCode;
                switch (error) {
                    case CameraDevice.StateCallback.ERROR_CAMERA_IN_USE:
                        cameraAccessExceptionErrorCode = CameraAccessException.CAMERA_IN_USE;
                        break;
                    case CameraDevice.StateCallback.ERROR_MAX_CAMERAS_IN_USE:
                        cameraAccessExceptionErrorCode = CameraAccessException.MAX_CAMERAS_IN_USE;
                        break;
                    case CameraDevice.StateCallback.ERROR_CAMERA_DISABLED:
                        cameraAccessExceptionErrorCode = CameraAccessException.CAMERA_DISABLED;
                        break;
                    case CameraDevice.StateCallback.ERROR_CAMERA_DEVICE:
                    case CameraDevice.StateCallback.ERROR_CAMERA_SERVICE:
                    default:
                        cameraAccessExceptionErrorCode = CameraAccessException.CAMERA_ERROR;
                        break;
                }
                future.completeExceptionally(new CameraAccessException(cameraAccessExceptionErrorCode));
            }
        }, cameraHandler);

        try {
            return future.get();
        } catch (ExecutionException e) {
            throw (CameraAccessException) e.getCause();
        }
    }

    private static boolean contains(int[] values, int value) {
        if (values != null) {
            for (int v : values) {
                if (v == value) {
                    return true;
                }
            }
        }
        return false;
    }

    // Camera mirroring requires Android 11+ (see Server)
    @TargetApi(AndroidVersions.API_30_ANDROID_11)
    private void configureExposureAndStabilization(CaptureRequest.Builder requestBuilder) throws CameraAccessException {
        if (iso == 0 && Float.isNaN(ev) && !aeLock && !awbLock && !ois && !eis) {
            return;
        }
        if (highSpeed) {
            // Constrained high-speed sessions only accept a restricted set of capture keys
            Ln.w("Manual camera controls are not supported in high-speed capture mode; ignoring");
            return;
        }

        CameraCharacteristics characteristics = ServiceManager.getCameraManager().getCameraCharacteristics(getLensCameraId());

        if (iso != 0) {
            int[] capabilities = characteristics.get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES);
            Range<Integer> isoRange = characteristics.get(CameraCharacteristics.SENSOR_INFO_SENSITIVITY_RANGE);
            Range<Long> exposureRange = characteristics.get(CameraCharacteristics.SENSOR_INFO_EXPOSURE_TIME_RANGE);
            if (!contains(capabilities, CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR) || isoRange == null
                    || exposureRange == null) {
                Ln.w("Camera '" + cameraId + "' does not support manual sensor control; ignoring --camera-iso/--camera-exposure");
            } else {
                int clampedIso = isoRange.clamp(iso);
                long exposureNs = exposureRange.clamp(exposureUs * 1000);
                requestBuilder.set(CaptureRequest.CONTROL_AE_MODE, CameraMetadata.CONTROL_AE_MODE_OFF);
                requestBuilder.set(CaptureRequest.SENSOR_SENSITIVITY, clampedIso);
                requestBuilder.set(CaptureRequest.SENSOR_EXPOSURE_TIME, exposureNs);
                Ln.i("Camera exposure: ISO " + clampedIso + ", " + (exposureNs / 1000) + " us");
            }
        }

        if (!Float.isNaN(ev)) {
            Range<Integer> evRange = characteristics.get(CameraCharacteristics.CONTROL_AE_COMPENSATION_RANGE);
            Rational evStep = characteristics.get(CameraCharacteristics.CONTROL_AE_COMPENSATION_STEP);
            if (evRange == null || evStep == null || evStep.floatValue() == 0 || evRange.getUpper() == 0) {
                Ln.w("Camera '" + cameraId + "' does not support exposure compensation; ignoring --camera-ev");
            } else {
                int index = evRange.clamp(Math.round(ev / evStep.floatValue()));
                requestBuilder.set(CaptureRequest.CONTROL_AE_EXPOSURE_COMPENSATION, index);
                Ln.i("Camera exposure compensation: " + (index * evStep.floatValue()) + " EV");
            }
        }

        if (aeLock) {
            if (Boolean.TRUE.equals(characteristics.get(CameraCharacteristics.CONTROL_AE_LOCK_AVAILABLE))) {
                requestBuilder.set(CaptureRequest.CONTROL_AE_LOCK, true);
                Ln.i("Camera auto exposure locked");
            } else {
                Ln.w("Camera '" + cameraId + "' does not support AE lock; ignoring --camera-ae-lock");
            }
        }

        if (awbLock) {
            if (Boolean.TRUE.equals(characteristics.get(CameraCharacteristics.CONTROL_AWB_LOCK_AVAILABLE))) {
                requestBuilder.set(CaptureRequest.CONTROL_AWB_LOCK, true);
                Ln.i("Camera auto white balance locked");
            } else {
                Ln.w("Camera '" + cameraId + "' does not support AWB lock; ignoring --camera-awb-lock");
            }
        }

        if (ois) {
            int[] modes = characteristics.get(CameraCharacteristics.LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION);
            if (contains(modes, CameraMetadata.LENS_OPTICAL_STABILIZATION_MODE_ON)) {
                requestBuilder.set(CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE, CameraMetadata.LENS_OPTICAL_STABILIZATION_MODE_ON);
                Ln.i("Camera optical stabilization enabled");
            } else {
                Ln.w("Camera '" + cameraId + "' does not support optical stabilization; ignoring --camera-ois");
            }
        }

        if (eis) {
            int[] modes = characteristics.get(CameraCharacteristics.CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES);
            if (contains(modes, CameraMetadata.CONTROL_VIDEO_STABILIZATION_MODE_ON)) {
                requestBuilder.set(CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE, CameraMetadata.CONTROL_VIDEO_STABILIZATION_MODE_ON);
                Ln.i("Camera video stabilization enabled");
            } else {
                Ln.w("Camera '" + cameraId + "' does not support video stabilization; ignoring --camera-eis");
            }
        }
    }

    private void configureManualFocus(CaptureRequest.Builder requestBuilder) throws CameraAccessException {
        if (highSpeed) {
            // Constrained high-speed sessions only accept a restricted set of capture keys
            Ln.w("Manual focus is not supported in high-speed capture mode; ignoring");
            return;
        }

        CameraManager cameraManager = ServiceManager.getCameraManager();
        CameraCharacteristics characteristics = cameraManager.getCameraCharacteristics(getLensCameraId());

        int[] afModes = characteristics.get(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES);
        boolean afOffSupported = false;
        if (afModes != null) {
            for (int mode : afModes) {
                if (mode == CameraMetadata.CONTROL_AF_MODE_OFF) {
                    afOffSupported = true;
                    break;
                }
            }
        }

        if (!afOffSupported) {
            Ln.w("Camera '" + cameraId + "' does not support disabling autofocus; ignoring "
                    + "--no-camera-autofocus/--camera-focus-distance");
            return;
        }

        requestBuilder.set(CaptureRequest.CONTROL_AF_MODE, CameraMetadata.CONTROL_AF_MODE_OFF);
        Ln.i("Camera autofocus disabled");

        if (!Float.isNaN(focusDistance)) {
            Float minDistance = characteristics.get(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE);
            if (minDistance == null || minDistance == 0) {
                // 0 (or unavailable) means the lens is fixed-focus: LENS_FOCUS_DISTANCE cannot be set
                Ln.w("Camera '" + cameraId + "' has a fixed-focus lens; --camera-focus-distance ignored");
            } else {
                float clamped = focusDistance;
                if (clamped < 0) {
                    clamped = 0;
                } else if (clamped > minDistance) {
                    Ln.w("Requested focus distance " + focusDistance + " exceeds the closest focus distance "
                            + minDistance + "; clamping");
                    clamped = minDistance;
                }
                requestBuilder.set(CaptureRequest.LENS_FOCUS_DISTANCE, clamped);
                manualFocus = clamped;
                Ln.i("Camera focus distance set to " + clamped + " diopters");
            }
        }
    }

    @TargetApi(AndroidVersions.API_31_ANDROID_12)
    private void setRepeatingRequest(CameraCaptureSession session, CaptureRequest request) throws CameraAccessException {
        CameraCaptureSession.CaptureCallback callback = new CameraCaptureSession.CaptureCallback() {
            @Override
            public void onCaptureStarted(CameraCaptureSession session, CaptureRequest request, long timestamp, long frameNumber) {
                // Called for each frame captured, do nothing
            }

            @Override
            public void onCaptureFailed(CameraCaptureSession session, CaptureRequest request, CaptureFailure failure) {
                Ln.w("Camera capture failed: frame " + failure.getFrameNumber());
            }
        };

        if (highSpeed) {
            CameraConstrainedHighSpeedCaptureSession highSpeedSession = (CameraConstrainedHighSpeedCaptureSession) session;
            List<CaptureRequest> requests = highSpeedSession.createHighSpeedRequestList(request);
            highSpeedSession.setRepeatingBurst(requests, callback, cameraHandler);
        } else {
            session.setRepeatingRequest(request, callback, cameraHandler);
        }
    }

    @Override
    public boolean isClosed() {
        return disconnected.get();
    }

    public void setTorchEnabled(boolean enabled) {
        cameraHandler.post(() -> {
            assertCameraThread();
            if (currentSession != null && requestBuilder != null) {
                try {
                    Ln.i("Turn camera torch " + (enabled ? "on" : "off"));
                    requestBuilder.set(CaptureRequest.FLASH_MODE, enabled ? CaptureRequest.FLASH_MODE_TORCH : CaptureRequest.FLASH_MODE_OFF);
                    CaptureRequest request = requestBuilder.build();
                    setRepeatingRequest(currentSession, request);
                } catch (CameraAccessException e) {
                    Ln.e("Camera error", e);
                }
            }
        });
    }

    @TargetApi(AndroidVersions.API_30_ANDROID_11)
    private void zoom(boolean in) {
        cameraHandler.post(() -> {
            assertCameraThread();
            if (currentSession != null && requestBuilder != null) {
                // Always align to log values
                double z = Math.round(Math.log(zoom) / Math.log(ZOOM_FACTOR));
                double dir = in ? 1 : -1;
                zoom = (float) Math.pow(ZOOM_FACTOR, z + dir);

                try {
                    zoom = clampZoom(zoom);
                    Ln.i("Set camera zoom: " + zoom);
                    requestBuilder.set(CaptureRequest.CONTROL_ZOOM_RATIO, zoom);
                    CaptureRequest request = requestBuilder.build();
                    setRepeatingRequest(currentSession, request);
                } catch (CameraAccessException e) {
                    Ln.e("Camera error", e);
                }
            }
        });
    }

    public void zoomIn() {
        zoom(true);
    }

    private static final int FOCUS_STEPS = 20; // from infinity to the closest distance

    public void adjustFocus(boolean nearer) {
        cameraHandler.post(() -> {
            assertCameraThread();
            if (currentSession == null || requestBuilder == null) {
                return;
            }
            if (highSpeed || minFocusDistance == null || minFocusDistance == 0) {
                Ln.w("Manual focus is not available for this camera");
                return;
            }

            float step = minFocusDistance / FOCUS_STEPS;
            // Without a known manual distance (autofocus), start from infinity
            float current = Float.isNaN(manualFocus) ? 0 : manualFocus;
            manualFocus = Math.max(0, Math.min(minFocusDistance, current + (nearer ? step : -step)));

            try {
                Ln.i("Set camera focus distance: " + manualFocus + " diopters");
                requestBuilder.set(CaptureRequest.CONTROL_AF_MODE, CameraMetadata.CONTROL_AF_MODE_OFF);
                requestBuilder.set(CaptureRequest.LENS_FOCUS_DISTANCE, manualFocus);
                setRepeatingRequest(currentSession, requestBuilder.build());
            } catch (CameraAccessException e) {
                Ln.e("Camera error", e);
            }
        });
    }

    public void resetAutofocus() {
        cameraHandler.post(() -> {
            assertCameraThread();
            if (currentSession == null || requestBuilder == null || highSpeed) {
                return;
            }

            try {
                Ln.i("Set camera autofocus");
                manualFocus = Float.NaN;
                requestBuilder.set(CaptureRequest.CONTROL_AF_MODE, CameraMetadata.CONTROL_AF_MODE_CONTINUOUS_VIDEO);
                setRepeatingRequest(currentSession, requestBuilder.build());
            } catch (CameraAccessException e) {
                Ln.e("Camera error", e);
            }
        });
    }

    public void zoomOut() {
        zoom(false);
    }

    private float clampZoom(float value) {
        assertCameraThread();
        if (zoomRange == null) {
            return value;
        }

        return zoomRange.clamp(value);
    }

    private void assertCameraThread() {
        assert Thread.currentThread() == cameraThread;
    }
}
