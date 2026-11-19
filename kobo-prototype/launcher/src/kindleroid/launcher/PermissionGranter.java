package kindleroid.launcher;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.Process;
import android.os.UserHandle;
import android.util.Log;
import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.List;

/** Grants the runtime permissions an e-reader setup needs, as a user would: Android's permission dialog cannot be
 *  answered here. Shared storage (/sdcard, including /sdcard/Kobo) and phone state, which many apps check at start
 *  and quit without; this Kobo has no telephony, so it only tells them so. Only permissions an app requests are
 *  granted, when it is installed and once for every app when the launcher starts. The launcher is platform-signed,
 *  which allows GRANT_RUNTIME_PERMISSIONS. */
public final class PermissionGranter extends BroadcastReceiver {
    private static final List<String> GRANTED = Arrays.asList(
        "android.permission.READ_EXTERNAL_STORAGE",
        "android.permission.WRITE_EXTERNAL_STORAGE",
        "android.permission.READ_PHONE_STATE");

    @Override public void onReceive(Context context, Intent intent) {
        if (intent.getData() != null) grant(context, intent.getData().getSchemeSpecificPart());
    }

    static void grant(Context context, String pkg) {
        PackageManager pm = context.getPackageManager();
        try {
            PackageInfo info = pm.getPackageInfo(pkg, PackageManager.GET_PERMISSIONS);
            if (info.requestedPermissions == null || (info.applicationInfo.flags & ApplicationInfo.FLAG_SYSTEM) != 0) return;
            Method grant = PackageManager.class.getMethod("grantRuntimePermission", String.class, String.class, UserHandle.class);
            for (String permission : info.requestedPermissions) {
                if (!GRANTED.contains(permission) || pm.checkPermission(permission, pkg) == PackageManager.PERMISSION_GRANTED) continue;
                grant.invoke(pm, pkg, permission, Process.myUserHandle());
                Log.i(HomeActivity.TAG, "PERMISSION_GRANTED package=" + pkg + " permission=" + permission);
            }
        } catch (Exception e) {
            Log.w(HomeActivity.TAG, "PERMISSION_GRANT_FAILED package=" + pkg, e);
        }
    }

    static void grantAll(final Context context) {
        new Thread(new Runnable() {
            @Override public void run() {
                for (PackageInfo info : context.getPackageManager().getInstalledPackages(0)) grant(context, info.packageName);
            }
        }, "PermissionGranter").start();
    }
}
