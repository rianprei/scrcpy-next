package com.genymobile.scrcpy.wrappers;

import com.genymobile.scrcpy.FakeContext;
import com.genymobile.scrcpy.util.Ln;

import android.content.ClipData;
import android.content.Context;
import android.net.Uri;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

public final class ClipboardManager {

    public static final class Image {
        public final String mimeType;
        public final byte[] data;

        private Image(String mimeType, byte[] data) {
            this.mimeType = mimeType;
            this.data = data;
        }
    }

    private final android.content.ClipboardManager manager;

    static ClipboardManager create() {
        android.content.ClipboardManager manager = (android.content.ClipboardManager) FakeContext.get().getSystemService(Context.CLIPBOARD_SERVICE);
        if (manager == null) {
            // Some devices have no clipboard manager
            // <https://github.com/Genymobile/scrcpy/issues/1440>
            // <https://github.com/Genymobile/scrcpy/issues/1556>
            return null;
        }
        return new ClipboardManager(manager);
    }

    private ClipboardManager(android.content.ClipboardManager manager) {
        this.manager = manager;
    }

    public CharSequence getText() {
        ClipData clipData = manager.getPrimaryClip();
        if (clipData == null || clipData.getItemCount() == 0) {
            return null;
        }
        return clipData.getItemAt(0).getText();
    }

    /**
     * Read the image of the clipboard (the content URI of the first item, if its MIME type is image/*).
     *
     * @return the image, or null if there is none or if it cannot be read
     */
    public Image getImage() {
        ClipData clipData = manager.getPrimaryClip();
        if (clipData == null || clipData.getItemCount() == 0) {
            return null;
        }
        String[] mimeTypes = clipData.getDescription().filterMimeTypes("image/*");
        Uri uri = clipData.getItemAt(0).getUri();
        if (mimeTypes == null || uri == null || mimeTypes[0].length() > 255) {
            return null;
        }

        try (InputStream in = FakeContext.get().getContentResolver().openInputStream(uri)) {
            if (in == null) {
                return null;
            }
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[64 * 1024];
            int r;
            while ((r = in.read(buffer)) != -1) {
                out.write(buffer, 0, r);
            }
            return new Image(mimeTypes[0], out.toByteArray());
        } catch (IOException | RuntimeException e) {
            // The provider may refuse (e.g. SecurityException) or fail
            Ln.w("Could not read the device clipboard image: " + e);
            return null;
        } catch (OutOfMemoryError e) {
            // No size limit: an image too large for the server memory must not kill the server
            Ln.w("Device clipboard image too large for the memory, not copied");
            return null;
        }
    }

    public boolean setImage(Uri uri, String mimeType) {
        ClipData clipData = new ClipData("image", new String[] {mimeType}, new ClipData.Item(uri));
        manager.setPrimaryClip(clipData);
        return true;
    }

    public boolean setText(CharSequence text) {
        ClipData clipData = ClipData.newPlainText(null, text);
        manager.setPrimaryClip(clipData);
        return true;
    }

    public void addPrimaryClipChangedListener(android.content.ClipboardManager.OnPrimaryClipChangedListener listener) {
        manager.addPrimaryClipChangedListener(listener);
    }
}
