package kindleroid.launcher;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.util.AtomicFile;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;

final class SleepWallpaper {
    private static final String NAME = "sleep-wallpaper.rgba";
    private static final int EDGE = 840;

    private SleepWallpaper() {}

    private static AtomicFile saved(Context c) {
        return new AtomicFile(new File(c.getFilesDir(), NAME));
    }

    static synchronized void publish(Context c) throws IOException {
        File directory = HostSettings.directory(c);
        if (directory.equals(c.getFilesDir())) throw new IOException("wallpaper bridge unavailable");
        byte[] data;
        try {
            data = saved(c).readFully();
        } catch (FileNotFoundException absent) {
            File target = new File(directory, NAME);
            if (target.exists() && !target.delete()) throw new IOException("remove wallpaper");
            return;
        }
        HostSettings.atomic(c, NAME, data);
    }

    static synchronized void reset(Context c) throws IOException {
        saved(c).delete();
        publish(c);
    }

    static synchronized void select(Context c, Uri uri) throws IOException {
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inJustDecodeBounds = true;
        try (InputStream input = c.getContentResolver().openInputStream(uri)) {
            BitmapFactory.decodeStream(input, null, options);
        }
        int longest = Math.max(options.outWidth, options.outHeight);
        if (!"image/png".equals(options.outMimeType) || longest <= 0
                || options.outWidth <= 0 || options.outHeight <= 0 || longest > 16384)
            throw new IOException("unsupported PNG");
        options.inJustDecodeBounds = false;
        options.inSampleSize = 1;
        while (longest / options.inSampleSize > EDGE * 2) options.inSampleSize *= 2;
        Bitmap decoded;
        try (InputStream input = c.getContentResolver().openInputStream(uri)) {
            decoded = BitmapFactory.decodeStream(input, null, options);
        }
        if (decoded == null) throw new IOException("decode wallpaper");
        int w = decoded.getWidth(), h = decoded.getHeight();
        float scale = Math.min(1f, (float) EDGE / Math.max(w, h));
        Bitmap image = Bitmap.createScaledBitmap(decoded, Math.max(1, Math.round(w * scale)),
                Math.max(1, Math.round(h * scale)), true);
        if (image != decoded) decoded.recycle();
        w = image.getWidth();
        h = image.getHeight();
        ByteArrayOutputStream bytes = new ByteArrayOutputStream(4 + w * h * 4);
        bytes.write(w >> 8); bytes.write(w); bytes.write(h >> 8); bytes.write(h);
        int[] row = new int[w];
        for (int y = 0; y < h; y++) {
            image.getPixels(row, 0, w, 0, y, w, 1);
            for (int argb : row) {
                bytes.write(argb >> 16); bytes.write(argb >> 8);
                bytes.write(argb); bytes.write(argb >>> 24);
            }
        }
        image.recycle();

        AtomicFile file = saved(c);
        FileOutputStream output = file.startWrite();
        try {
            bytes.writeTo(output);
            file.finishWrite(output);
        } catch (IOException e) {
            file.failWrite(output);
            throw e;
        }
        publish(c);
    }
}
