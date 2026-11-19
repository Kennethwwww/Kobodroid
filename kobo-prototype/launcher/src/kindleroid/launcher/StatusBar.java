package kindleroid.launcher;

import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.PixelFormat;
import android.graphics.drawable.Drawable;
import android.hardware.display.DisplayManager;
import android.view.Display;
import android.graphics.Typeface;
import android.os.BatteryManager;
import android.os.Handler;
import android.os.IBinder;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/** Android's status bar on Kobodroid: time, Wi-Fi and battery. As a real TYPE_STATUS_BAR window (the launcher is
 *  platform-signed), the window manager reserves its space and hides it while an app is fullscreen, e.g. while
 *  reading, and shows it again with the app's menus. A tap opens the panel's control centre. */
public final class StatusBar extends Service {
    private static final int TYPE_STATUS_BAR = 2000;
    private Bar bar;
    private WindowManager.LayoutParams params;
    private boolean added;
    private int attempts;
    private int battery = -1;
    private boolean charging, wifi;
    private final Handler handler = new Handler();
    private HostSettings.Watch host;
    private int appliedRotateId = -1, reportedRotation = -1, lastCleanup;

    /* Rotation is chosen in the panel's control centre (ui.state rotate_id/rotate_to) and applied here, as a locked
       user rotation; the display's actual rotation, which an app can also force, goes back to the panel so its
       touch mapping and overlays follow. */
    private final Runnable hostChanged = new Runnable() {
        @Override public void run() {
            HostSettings s = HostSettings.read(StatusBar.this);
            consumeCleanup(s);
            int id = s.get("rotate_id", 0), to = s.get("rotate_to", -1);
            if (id > 0 && id != appliedRotateId && to >= 0 && to <= 3) {
                appliedRotateId = id;
                try {
                    Object binder = Class.forName("android.os.ServiceManager").getMethod("getService", String.class).invoke(null, "window");
                    Object wm = Class.forName("android.view.IWindowManager$Stub").getMethod("asInterface", IBinder.class).invoke(null, binder);
                    wm.getClass().getMethod("freezeRotation", int.class).invoke(wm, to);
                    android.util.Log.i(HomeActivity.TAG, "ROTATION_SET " + to);
                } catch (Exception e) {
                    android.util.Log.w(HomeActivity.TAG, "ROTATION_SET_FAILED", e);
                }
            }
            reportRotation(reportedRotation < 0);
        }
    };

    private void consumeCleanup(HostSettings state) {
        int cleanup = state.get("clean_seq", 0);
        if (cleanup <= lastCleanup) return;
        lastCleanup = cleanup;
        clearBackgroundApps();
    }

    private void clearBackgroundApps() {
        android.app.ActivityManager manager =
                (android.app.ActivityManager) getSystemService(ACTIVITY_SERVICE);
        for (android.content.pm.ApplicationInfo app : getPackageManager().getInstalledApplications(0)) {
            if ((app.flags & android.content.pm.ApplicationInfo.FLAG_SYSTEM) == 0 && !getPackageName().equals(app.packageName))
                manager.killBackgroundProcesses(app.packageName);
        }
        android.util.Log.i(HomeActivity.TAG, "BACKGROUND_CLEANUP_REQUESTED");
        android.widget.Toast.makeText(this, "已请求清理后台", android.widget.Toast.LENGTH_SHORT).show();
    }

    private final DisplayManager.DisplayListener displayListener = new DisplayManager.DisplayListener() {
        @Override public void onDisplayAdded(int id) {}
        @Override public void onDisplayRemoved(int id) {}
        @Override public void onDisplayChanged(int id) {
            if (id == Display.DEFAULT_DISPLAY) reportRotation(false);
        }
    };

    private void reportRotation(boolean force) {
        int rotation = ((WindowManager) getSystemService(WINDOW_SERVICE)).getDefaultDisplay().getRotation();
        if (!force && rotation == reportedRotation) return;
        /* Its own file, not a request: requests share one slot and a later one would replace it. */
        try {
            HostSettings.atomic(this, "display.rotation", (rotation + "\n").getBytes("US-ASCII"));
            reportedRotation = rotation;
            android.util.Log.i(HomeActivity.TAG, "ROTATION_REPORTED " + rotation);
        } catch (IOException e) {
            android.util.Log.w(HomeActivity.TAG, "ROTATION_REPORT_FAILED", e);
        }
    }

    @Override public void onConfigurationChanged(android.content.res.Configuration config) {
        super.onConfigurationChanged(config);
        reportRotation(false);
    }
    private final BroadcastReceiver changed = new BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent i) {
            if (Intent.ACTION_BATTERY_CHANGED.equals(i.getAction())) {
                int level = i.getIntExtra(BatteryManager.EXTRA_LEVEL, -1), scale = i.getIntExtra(BatteryManager.EXTRA_SCALE, 100);
                battery = level >= 0 && scale > 0 ? Math.min(100, level * 100 / scale) : -1;
                charging = i.getIntExtra(BatteryManager.EXTRA_STATUS, -1) == BatteryManager.BATTERY_STATUS_CHARGING;
            }
            bar.invalidate();
        }
    };
    private final Runnable poll = new Runnable() {
        private int ticks;
        @Override public void run() {
            consumeCleanup(HostSettings.read(StatusBar.this));
            if (ticks++ % 10 == 0) {
                boolean now = wifiConnected();
                if (now != wifi) { wifi = now; bar.invalidate(); }
            }
            handler.postDelayed(this, 1000);
        }
    };

    /** The guest shares the Kobo's network stack: Wi-Fi is up when wlan0 carries the default route. */
    static boolean wifiConnected() {
        try (BufferedReader in = new BufferedReader(new FileReader("/proc/net/route"))) {
            String line;
            while ((line = in.readLine()) != null) {
                String[] f = line.trim().split("\\s+");
                if (f.length > 1 && "wlan0".equals(f[0]) && "00000000".equals(f[1])) return true;
            }
        } catch (IOException ignored) {
        }
        return false;
    }

    @Override public void onCreate() {
        super.onCreate();
        bar = new Bar(this);
        params = new WindowManager.LayoutParams(WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.WRAP_CONTENT, TYPE_STATUS_BAR,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_TOUCHABLE_WHEN_WAKING
                | WindowManager.LayoutParams.FLAG_SPLIT_TOUCH, PixelFormat.OPAQUE);
        params.gravity = Gravity.TOP;
        params.windowAnimations = 0;
        params.setTitle("StatusBar");
        ((DisplayManager) getSystemService(DISPLAY_SERVICE)).registerDisplayListener(displayListener, handler);
        addBar.run();
        new Thread(() -> {
            try { SleepWallpaper.publish(this); }
            catch (IOException e) { android.util.Log.w(HomeActivity.TAG, "SLEEP_WALLPAPER_PUBLISH_FAILED", e); }
        }, "sleep-wallpaper-publish").start();
        IntentFilter f = new IntentFilter(Intent.ACTION_BATTERY_CHANGED);
        f.addAction(Intent.ACTION_TIME_TICK);
        f.addAction(Intent.ACTION_TIME_CHANGED);
        f.addAction(Intent.ACTION_TIMEZONE_CHANGED);
        Intent sticky = registerReceiver(changed, f);
        if (sticky != null) changed.onReceive(this, sticky);
        handler.post(poll);
        host = HostSettings.observe(this, hostChanged);
    }

    /* The window manager allows one status bar. A restarted launcher can find its previous one still registered
       for a moment; retry instead of failing, which would crash the launcher in a loop. */
    private final Runnable addBar = new Runnable() {
        @Override public void run() {
            try {
                ((WindowManager) getSystemService(WINDOW_SERVICE)).addView(bar, params);
                added = true;
                android.util.Log.i(HomeActivity.TAG, "STATUS_BAR_ADDED");
            } catch (RuntimeException e) {
                android.util.Log.w(HomeActivity.TAG, "STATUS_BAR_RETRY " + e.getMessage());
                if (++attempts < 30) handler.postDelayed(this, 2000);
            }
        }
    };

    @Override public int onStartCommand(Intent intent, int flags, int startId) { return START_STICKY; }

    @Override public void onDestroy() {
        ((DisplayManager) getSystemService(DISPLAY_SERVICE)).unregisterDisplayListener(displayListener);
        handler.removeCallbacks(poll);
        handler.removeCallbacks(addBar);
        if (host != null) host.stopWatching();
        unregisterReceiver(changed);
        if (added) ((WindowManager) getSystemService(WINDOW_SERVICE)).removeView(bar);
        super.onDestroy();
    }

    @Override public IBinder onBind(Intent intent) { return null; }

    private final class Bar extends View {
        private final Rect digitBounds = new Rect();
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Drawable wifiIcon = UiStyle.icon(StatusBar.this, "wifi").mutate();
        private final SimpleDateFormat clock = new SimpleDateFormat("HH:mm", Locale.CHINA);

        Bar(Context c) {
            super(c);
            setBackgroundColor(Color.WHITE);
            paint.setTypeface(Typeface.create("sans-serif", Typeface.NORMAL));
        }

        @Override protected void onMeasure(int w, int h) {
            setMeasuredDimension(MeasureSpec.getSize(w), getResources().getDimensionPixelSize(
                getResources().getIdentifier("status_bar_height", "dimen", "android")));
        }

        @Override public boolean onTouchEvent(MotionEvent e) {
            if (e.getAction() == MotionEvent.ACTION_UP) {
                try { HostSettings.request(getContext(), "cc", 1); } catch (IOException ignored) {}
            }
            return true;
        }

        @Override protected void onDraw(Canvas c) {
            int w = getWidth(), h = getHeight(), cy = h / 2;
            float u = h / 64f;  // drawn for a 64 px bar
            paint.setColor(0xff000000);
            paint.setStyle(Paint.Style.FILL);
            paint.setTextSize(30 * u);
            paint.getTextBounds("0", 0, 1, digitBounds);
            float baseline = cy - digitBounds.exactCenterY();
            c.drawText(clock.format(new Date()), 28 * u, baseline, paint);
            /* Battery outline, terminal and fill, then its percentage. */
            float right = w - 28 * u, bw = 46 * u, bh = 24 * u, bx = right - 4 * u - bw, by = cy - bh / 2;
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(3 * u);
            c.drawRect(bx, by, bx + bw, by + bh, paint);
            paint.setStyle(Paint.Style.FILL);
            c.drawRect(bx + bw, cy - 5 * u, right, cy + 5 * u, paint);
            if (battery > 0) c.drawRect(bx + 6 * u, by + 6 * u, bx + 6 * u + (bw - 12 * u) * battery / 100, by + bh - 6 * u, paint);
            if (charging) {
                paint.setColor(battery >= 50 ? Color.WHITE : Color.BLACK);
                paint.setStrokeWidth(2 * u);
                float center = bx + bw / 2;
                c.drawLine(center + 2 * u, cy - 7 * u, center - 3 * u, cy, paint);
                c.drawLine(center - 3 * u, cy, center + 3 * u, cy, paint);
                c.drawLine(center + 3 * u, cy, center - 2 * u, cy + 7 * u, paint);
                paint.setColor(Color.BLACK);
            }
            float x = bx - 12 * u;
            if (battery >= 0) {
                String label = battery + "%";
                paint.setTextAlign(Paint.Align.RIGHT);
                c.drawText(label, x, baseline, paint);
                paint.setTextAlign(Paint.Align.LEFT);
                x -= paint.measureText(label);
            }
            int size = Math.round(40 * u), end = Math.round(x - 14 * u);
            wifiIcon.setBounds(end - size, cy - size / 2, end, cy - size / 2 + size);
            wifiIcon.setAlpha(wifi ? 255 : 95);
            wifiIcon.draw(c);
        }
    }
}
