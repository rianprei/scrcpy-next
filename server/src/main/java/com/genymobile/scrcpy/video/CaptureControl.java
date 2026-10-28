package com.genymobile.scrcpy.video;

import com.genymobile.scrcpy.util.Ln;

import android.media.MediaCodec;
import android.os.Bundle;

public class CaptureControl {

    public static final int RESET_REASON_TERMINATED = 1;
    public static final int RESET_REASON_DISPLAY_PROPERTIES_CHANGED = 1 << 1;
    public static final int RESET_REASON_CLIENT_RESET = 1 << 2;
    public static final int RESET_REASON_CLIENT_RESIZED = 1 << 3;

    private int reset = 0;

    // Current instance of MediaCodec to "interrupt" on reset
    private MediaCodec runningMediaCodec;

    public synchronized boolean isResetRequested() {
        return reset != 0;
    }

    public synchronized int consumeReset() {
        int value = reset;
        reset = 0;
        return value;
    }

    public synchronized void reset(int reason) {
        assert reason != 0;
        reset |= reason;
        if (runningMediaCodec != null) {
            try {
                runningMediaCodec.signalEndOfInputStream();
            } catch (IllegalStateException e) {
                // ignore
            }
        }
    }

    /**
     * Ask the running encoder for a key frame as soon as possible, without restarting the capture (e.g. for a client joining a stream).
     */
    public synchronized void requestSyncFrame() {
        if (runningMediaCodec != null) {
            Bundle params = new Bundle();
            params.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0);
            try {
                runningMediaCodec.setParameters(params);
                Ln.d("Key frame requested");
            } catch (IllegalStateException e) {
                // the codec is being reset, the next stream starts with a key frame anyway
            }
        }
    }

    public synchronized void setRunningMediaCodec(MediaCodec runningMediaCodec) {
        this.runningMediaCodec = runningMediaCodec;
    }
}
