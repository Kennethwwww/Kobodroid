package kindleroid.launcher;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.ColorMatrix;
import android.graphics.ColorMatrixColorFilter;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.os.BatteryManager;
import android.os.Bundle;
import android.provider.Settings;
import android.text.TextUtils;
import android.util.Log;
import android.util.TypedValue;
import android.view.GestureDetector;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.GridLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.text.Collator;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.HashSet;

/**
 * Home screen for the Kobo's e-ink panel: a paged app grid (fixed cells, no scrolling, no animation),
 * grayscale icons, a header with clock and battery, and a long-press menu per app. Every change is a plain
 * redraw so the display bridge refreshes only what changed.
 */
public class HomeActivity extends Activity {
    static final String TAG = "KobodroidLauncher";
    private static final int ICON_SAMPLE_SIZE = 128;

    private final List<App> apps = new ArrayList<App>();
    private int page;
    private TextView sectionTitle;
    private boolean active, appsDirty = true, gridDirty = true, lastShowHidden;
    private Set<String> lastHidden = new HashSet<String>();
    private int drawnPage = -1, drawnCols, drawnWidth, drawnHeight;

    private GridLayout grid;
    private TextView clock, battery, pageLabel, emptyTitle,emptyNote;
    private View empty;
    private ImageView hostWifi;
    private HostSettings.Watch hostStateObserver;
    private Button prev, next, installHelp;
    private LinearLayout pager;
    private GestureDetector swipe;

    private final BroadcastReceiver packagesChanged = new BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent i) { appsDirty = true; if (active) reload(); }
    };
    private final BroadcastReceiver statusChanged = new BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent i) { updateStatus(i); }
    };

    static final class App {
        final ComponentName component;
        final String label;
        final ResolveInfo info;
        final boolean system;
        Bitmap artwork;
        int artworkSize;
        App(ComponentName component, String label, ResolveInfo info, boolean system) {
            this.component = component; this.label = label; this.info = info; this.system = system;
        }
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setWindowAnimations(0);
        startService(new Intent(this, StatusBar.class));
        PermissionGranter.grantAll(this);
        setContentView(buildLayout());

        IntentFilter f = new IntentFilter();
        f.addAction(Intent.ACTION_PACKAGE_ADDED);
        f.addAction(Intent.ACTION_PACKAGE_REMOVED);
        f.addAction(Intent.ACTION_PACKAGE_CHANGED);
        f.addDataScheme("package");
        registerReceiver(packagesChanged, f);

        swipe = new GestureDetector(this, new GestureDetector.SimpleOnGestureListener() {
            @Override public boolean onFling(MotionEvent a, MotionEvent b, float vx, float vy) {
                if (a == null || b == null) return false;
                float dx = b.getX() - a.getX(), dy = b.getY() - a.getY();
                if (Math.abs(dx) < dp(60) || Math.abs(dx) < Math.abs(dy) * 1.5f) return false;
                if (!Prefs.get(HomeActivity.this).getBoolean(Prefs.SWIPE_PAGING, true)) return false;
                showPage(page + (dx < 0 ? 1 : -1));
                return true;
            }
        });
        Log.i(TAG, "LAUNCHER_CREATED");
    }

    @Override public void onConfigurationChanged(android.content.res.Configuration config) {
        super.onConfigurationChanged(config);
        gridDirty = true;
        setContentView(buildLayout());
    }

    @Override protected void onResume() {
        super.onResume();
        active = true;
        IntentFilter f = new IntentFilter();
        f.addAction(Intent.ACTION_TIME_TICK);
        f.addAction(Intent.ACTION_TIME_CHANGED);
        f.addAction(Intent.ACTION_TIMEZONE_CHANGED);
        f.addAction(Intent.ACTION_BATTERY_CHANGED);
        updateStatus(registerReceiver(statusChanged, f));
        hostStateObserver=HostSettings.observe(this,new Runnable(){public void run(){if(active)updateStatus(null);}});
        try{HostSettings.request(this,"query",0);}catch(java.io.IOException e){}
        reload();  // the hidden-app and column settings may have changed
    }

    @Override protected void onPause() {
        active = false;
        if(hostStateObserver!=null){hostStateObserver.stopWatching();hostStateObserver=null;}
        unregisterReceiver(statusChanged);
        super.onPause();
    }

    @Override protected void onDestroy() {
        unregisterReceiver(packagesChanged);
        super.onDestroy();
    }

    @Override public void onBackPressed() {
        showPage(0);  // home has nowhere to go back to
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        showPage(0);  // HOME pressed while already home
    }

    @Override public boolean dispatchTouchEvent(MotionEvent ev) {
        if (swipe != null && swipe.onTouchEvent(ev)) return true;
        return super.dispatchTouchEvent(ev);
    }

    private View buildLayout() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.WHITE);

        LinearLayout top = new LinearLayout(this);
        top.setGravity(Gravity.CENTER_VERTICAL);top.setPadding(dp(28),0,dp(12),dp(12));
        sectionTitle=text("应用",28);sectionTitle.setTypeface(Typeface.create("serif",Typeface.BOLD));top.addView(sectionTitle,new LinearLayout.LayoutParams(0,dp(48),1f));
        View more=UiStyle.settingsButton(this);
        more.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { start(new Intent(HomeActivity.this, SettingsActivity.class)); }
        });
        top.addView(more,new LinearLayout.LayoutParams(dp(48),dp(48)));
        root.addView(top,new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,dp(70)));
        android.widget.FrameLayout body = new android.widget.FrameLayout(this);
        body.setPadding(dp(24),dp(20),dp(24),0);
        grid = new GridLayout(this);
        grid.setUseDefaultMargins(false);
        body.addView(grid, new android.widget.FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        LinearLayout emptyBox=new LinearLayout(this);emptyBox.setOrientation(LinearLayout.VERTICAL);emptyBox.setGravity(Gravity.CENTER);ImageView emptyIcon=new ImageView(this);emptyIcon.setImageDrawable(UiStyle.icon(this,"book"));emptyBox.addView(emptyIcon,new LinearLayout.LayoutParams(dp(84),dp(84)));emptyTitle=text("添加应用",26);LinearLayout.LayoutParams et=new LinearLayout.LayoutParams(-2,-2);et.topMargin=dp(24);emptyBox.addView(emptyTitle,et);emptyNote=text("复制 APK，下次进入时安装。",19);emptyNote.setTextColor(0xff333333);LinearLayout.LayoutParams en=new LinearLayout.LayoutParams(-2,-2);en.topMargin=dp(14);emptyBox.addView(emptyNote,en);installHelp=flatButton("安装说明");installHelp.setOnClickListener(new View.OnClickListener(){public void onClick(View v){start(new Intent(HomeActivity.this,InstallActivity.class));}});emptyBox.addView(installHelp,new LinearLayout.LayoutParams(dp(220),dp(54)));empty=emptyBox;
        empty.setVisibility(View.GONE);
        body.addView(empty, new android.widget.FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        root.addView(body, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        grid.addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
            @Override public void onLayoutChange(View v, int l, int t, int r, int b, int ol, int ot, int or, int ob) {
                // Children added during a layout pass are laid out only in the next one.
                if (r - l != or - ol || b - t != ob - ot) {
                    grid.post(new Runnable() { @Override public void run() { showPage(page); } });
                }
            }
        });

        LinearLayout bottom = new LinearLayout(this);
        pager=bottom;
        bottom.setGravity(Gravity.CENTER_VERTICAL);
        bottom.setVisibility(View.GONE);
        prev = flatButton("◀ 上一页");
        next = flatButton("下一页 ▶");
        pageLabel = text("", 16);
        pageLabel.setGravity(Gravity.CENTER);
        prev.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { showPage(page - 1); }
        });
        next.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { showPage(page + 1); }
        });
        bottom.addView(prev, weighted());
        bottom.addView(pageLabel, weighted());
        bottom.addView(next, weighted());
        root.addView(bottom, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return root;
    }

    private void reload() {
        PackageManager pm = getPackageManager();
        applyDefaultHidden(pm);
        Set<String> hidden = Prefs.hidden(this);
        boolean showHidden = Prefs.get(this).getBoolean(Prefs.SHOW_HIDDEN, false);
        if (!appsDirty && hidden.equals(lastHidden) && showHidden == lastShowHidden) { showPage(page); return; }
        Intent main = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
        List<ResolveInfo> found = pm.queryIntentActivities(main, 0);
        appsDirty = false;
        gridDirty = true;
        lastHidden = new HashSet<String>(hidden);
        lastShowHidden = showHidden;
        apps.clear();
        for (ResolveInfo r : found) {
            if (getPackageName().equals(r.activityInfo.packageName)) continue;
            if (Prefs.isDiagnosticPackage(r.activityInfo.packageName)) continue;
            ComponentName cn = new ComponentName(r.activityInfo.packageName, r.activityInfo.name);
            if (!showHidden && hidden.contains(cn.flattenToString())) continue;
            CharSequence label = r.loadLabel(pm);
            boolean system = (r.activityInfo.applicationInfo.flags & ApplicationInfo.FLAG_SYSTEM) != 0;
            String name=label==null?cn.getPackageName():label.toString();
            apps.add(new App(cn,name,r,system));
        }
        final Collator collator = Collator.getInstance(Locale.CHINA);
        Collections.sort(apps, new Comparator<App>() {
            @Override public int compare(App a,App b){return collator.compare(a.label,b.label);}
        });
        StringBuilder order = new StringBuilder();
        for (App a : apps) order.append(order.length() == 0 ? "" : " | ").append(a.label);
        Log.i(TAG, "LAUNCHER_APPS count=" + apps.size() + " hidden=" + hidden.size() + " order=[" + order + "]");
        showPage(page);
    }

    /** First start only: hide the default set (e.g. Android settings) so the grid holds just the reader apps. */
    private void applyDefaultHidden(PackageManager pm) {
        SharedPreferences p = Prefs.get(this);
        if (p.getBoolean(Prefs.DEFAULTS_APPLIED, false)) return;
        Set<String> hidden = Prefs.hidden(this);
        Intent main = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
        for (String pkg : Prefs.DEFAULT_HIDDEN_PACKAGES) {
            for (ResolveInfo r : pm.queryIntentActivities(new Intent(main).setPackage(pkg), 0)) {
                hidden.add(new ComponentName(r.activityInfo.packageName, r.activityInfo.name).flattenToString());
            }
        }
        p.edit().putStringSet(Prefs.HIDDEN_APPS, hidden).putBoolean(Prefs.DEFAULTS_APPLIED, true).apply();
    }

    private void showPage(int requested) {
        if (grid == null || grid.getWidth() == 0) return;
        int cols = Prefs.columns(this);
        int gap = dp(16), rowGap = dp(12), cellH = dp(128);
        int availableWidth = grid.getWidth() - gap * (cols - 1);
        int cellW = availableWidth / cols, extra = availableWidth % cols;
        int rows = Math.max(1, (grid.getHeight() + rowGap) / (cellH + rowGap));
        int perPage = cols * rows;
        int pages = Math.max(1, (apps.size() + perPage - 1) / perPage);
        int target = Math.max(0, Math.min(requested, pages - 1));
        if (!gridDirty && target == drawnPage && cols == drawnCols
                && grid.getWidth() == drawnWidth && grid.getHeight() == drawnHeight) return;
        page = target;
        gridDirty = false;
        drawnPage = page; drawnCols = cols; drawnWidth = grid.getWidth(); drawnHeight = grid.getHeight();

        grid.removeAllViews();
        grid.setColumnCount(cols);
        grid.setRowCount(rows);
        int end = Math.min(apps.size(), (page + 1) * perPage);
        for (int i = page * perPage; i < end; i++) {
            GridLayout.LayoutParams lp = new GridLayout.LayoutParams();
            int index = i - page * perPage;
            int column = index % cols;
            lp.width = cellW + (column < extra ? 1 : 0);
            lp.height = cellH;
            lp.columnSpec = GridLayout.spec(column);
            lp.rowSpec = GridLayout.spec(index / cols);
            lp.rightMargin = column < cols - 1 ? gap : 0;
            lp.bottomMargin = rowGap;
            grid.addView(cell(apps.get(i)), lp);
        }
        empty.setVisibility(apps.isEmpty() ? View.VISIBLE : View.GONE);
        pager.setVisibility(pages>1?View.VISIBLE:View.GONE);
        pageLabel.setText((page + 1) + " / " + pages);
        prev.setEnabled(page > 0);
        next.setEnabled(page < pages - 1);
        prev.setTextColor(page > 0 ? Color.BLACK : Color.LTGRAY);
        next.setTextColor(page < pages - 1 ? Color.BLACK : Color.LTGRAY);
        Log.i(TAG, "LAUNCHER_PAGE page=" + (page + 1) + "/" + pages + " cols=" + cols + " cell=" + cellW + "x" + cellH
                + " grid_top=" + gridTop());
    }

    private int gridTop() {
        int[] at = new int[2];
        grid.getLocationOnScreen(at);
        return at[1];
    }

    private Bitmap appArtwork(App app, int size) {
        if (app.artwork != null && app.artworkSize == size) return app.artwork;

        Drawable drawable = app.info.loadIcon(getPackageManager());
        int width = Math.max(1, drawable.getIntrinsicWidth());
        int height = Math.max(1, drawable.getIntrinsicHeight());
        float scale = (float) ICON_SAMPLE_SIZE / Math.max(width, height);
        Bitmap sample = Bitmap.createBitmap(Math.max(1, Math.round(width * scale)),
                Math.max(1, Math.round(height * scale)), Bitmap.Config.ARGB_8888);
        drawable.setBounds(0, 0, sample.getWidth(), sample.getHeight());
        drawable.draw(new Canvas(sample));

        int sampleW = sample.getWidth(), sampleH = sample.getHeight();
        int[] pixels = new int[sampleW * sampleH];
        sample.getPixels(pixels, 0, sampleW, 0, 0, sampleW, sampleH);
        Rect ink = new Rect(sampleW, sampleH, 0, 0);
        for (int y = 0; y < sampleH; y++) {
            for (int x = 0; x < sampleW; x++) {
                if (Color.alpha(pixels[y * sampleW + x]) < 32) continue;
                ink.left = Math.min(ink.left, x);
                ink.top = Math.min(ink.top, y);
                ink.right = Math.max(ink.right, x + 1);
                ink.bottom = Math.max(ink.bottom, y + 1);
            }
        }
        if (ink.isEmpty()) ink.set(0, 0, sampleW, sampleH);
        float fit = (float) size / Math.max(ink.width(), ink.height());
        float insetX = (size - ink.width() * fit) / 2;
        float insetY = (size - ink.height() * fit) / 2;
        RectF destination = new RectF(insetX, insetY, size - insetX, size - insetY);
        ColorMatrix gray = new ColorMatrix();
        gray.setSaturation(0f);
        Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
        paint.setColorFilter(new ColorMatrixColorFilter(gray));
        app.artwork = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        new Canvas(app.artwork).drawBitmap(sample, ink, destination, paint);
        sample.recycle();
        app.artworkSize = size;
        return app.artwork;
    }

    private View cell(final App app) {
        LinearLayout v = new LinearLayout(this);
        v.setOrientation(LinearLayout.VERTICAL);
        v.setGravity(Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        v.setPadding(0, dp(8), 0, 0);

        android.widget.FrameLayout badge = new android.widget.FrameLayout(this);
        badge.setBackground(UiStyle.surface(UiStyle.CONTROL_FILL, 32, this));
        ImageView icon = new ImageView(this);
        int artworkSize = dp(44);
        icon.setImageBitmap(appArtwork(app, artworkSize));
        badge.addView(icon, new android.widget.FrameLayout.LayoutParams(artworkSize, artworkSize, Gravity.CENTER));
        v.addView(badge, new LinearLayout.LayoutParams(dp(64), dp(64)));

        TextView label = text(app.label, 18);
        label.setGravity(Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        label.setLines(2);
        label.setEllipsize(TextUtils.TruncateAt.END);
        LinearLayout.LayoutParams labelSlot = new LinearLayout.LayoutParams(-1, -2);
        labelSlot.topMargin = dp(10);
        v.addView(label, labelSlot);
        v.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { launch(app); }
        });
        v.setOnLongClickListener(new View.OnLongClickListener() {
            @Override public boolean onLongClick(View view) { view.setBackgroundColor(Color.TRANSPARENT); showAppMenu(app); return true; }
        });
        return v;
    }

    private void launch(App app) {
        Log.i(TAG, "LAUNCH " + app.component.flattenToShortString());
        Intent i = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
                .setComponent(app.component)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED);
        start(i);
    }

    private void showAppMenu(final App app) {
        final boolean hidden = Prefs.hidden(this).contains(app.component.flattenToString());
        final List<String> labels = new ArrayList<String>();
        final List<Integer> actions = new ArrayList<Integer>();
        labels.add("应用信息"); actions.add(1);
        if (!app.system) { labels.add("卸载"); actions.add(2); }
        labels.add(hidden ? "取消隐藏" : "在桌面上隐藏"); actions.add(3);
        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle(app.label)
                .setItems(labels.toArray(new String[0]), new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int which) {
                        Uri pkg = Uri.fromParts("package", app.component.getPackageName(), null);
                        switch (actions.get(which)) {
                            case 1: start(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, pkg)); break;
                            case 2: start(new Intent(Intent.ACTION_DELETE, pkg)); break;
                            case 3:
                                Prefs.setHidden(HomeActivity.this, app.component.flattenToString(), !hidden);
                                reload();
                                break;
                        }
                    }
                })
                .create();
        UiStyle.showFlat(dialog);
    }

    private void updateStatus(Intent battery) {}

    private void start(Intent i) {
        try {
            startActivity(i);
            overridePendingTransition(0, 0);
        } catch (ActivityNotFoundException | SecurityException e) {
            Toast.makeText(this, "无法打开：" + e.getClass().getSimpleName(), Toast.LENGTH_SHORT).show();
        }
    }

    private TextView text(String s, int sp) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextColor(Color.BLACK);t.setIncludeFontPadding(false);
        t.setTextSize(TypedValue.COMPLEX_UNIT_PX, dp(sp));
        return t;
    }

    private Button flatButton(String s) {
        Button b = new Button(this, null, android.R.attr.borderlessButtonStyle);
        b.setText(s);
        b.setAllCaps(false);
        b.setTextColor(Color.BLACK);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        b.setStateListAnimator(null);
        b.setBackground(null);  // no ripple: every ripple frame would be a panel refresh
        b.setMinHeight(dp(52));
        return b;
    }

    private static LinearLayout.LayoutParams weighted() {
        return new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
    }

    private int dp(int v) {
        return UiStyle.px(this,v);
    }
}
