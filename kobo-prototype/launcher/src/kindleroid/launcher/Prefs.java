package kindleroid.launcher;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.HashSet;
import java.util.Set;

/** Launcher settings, kept in one SharedPreferences file. Light, natural light and Return to Kobo live in the
 *  control centre drawn by the Kobodroid panel; panel settings go through HostSettings. */
final class Prefs {
    static final String FILE = "kindleroid_launcher";

    static final String SWIPE_PAGING = "swipe_paging";
    static final String SHOW_HIDDEN = "show_hidden";
    static final String HIDDEN_APPS = "hidden_apps";
    static final String COLUMNS = "columns";
    static final String DEFAULTS_APPLIED = "defaults_applied_v2";

    /** Hidden from the grid on first start (the user can show them again): Android's own settings, which stay
     *  reachable from 桌面设置. */
    static final String[] DEFAULT_HIDDEN_PACKAGES = { "com.android.settings" };
    static boolean isDiagnosticPackage(String name) {
        return "kindleroid.installprobe7".equals(name) || "kindleroid.koboprobe".equals(name)
                || "kindleroid.samplereader".equals(name);
    }

    private Prefs() {}

    static SharedPreferences get(Context c) {
        return c.getSharedPreferences(FILE, Context.MODE_PRIVATE);
    }

    static int columns(Context c) {
        int n = get(c).getInt(COLUMNS, 4);
        return n < 3 ? 3 : n > 5 ? 5 : n;
    }

    static Set<String> hidden(Context c) {
        return new HashSet<String>(get(c).getStringSet(HIDDEN_APPS, new HashSet<String>()));
    }

    static void setHidden(Context c, String component, boolean hide) {
        Set<String> s = hidden(c);
        if (hide) s.add(component); else s.remove(component);
        get(c).edit().putStringSet(HIDDEN_APPS, s).apply();
    }
}
