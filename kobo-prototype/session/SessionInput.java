import android.os.SystemClock;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.KeyEvent;
import android.view.MotionEvent;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.lang.reflect.Method;

/* Injects the panel's live touch stream into Android. The panel writes one command per line to the bridge/input
   FIFO: "d x y" (down), "m x y" (move), "u x y" (up), "M n id x y [id x y]" (all fingers while two are down) and
   "k code" (key), in Android coordinates. Android's own gesture detection then sees real timing, so drags, long
   presses, flings and pinches behave as on a phone. */
public final class SessionInput {
    /* Touches arrive in the display's current orientation (the panel converts them); either side can be 1680. */
    private static final int WIDTH = 1680, HEIGHT = 1680;
    private final Object manager;
    private final Method inject;
    private long downTime = -1;
    private int moves, count;
    private final int[] ids = new int[2], xs = new int[2], ys = new int[2];

    private SessionInput() throws Exception {
        Class<?> type = Class.forName("android.hardware.input.InputManager");
        manager = type.getMethod("getInstance").invoke(null);
        inject = type.getMethod("injectInputEvent", InputEvent.class, int.class);
    }

    private boolean send(InputEvent event) throws Exception {
        return Boolean.TRUE.equals(inject.invoke(manager, event, 0));
    }

    /* One event for the current pointers; index selects the pointer of a POINTER_DOWN or POINTER_UP. */
    private boolean motion(int action, int index) throws Exception {
        MotionEvent.PointerProperties[] props = new MotionEvent.PointerProperties[count];
        MotionEvent.PointerCoords[] coords = new MotionEvent.PointerCoords[count];
        for (int i = 0; i < count; i++) {
            props[i] = new MotionEvent.PointerProperties();
            props[i].id = ids[i];
            props[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
            coords[i] = new MotionEvent.PointerCoords();
            coords[i].x = xs[i];
            coords[i].y = ys[i];
            coords[i].pressure = 1;
            coords[i].size = 1;
        }
        MotionEvent event = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(),
            action | (index << MotionEvent.ACTION_POINTER_INDEX_SHIFT), count, props, coords, 0, 0, 1, 1, 0, 0,
            InputDevice.SOURCE_TOUCHSCREEN, 0);
        try {
            return send(event);
        } finally {
            event.recycle();
        }
    }

    private void place(int i, int id, int x, int y) {
        ids[i] = id;
        xs[i] = Math.max(0, Math.min(WIDTH - 1, x));
        ys[i] = Math.max(0, Math.min(HEIGHT - 1, y));
    }

    private int indexOf(int id) {
        for (int i = 0; i < count; i++) if (ids[i] == id) return i;
        return -1;
    }

    /* "M": fingers that appeared go down, fingers that left go up, the rest move. */
    private void multi(String[] f) throws Exception {
        int n = Integer.parseInt(f[1]);
        if (n < 1 || n > 2 || f.length != 2 + 3 * n || downTime < 0) return;
        int[] id = new int[n], x = new int[n], y = new int[n];
        for (int i = 0; i < n; i++) {
            id[i] = Integer.parseInt(f[2 + 3 * i]);
            x[i] = Integer.parseInt(f[3 + 3 * i]);
            y[i] = Integer.parseInt(f[4 + 3 * i]);
        }
        boolean changed = false;
        for (int i = count - 1; i >= 0; i--) {
            boolean kept = false;
            for (int k = 0; k < n; k++) kept |= id[k] == ids[i];
            if (kept || count == 1) continue;
            motion(MotionEvent.ACTION_POINTER_UP, i);
            for (int j = i; j < count - 1; j++) place(j, ids[j + 1], xs[j + 1], ys[j + 1]);
            count--;
            changed = true;
        }
        for (int k = 0; k < n; k++) {
            int i = indexOf(id[k]);
            if (i < 0 && count == 1 && n == 1) i = 0;  // the remaining finger may carry the other slot's id
            if (i < 0 && count < 2) {
                place(count++, id[k], x[k], y[k]);
                motion(MotionEvent.ACTION_POINTER_DOWN, count - 1);
                changed = true;
            } else if (i >= 0) {
                place(i, id[k], x[k], y[k]);
            }
        }
        if (!changed) motion(MotionEvent.ACTION_MOVE, 0);
        ++moves;
    }

    private void command(String line) throws Exception {
        String[] f = line.split(" ");
        if (f.length == 3 && f[0].equals("d")) {
            if (downTime >= 0) motion(MotionEvent.ACTION_CANCEL, 0);
            downTime = SystemClock.uptimeMillis();
            moves = 0;
            count = 1;
            place(0, 0, Integer.parseInt(f[1]), Integer.parseInt(f[2]));
            motion(MotionEvent.ACTION_DOWN, 0);
        } else if (f.length == 3 && f[0].equals("m") && downTime >= 0 && count == 1) {
            ++moves;
            place(0, ids[0], Integer.parseInt(f[1]), Integer.parseInt(f[2]));
            motion(MotionEvent.ACTION_MOVE, 0);
        } else if (f.length >= 2 && f[0].equals("M")) {
            multi(f);
        } else if (f.length == 3 && f[0].equals("u") && downTime >= 0) {
            while (count > 1) { motion(MotionEvent.ACTION_POINTER_UP, count - 1); count--; }
            place(0, ids[0], Integer.parseInt(f[1]), Integer.parseInt(f[2]));
            boolean accepted = motion(MotionEvent.ACTION_UP, 0);
            System.out.println("SESSION_INPUT touch moves=" + moves + " ms=" + (SystemClock.uptimeMillis() - downTime)
                + " accepted=" + accepted);
            downTime = -1;
        } else if (f.length == 2 && f[0].equals("r")) {
            /* Display rotation from the panel (control centre or the Kobo's tilt sensor): a locked user rotation,
               which apps that ask for an orientation still override. This process runs as root in the guest. */
            int rotation = Integer.parseInt(f[1]);
            if (rotation < 0 || rotation > 3) return;
            try {  /* never let a failure here end the touch stream */
                Object binder = Class.forName("android.os.ServiceManager").getMethod("getService", String.class).invoke(null, "window");
                Object wm = Class.forName("android.view.IWindowManager$Stub").getMethod("asInterface", android.os.IBinder.class).invoke(null, binder);
                wm.getClass().getMethod("freezeRotation", int.class).invoke(wm, rotation);
                System.out.println("SESSION_INPUT rotation=" + rotation);
            } catch (Exception e) {
                System.out.println("SESSION_INPUT rotation_failed=" + e);
            }
        } else if (f.length == 2 && f[0].equals("k")) {
            int code = Integer.parseInt(f[1]);
            if (code != 3 && code != 4 && code != 92 && code != 93) return;
            long now = SystemClock.uptimeMillis();
            boolean down = send(new KeyEvent(now, now, KeyEvent.ACTION_DOWN, code, 0));
            boolean up = send(new KeyEvent(now, now, KeyEvent.ACTION_UP, code, 0));
            System.out.println("SESSION_INPUT key=" + code + " accepted=" + (down && up));
        }
    }

    public static void main(String[] args) throws Exception {
        SessionInput input = new SessionInput();
        File fifo = new File("/bridge/input");
        File stop = new File("/bridge/stop");
        File result = new File("/bridge/result");
        System.out.println("SESSION_INPUT_READY=1");
        while (!stop.exists() && !result.exists()) {
            if (!fifo.exists()) {
                Thread.sleep(50);
                continue;
            }
            /* Opening blocks until the panel opens its end; EOF means the panel closed it. */
            try (BufferedReader reader = new BufferedReader(new FileReader(fifo))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    try {
                        input.command(line);
                    } catch (NumberFormatException ignored) {
                        System.out.println("SESSION_INPUT_INVALID=1");
                    }
                }
            }
        }
    }
}
