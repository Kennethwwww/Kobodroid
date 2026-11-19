package kindleroid.launcher;

import android.content.res.AssetManager;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.inputmethodservice.InputMethodService;
import android.os.Handler;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;

/** Kobo 拼音: a quick pinyin keyboard for e-ink, after KOReader's. Letters build the pinyin, the candidate row
 *  shows its characters at once, space or a tap commits; when the next letter cannot extend the pinyin, the first
 *  candidate is committed and a new syllable starts (the committed one can still be swapped from the row). One
 *  view draws everything, with no pressed states, popups or animation, so each key costs one small refresh.
 *  Dictionary: rime-pinyin-simp (Apache License 2.0), as converted for KOReader. */
public final class PinyinIme extends InputMethodService {
    private static HashMap<String, String[]> dict;
    private static String[] codes;

    private Board board;
    private final StringBuilder pinyin = new StringBuilder();
    private boolean chinese = true, shift, symbols;
    /* The last automatically committed syllable, so its character can still be swapped. */
    private String lastCode;
    private String lastText;
    private List<String> candidates = new ArrayList<String>();
    private int page;

    private static synchronized void load(AssetManager assets) {
        if (dict != null) return;
        HashMap<String, String[]> map = new HashMap<String, String[]>(8192);
        try (BufferedReader in = new BufferedReader(new InputStreamReader(assets.open("pinyin.txt"), "UTF-8"))) {
            String line;
            while ((line = in.readLine()) != null) {
                if (line.isEmpty() || line.charAt(0) == '#') continue;
                int space = line.indexOf(' ');
                if (space > 0) map.put(line.substring(0, space), line.substring(space + 1).split(" "));
            }
        } catch (IOException e) {
            android.util.Log.e(HomeActivity.TAG, "PINYIN_DICTIONARY_FAILED", e);
        }
        String[] sorted = map.keySet().toArray(new String[0]);
        Arrays.sort(sorted);
        codes = sorted;
        dict = map;
    }

    /** Exact matches first, then the first entries of every longer pinyin starting with code. */
    private static List<String> lookup(String code) {
        java.util.LinkedHashSet<String> out = new java.util.LinkedHashSet<>();
        if (code.isEmpty()) return new ArrayList<>();
        String[] exact = dict.get(code);
        if (exact != null) out.addAll(Arrays.asList(exact));
        int i = Arrays.binarySearch(codes, code);
        if (i < 0) i = -i - 1; else i++;
        for (; i < codes.length && codes[i].startsWith(code) && out.size() < 64; i++) {
            String[] more = dict.get(codes[i]);
            for (int k = 0; k < Math.min(3, more.length) && out.size() < 64; k++) out.add(more[k]);
        }
        return new ArrayList<>(out);
    }

    private static boolean extendable(String code) {
        int i = Arrays.binarySearch(codes, code);
        if (i >= 0) return true;
        i = -i - 1;
        return i < codes.length && codes[i].startsWith(code);
    }

    @Override public void onCreate() {
        super.onCreate();
        load(getAssets());
    }

    @Override public View onCreateInputView() {
        board = new Board();
        return board;
    }

    private void reportImeVisible(boolean visible) {
        if (HostSettings.directory(this).equals(getFilesDir())) return;
        try {
            HostSettings.atomic(this, "ime.visible", new byte[]{(byte)(visible ? '1' : '0')});
        } catch (IOException e) {
            android.util.Log.w(HomeActivity.TAG, "IME_STATE_WRITE_FAILED", e);
        }
    }
    @Override public void onWindowShown() {
        super.onWindowShown();
        reportImeVisible(true);
    }
    @Override public void onWindowHidden() {
        reportImeVisible(false);
        super.onWindowHidden();
    }
    @Override public void onDestroy() {
        reportImeVisible(false);
        super.onDestroy();
    }

    @Override public boolean onEvaluateFullscreenMode() { return false; }

    @Override public void onStartInputView(EditorInfo info, boolean restarting) {
        super.onStartInputView(info, restarting);
        pinyin.setLength(0);
        lastCode = null;
        symbols = shift = false;
        int cls = info.inputType & EditorInfo.TYPE_MASK_CLASS;
        chinese = cls == EditorInfo.TYPE_CLASS_TEXT && (info.inputType & EditorInfo.TYPE_MASK_VARIATION) != EditorInfo.TYPE_TEXT_VARIATION_PASSWORD
            && (info.inputType & EditorInfo.TYPE_MASK_VARIATION) != EditorInfo.TYPE_TEXT_VARIATION_EMAIL_ADDRESS
            && (info.inputType & EditorInfo.TYPE_MASK_VARIATION) != EditorInfo.TYPE_TEXT_VARIATION_URI;
        symbols = cls == EditorInfo.TYPE_CLASS_NUMBER || cls == EditorInfo.TYPE_CLASS_PHONE;
        update();
    }

    @Override public void onFinishInput() {
        commitPinyin(false);
        super.onFinishInput();
    }

    private void update() {
        if (pinyin.length() > 0) candidates = lookup(pinyin.toString());
        else if (lastCode != null) candidates = lookup(lastCode);
        else candidates = new ArrayList<String>();
        page = 0;
        InputConnection ic = getCurrentInputConnection();
        if (ic != null) {
            if (pinyin.length() > 0) ic.setComposingText(pinyin, 1);
            else ic.finishComposingText();
        }
        if (board != null) board.invalidate();
    }

    /** Commits the first candidate (or the letters when there is none, or when asked for the letters). */
    private void commitPinyin(boolean letters) {
        if (pinyin.length() == 0) return;
        InputConnection ic = getCurrentInputConnection();
        List<String> c = lookup(pinyin.toString());
        String text = letters || c.isEmpty() ? pinyin.toString() : c.get(0);
        if (ic != null) ic.commitText(text, 1);
        lastCode = letters || c.isEmpty() ? null : pinyin.toString();
        lastText = text;
        pinyin.setLength(0);
    }

    private void choose(String word) {
        InputConnection ic = getCurrentInputConnection();
        if (ic == null) return;
        if (pinyin.length() > 0) {
            ic.commitText(word, 1);
            pinyin.setLength(0);
        } else if (lastCode != null && lastText != null) {
            ic.deleteSurroundingText(lastText.length(), 0);
            ic.commitText(word, 1);
        }
        lastCode = null;
        update();
    }

    private void letter(char c) {
        InputConnection ic = getCurrentInputConnection();
        if (ic == null) return;
        if (!chinese) {
            ic.commitText(String.valueOf(shift ? Character.toUpperCase(c) : c), 1);
            if (shift) { shift = false; board.invalidate(); }
            return;
        }
        String next = pinyin.toString() + c;
        if (pinyin.length() > 0 && !extendable(next)) {
            commitPinyin(false);
            pinyin.append(c);
        } else {
            pinyin.append(c);
            lastCode = null;
        }
        update();
    }

    private void text(String s) {
        InputConnection ic = getCurrentInputConnection();
        if (ic == null) return;
        commitPinyin(false);
        lastCode = null;
        ic.commitText(s, 1);
        update();
    }

    private void backspace() {
        InputConnection ic = getCurrentInputConnection();
        if (ic == null) return;
        lastCode = null;
        if (pinyin.length() > 0) {
            pinyin.setLength(pinyin.length() - 1);
            update();
        } else {
            ic.deleteSurroundingText(1, 0);
            if (board != null && !candidates.isEmpty()) update();
        }
    }

    private void enter() {
        InputConnection ic = getCurrentInputConnection();
        if (ic == null) return;
        if (pinyin.length() > 0) { commitPinyin(true); lastCode = null; update(); return; }
        EditorInfo info = getCurrentInputEditorInfo();
        int action = info == null ? EditorInfo.IME_ACTION_NONE : info.imeOptions & EditorInfo.IME_MASK_ACTION;
        if (info != null && action != EditorInfo.IME_ACTION_NONE && action != EditorInfo.IME_ACTION_UNSPECIFIED
            && (info.imeOptions & EditorInfo.IME_FLAG_NO_ENTER_ACTION) == 0) ic.performEditorAction(action);
        else {
            ic.sendKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_ENTER));
            ic.sendKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER));
        }
    }

    /* Keys: label shown, and what it does. */
    private static final String[][] LETTERS = {
        {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
        {"a", "s", "d", "f", "g", "h", "j", "k", "l"},
        {"⇧", "z", "x", "c", "v", "b", "n", "m", "⌫"},
        {"123", "中", "，", "空格", "。", "⏎", "↓"},
    };
    private static final String[][] SYMBOLS = {
        {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
        {"@", "#", "%", "&", "*", "-", "+", "=", "/", "…"},
        {"、", "？", "！", "：", "；", "“", "”", "（", "）", "⌫"},
        {"ABC", "《", "》", "空格", ".", "⏎", "↓"},
    };

    private final class Board extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        /* Key outlines: four filled bars on whole pixels, no anti-aliasing. */
        private final float u;
        private final int candH, rowH;
        private final ArrayList<RectF> keyRects = new ArrayList<RectF>();
        private final ArrayList<String> keyLabels = new ArrayList<String>();
        private final ArrayList<RectF> candRects = new ArrayList<RectF>();
        private final ArrayList<String> candWords = new ArrayList<String>();
        private RectF prev, next;
        private final Handler repeat = new Handler();
        private final Runnable repeatDelete = new Runnable() {
            @Override public void run() { backspace(); repeat.postDelayed(this, 90); }
        };

        Board() {
            super(PinyinIme.this);
            setBackgroundColor(Color.WHITE);
            u = UiStyle.shortEdge(PinyinIme.this) / 1264f;
            candH = Math.round(96 * u);
            rowH = Math.round(112 * u);
            paint.setTypeface(Typeface.create("sans-serif", Typeface.NORMAL));
            /* Skia in software, not the guest's software OpenGL, whose thin stroke triangles leave stray slanted
               edges on the key outlines. */
            setLayerType(View.LAYER_TYPE_SOFTWARE, null);
        }

        private final android.graphics.drawable.Drawable shiftIcon = icon("shift");
        private final android.graphics.drawable.Drawable deleteIcon = icon("delete");
        private final android.graphics.drawable.Drawable enterIcon = icon("enter");
        private final android.graphics.drawable.Drawable hideIcon = icon("keyboard_hide");
        private final android.graphics.drawable.Drawable previousIcon = icon("back");
        private final android.graphics.drawable.Drawable nextIcon = icon("next");

        private android.graphics.drawable.Drawable icon(String name) {
            return UiStyle.icon(PinyinIme.this, name).mutate();
        }

        private android.graphics.drawable.Drawable keyIcon(String key) {
            switch (key) {
                case "⇧": return shiftIcon;
                case "⌫": return deleteIcon;
                case "⏎": return enterIcon;
                case "↓": return hideIcon;
                default: return null;
            }
        }

        @Override protected void onMeasure(int w, int h) {
            setMeasuredDimension(MeasureSpec.getSize(w), candH + rowH * 4 + Math.round(16 * u));
        }

        private void layoutKeys(int width) {
            keyRects.clear();
            keyLabels.clear();
            String[][] rows = symbols ? SYMBOLS : LETTERS;
            float gap = 8 * u, side = 10 * u, unit = (width - 2 * side) / 10f;
            for (int r = 0; r < 4; r++) {
                String[] row = rows[r];
                float top = candH + r * rowH + gap / 2, bottom = top + rowH - gap;
                float[] widths = new float[row.length];
                float total = 0;
                for (int i = 0; i < row.length; i++) {
                    String k = row[i];
                    widths[i] = k.equals("空格") ? 3f : (k.equals("⇧") || k.equals("⌫") || k.equals("⏎") || k.length() >= 3) ? 1.5f : 1f;
                    if (r == 3 && !k.equals("空格")) widths[i] = k.equals("123") || k.equals("ABC") || k.equals("⏎") ? 1.5f : 1f;
                    total += widths[i];
                }
                float x = side + (10 - total) * unit / 2;
                for (int i = 0; i < row.length; i++) {
                    keyRects.add(new RectF(Math.round(x + gap / 2), Math.round(top),
                        Math.round(x + widths[i] * unit - gap / 2), Math.round(bottom)));
                    keyLabels.add(row[i]);
                    x += widths[i] * unit;
                }
            }
        }

        @Override protected void onDraw(Canvas c) {
            int w = getWidth();
            layoutKeys(w);
            /* Candidate row: the pinyin, then candidates; arrows page through long lists. */
            candRects.clear();
            candWords.clear();
            prev = next = null;
            paint.setColor(Color.BLACK);
            paint.setTextSize(40 * u);
            Paint.FontMetrics m = paint.getFontMetrics();
            float base = candH / 2f - (m.ascent + m.descent) / 2;
            float x = 24 * u;
            if (pinyin.length() > 0) {
                paint.setTextSize(30 * u);
                c.drawText(pinyin.toString(), x, base, paint);
                x += paint.measureText(pinyin.toString()) + 28 * u;
                paint.setTextSize(40 * u);
            }
            float arrows = 2 * 72 * u, limit = w - arrows - 12 * u;
            int shown = 0, start = 0;
            for (int p = 0, i = 0; p < page && i < candidates.size(); p++) {
                /* Re-measure earlier pages to find where this one starts. */
                float px = x;
                while (i < candidates.size() && px + paint.measureText(candidates.get(i)) + 40 * u <= limit) {
                    px += paint.measureText(candidates.get(i)) + 40 * u;
                    i++;
                }
                start = i;
            }
            for (int i = start; i < candidates.size(); i++) {
                String word = candidates.get(i);
                float ww = paint.measureText(word);
                if (x + ww + 40 * u > limit) break;
                candRects.add(new RectF(x - 20 * u, 0, x + ww + 20 * u, candH));
                candWords.add(word);
                c.drawText(word, x, base, paint);
                x += ww + 40 * u;
                shown++;
            }
            if (page > 0 || start + shown < candidates.size()) {
                paint.setTextSize(40 * u);
                prev = new RectF(w - arrows, 0, w - arrows / 2, candH);
                next = new RectF(w - arrows / 2, 0, w, candH);
                UiStyle.drawIcon(c, previousIcon, prev.centerX(), prev.centerY(), 40 * u,
                                 page > 0 ? Color.BLACK : UiStyle.CONTROL_MUTED);
                UiStyle.drawIcon(c, nextIcon, next.centerX(), next.centerY(), 40 * u,
                                 start + shown < candidates.size() ? Color.BLACK : UiStyle.CONTROL_MUTED);
            }
            paint.setColor(Color.BLACK);
            c.drawRect(0, candH - 2, w, candH, paint);
            for (int i = 0; i < keyRects.size(); i++) {
                RectF r = keyRects.get(i);
                String key = keyLabels.get(i);
                String label = key;
                if (key.equals("中")) label = chinese ? "中" : "英";
                else if (!symbols && !chinese && shift && key.length() == 1 && Character.isLetter(key.charAt(0)))
                    label = key.toUpperCase();
                boolean selected = (key.equals("⇧") && shift) || (key.equals("中") && chinese);
                paint.setColor(selected ? Color.BLACK : UiStyle.CONTROL_FILL);
                c.drawRoundRect(r, 16 * u, 16 * u, paint);
                int ink = selected ? Color.WHITE : Color.BLACK;
                android.graphics.drawable.Drawable icon = keyIcon(key);
                if (icon != null) {
                    UiStyle.drawIcon(c, icon, r.centerX(), r.centerY(), 44 * u, ink);
                } else {
                    paint.setColor(ink);
                    paint.setTextSize((label.length() > 1 ? 34 : 44) * u);
                    Paint.FontMetrics km = paint.getFontMetrics();
                    c.drawText(label, r.centerX() - paint.measureText(label) / 2,
                               r.centerY() - (km.ascent + km.descent) / 2, paint);
                }
            }
        }

        @Override public boolean onTouchEvent(MotionEvent e) {
            float x = e.getX(), y = e.getY();
            if (e.getActionMasked() == MotionEvent.ACTION_DOWN) {
                String key = keyAt(x, y);
                if ("⌫".equals(key)) { backspace(); repeat.postDelayed(repeatDelete, 450); }
                return true;
            }
            if (e.getActionMasked() == MotionEvent.ACTION_CANCEL) { repeat.removeCallbacks(repeatDelete); return true; }
            if (e.getActionMasked() != MotionEvent.ACTION_UP) return true;
            repeat.removeCallbacks(repeatDelete);
            if (y < candH) {
                if (prev != null && prev.contains(x, y)) { if (page > 0) { page--; invalidate(); } return true; }
                if (next != null && next.contains(x, y)) { page++; invalidate(); return true; }
                for (int i = 0; i < candRects.size(); i++) if (candRects.get(i).contains(x, y)) { choose(candWords.get(i)); return true; }
                return true;
            }
            String key = keyAt(x, y);
            if (key == null || key.equals("⌫")) return true;
            switch (key) {
                case "⇧": shift = !shift; invalidate(); break;
                case "123": commitPinyin(false); symbols = true; update(); break;
                case "ABC": symbols = false; update(); break;
                case "中": commitPinyin(true); chinese = !chinese; shift = false; lastCode = null; update(); break;
                case "空格":
                    if (pinyin.length() > 0) { commitPinyin(false); update(); }
                    else text(" ");
                    break;
                case "⏎": enter(); break;
                case "↓":
                    commitPinyin(true);
                    update();
                    requestHideSelf(0);
                    break;
                case "，": text(chinese ? "，" : ","); break;
                case "。": text(chinese ? "。" : "."); break;
                default:
                    if (!symbols && key.length() == 1 && key.charAt(0) >= 'a' && key.charAt(0) <= 'z') letter(key.charAt(0));
                    else text(key);
            }
            return true;
        }

        private String keyAt(float x, float y) {
            for (int i = 0; i < keyRects.size(); i++) {
                RectF r = keyRects.get(i);
                if (x >= r.left - 4 * u && x <= r.right + 4 * u && y >= r.top - 4 * u && y <= r.bottom + 4 * u) return keyLabels.get(i);
            }
            return null;
        }
    }
}
