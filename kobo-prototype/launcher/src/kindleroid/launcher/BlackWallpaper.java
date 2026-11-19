package kindleroid.launcher;

import android.graphics.Canvas;
import android.graphics.Color;
import android.service.wallpaper.WallpaperService;
import android.view.SurfaceHolder;
import java.lang.reflect.Method;

/** Static black wallpaper on a 1x1 surface: one draw, no animation, almost no buffer memory. */
public class BlackWallpaper extends WallpaperService {
    @Override
    public Engine onCreateEngine() {
        return new BlackEngine();
    }

    class BlackEngine extends Engine {
        @Override
        public void onCreate(SurfaceHolder holder) {
            super.onCreate(holder);
            setTouchEventsEnabled(false);
            setOffsetNotificationsEnabled(false);
            // A wallpaper may only set a fixed size after the hidden Engine.setFixedSizeAllowed(true), as
            // SystemUI's ImageWallpaper does (Android 7 has no hidden-API restrictions). SurfaceFlinger scales
            // the 1x1 buffer to the window, so the screen is still fully black but the buffer costs ~nothing.
            try {
                Method allow = Engine.class.getDeclaredMethod("setFixedSizeAllowed", boolean.class);
                allow.setAccessible(true);
                allow.invoke(this, true);
                holder.setFixedSize(1, 1);
            } catch (ReflectiveOperationException e) {
                // Falls back to a window-sized buffer; still a valid wallpaper.
            }
        }

        @Override
        public void onSurfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            super.onSurfaceChanged(holder, format, width, height);
            draw(holder);
        }

        @Override
        public void onSurfaceRedrawNeeded(SurfaceHolder holder) {
            super.onSurfaceRedrawNeeded(holder);
            draw(holder);
        }

        private void draw(SurfaceHolder holder) {
            Canvas canvas = holder.lockCanvas();
            if (canvas != null) {
                canvas.drawColor(Color.BLACK);
                holder.unlockCanvasAndPost(canvas);
            }
        }
    }
}
