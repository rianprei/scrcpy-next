package com.genymobile.scrcpy.video;

import com.genymobile.scrcpy.AndroidVersions;
import com.genymobile.scrcpy.Options;
import com.genymobile.scrcpy.control.PositionMapper;
import com.genymobile.scrcpy.device.Device;
import com.genymobile.scrcpy.display.DisplayInfo;
import com.genymobile.scrcpy.display.DisplayMonitor;
import com.genymobile.scrcpy.display.DisplayProperties;
import com.genymobile.scrcpy.model.ConfigurationException;
import com.genymobile.scrcpy.model.Orientation;
import com.genymobile.scrcpy.model.Size;
import com.genymobile.scrcpy.opengl.AffineOpenGLFilter;
import com.genymobile.scrcpy.opengl.OpenGLFilter;
import com.genymobile.scrcpy.opengl.OpenGLRunner;
import com.genymobile.scrcpy.util.AffineMatrix;
import com.genymobile.scrcpy.util.Ln;
import com.genymobile.scrcpy.util.LogUtils;
import com.genymobile.scrcpy.wrappers.ServiceManager;
import com.genymobile.scrcpy.wrappers.SurfaceControl;

import android.graphics.Rect;
import android.hardware.display.VirtualDisplay;
import android.os.Build;
import android.os.IBinder;
import android.view.Surface;

import java.io.IOException;
import java.util.Locale;

public class ScreenCapture extends SurfaceCapture {

    private final VirtualDisplayListener vdListener;
    private final int displayId;
    private final Rect crop;
    private Orientation.Lock captureOrientationLock;
    private Orientation captureOrientation;
    private final float angle;

    private VideoConstraints videoConstraints;

    private DisplayInfo displayInfo;
    private Size videoSize;

    private final DisplayMonitor displayMonitor = new DisplayMonitor();

    private IBinder display;
    private VirtualDisplay virtualDisplay;

    private AffineMatrix transform;
    private OpenGLRunner glRunner;

    // --camera-pip: the camera drawn over the screen, at the bottom-right corner
    private static final int PIP_CAMERA_MAX_SIZE = 640;
    private final CameraCapture pipCamera;
    private AffineMatrix pipTransform; // shows the camera like a preview
    private Size pipSize;

    public ScreenCapture(VirtualDisplayListener vdListener, Options options) {
        this.vdListener = vdListener;
        this.displayId = options.getDisplayId();
        assert displayId != Device.DISPLAY_ID_NONE;
        this.crop = options.getCrop();
        this.captureOrientationLock = options.getCaptureOrientationLock();
        this.captureOrientation = options.getCaptureOrientation();
        assert captureOrientationLock != null;
        assert captureOrientation != null;
        this.angle = options.getAngle();
        this.pipCamera = options.getCameraPip() ? new CameraCapture(options, true) : null;
    }

    @Override
    public void init(VideoConstraints videoConstraints) throws ConfigurationException, IOException {
        this.videoConstraints = videoConstraints;
        displayMonitor.start(displayId, (props) -> getCaptureControl().reset(CaptureControl.RESET_REASON_DISPLAY_PROPERTIES_CHANGED));
        if (pipCamera != null) {
            // A camera error must not stop the screen capture: give it its own capture control (the overlay just freezes)
            pipCamera.init(new CaptureControl(), new VideoConstraints(PIP_CAMERA_MAX_SIZE, 1, null));
        }
    }

    @Override
    public void prepare() throws ConfigurationException, IOException {
        displayInfo = ServiceManager.getDisplayManager().getDisplayInfo(displayId);
        if (displayInfo == null) {
            Ln.e("Display " + displayId + " not found\n" + LogUtils.buildDisplayListMessage());
            throw new ConfigurationException("Unknown display id: " + displayId);
        }

        if ((displayInfo.getFlags() & DisplayInfo.FLAG_SUPPORTS_PROTECTED_BUFFERS) == 0) {
            Ln.w("Display doesn't have FLAG_SUPPORTS_PROTECTED_BUFFERS flag, mirroring can be restricted");
        }

        Size displaySize = displayInfo.getSize();
        int displayRotation = displayInfo.getRotation();
        displayMonitor.setSessionDisplayProperties(new DisplayProperties(displaySize, displayRotation));

        if (captureOrientationLock == Orientation.Lock.LockedInitial) {
            // The user requested to lock the video orientation to the current orientation
            captureOrientationLock = Orientation.Lock.LockedValue;
            captureOrientation = Orientation.fromRotation(displayRotation);
        }

        VideoFilter filter = new VideoFilter(displaySize);

        if (crop != null) {
            boolean transposed = (displayRotation % 2) != 0;
            filter.addCrop(crop, transposed);
        }

        boolean locked = captureOrientationLock != Orientation.Lock.Unlocked;
        filter.addOrientation(displayRotation, locked, captureOrientation);
        filter.addAngle(angle);

        transform = filter.getInverseTransform();
        videoSize = filter.getOutputSize().constrain(videoConstraints);

        if (pipCamera != null) {
            pipCamera.prepare();
            VideoFilter pipFilter = new VideoFilter(pipCamera.getSize());
            pipFilter.addOrientation(pipCamera.getPreviewOrientation(displayRotation));
            pipTransform = pipFilter.getInverseTransform();
            if (pipTransform == null) {
                pipTransform = AffineMatrix.IDENTITY;
            }
            pipSize = pipFilter.getOutputSize();
            if (transform == null) {
                // The overlay is drawn by OpenGL
                transform = AffineMatrix.IDENTITY;
            }
        }
    }

    // The camera rectangle in the video, in OpenGL coordinates: x, y (from the bottom-left corner), width, height
    private static int[] computePipViewport(Size video, Size camera) {
        int min = Math.min(video.getWidth(), video.getHeight());
        int width = min * 3 / 10;
        int height = width * camera.getHeight() / camera.getWidth();
        int margin = min / 40;
        return new int[] {video.getWidth() - margin - width, margin, width, height};
    }

    @Override
    public void start(Surface surface) throws IOException {
        if (display != null) {
            SurfaceControl.destroyDisplay(display);
            display = null;
        }
        if (virtualDisplay != null) {
            virtualDisplay.release();
            virtualDisplay = null;
        }

        Size inputSize;
        if (transform != null) {
            // If there is a filter, it must receive the full display content
            inputSize = displayInfo.getSize();
            assert glRunner == null;
            OpenGLFilter glFilter = new AffineOpenGLFilter(transform);
            glRunner = new OpenGLRunner(glFilter);
            surface = glRunner.start(inputSize, videoSize, surface);
            if (pipCamera != null) {
                int[] viewport = computePipViewport(videoSize, pipSize);
                Surface cameraSurface = glRunner.startOverlay(pipCamera.getSize(), CameraCapture.VFLIP_MATRIX, pipTransform, viewport);
                pipCamera.start(cameraSurface);
            }
        } else {
            // If there is no filter, the display must be rendered at target video size directly
            inputSize = videoSize;
        }

        try {
            virtualDisplay = ServiceManager.getDisplayManager()
                    .createVirtualDisplay("scrcpy", inputSize.getWidth(), inputSize.getHeight(), displayId, surface);
            Ln.d("Display: using DisplayManager API");
        } catch (Exception displayManagerException) {
            if (Build.BRAND.equalsIgnoreCase("oculus") && Build.MODEL.toLowerCase(Locale.ROOT).startsWith("quest ")) {
                // Workaround for buggy createVirtualDisplay on Quest
                // Extra workaround to fix the original Quest
                try {
                    virtualDisplay = (VirtualDisplay) VirtualDisplay.class.getDeclaredConstructors()[0].newInstance(null, null, null, surface);
                } catch (ReflectiveOperationException e) {
                    Ln.e("Could not create VirtualDisplay", e);
                }
            } else {
                try {
                    display = createDisplay();

                    Size deviceSize = displayInfo.getSize();
                    int layerStack = displayInfo.getLayerStack();
                    setDisplaySurface(display, surface, deviceSize.toRect(), inputSize.toRect(), layerStack);
                    Ln.d("Display: using SurfaceControl API");
                } catch (Exception surfaceControlException) {
                    Ln.e("Could not create display using DisplayManager", displayManagerException);
                    Ln.e("Could not create display using SurfaceControl", surfaceControlException);
                    throw new AssertionError("Could not create display");
                }
            }
        }

        if (vdListener != null) {
            int virtualDisplayId;
            PositionMapper positionMapper;
            if (virtualDisplay == null || displayId == 0) {
                // Surface control or main display: send all events to the original display, relative to the device size
                Size deviceSize = displayInfo.getSize();
                positionMapper = PositionMapper.create(videoSize, transform, deviceSize);
                virtualDisplayId = displayId;
            } else {
                // The positions are relative to the virtual display, not the original display (so use inputSize, not deviceSize!)
                positionMapper = PositionMapper.create(videoSize, transform, inputSize);
                virtualDisplayId = virtualDisplay.getDisplay().getDisplayId();
            }
            vdListener.onNewVirtualDisplay(virtualDisplayId, positionMapper);
        }
    }

    @Override
    public void stop() {
        if (pipCamera != null) {
            pipCamera.stop();
        }
        if (glRunner != null) {
            glRunner.stopAndRelease();
            glRunner = null;
        }
    }

    @Override
    public void release() {
        displayMonitor.stopAndRelease();
        if (pipCamera != null) {
            pipCamera.release();
        }

        if (display != null) {
            SurfaceControl.destroyDisplay(display);
            display = null;
        }
        if (virtualDisplay != null) {
            virtualDisplay.release();
            virtualDisplay = null;
        }
    }

    @Override
    public Size getSize() {
        return videoSize;
    }

    @Override
    protected boolean applyNewVideoConstraints(VideoConstraints videoConstraints) {
        this.videoConstraints = videoConstraints;
        return true;
    }

    private static IBinder createDisplay() throws Exception {
        // Since Android 12 (preview), secure displays could not be created with shell permissions anymore.
        // On Android 12 preview, SDK_INT is still R (not S), but CODENAME is "S".
        boolean secure = Build.VERSION.SDK_INT < AndroidVersions.API_30_ANDROID_11 || (Build.VERSION.SDK_INT == AndroidVersions.API_30_ANDROID_11
                && !"S".equals(Build.VERSION.CODENAME));
        return SurfaceControl.createDisplay("scrcpy", secure);
    }

    private static void setDisplaySurface(IBinder display, Surface surface, Rect deviceRect, Rect displayRect, int layerStack) {
        SurfaceControl.openTransaction();
        try {
            SurfaceControl.setDisplaySurface(display, surface);
            SurfaceControl.setDisplayProjection(display, 0, deviceRect, displayRect);
            SurfaceControl.setDisplayLayerStack(display, layerStack);
        } finally {
            SurfaceControl.closeTransaction();
        }
    }
}
