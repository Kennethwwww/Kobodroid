"""Port the V73 framework integration to the Android 7.0 AOSP sources."""
import argparse
from pathlib import Path

SERVICES = Path("frameworks/base/services")
CORE = SERVICES / "core/java/com/android/server"
PATCHES = {
    Path("frameworks/base/core/jni/fd_utils-inl.h"): (
        ('  "/system/framework/framework-res.apk",',
         '  "/system/framework/framework-res.apk",\n'
         '  "/vendor/overlay/KobodroidFramework.apk",'),
        ('  "/vendor/overlay/KobodroidFramework.apk",',
         '  "/vendor/overlay/KobodroidFramework.apk",\n'
         '  "/data/resource-cache/vendor@overlay@KobodroidFramework.apk@idmap",'),
    ),
    CORE / "content/ContentService.java": (
        ("return syncManager.getSyncAdapterPackagesForAuthorityAsUser(authority, userId);",
         "return syncManager == null ? new String[0]\n"
         "                    : syncManager.getSyncAdapterPackagesForAuthorityAsUser(authority, userId);"),
    ),
    CORE / "job/JobSchedulerService.java": (
        ("        mControllers.add(ConnectivityController.get(this));",
         '        if (!android.os.SystemProperties.getBoolean("config.disable_network", false)) {\n'
         "            mControllers.add(ConnectivityController.get(this));\n"
         "        }"),
    ),
    CORE / "connectivity/MetricsLoggerService.java": (
        ("            mDnsListener = new DnsEventListenerService(getContext());\n"
         "            publishBinderService(mDnsListener.SERVICE_NAME, mDnsListener);",
         '            if (!android.os.SystemProperties.getBoolean("config.disable_network", false)) {\n'
         "                mDnsListener = new DnsEventListenerService(getContext());\n"
         "                publishBinderService(mDnsListener.SERVICE_NAME, mDnsListener);\n"
         "            }"),
    ),
    CORE / "am/ProcessList.java": (
        ("    private static void writeLmkd(ByteBuffer buf) {",
         "    private static void writeLmkd(ByteBuffer buf) {\n"
         '        if (SystemProperties.getBoolean("ro.kindleroid.host_memory_guard", false)) {\n'
         "            return;\n"
         "        }"),
    ),
    SERVICES / "java/com/android/server/SystemServer.java": (
        ("            networkPolicy.bindNotificationManager(notification);",
         "            if (networkPolicy != null) {\n"
         "                networkPolicy.bindNotificationManager(notification);\n"
         "            }"),
    ),
}


def patch(root):
    changes = {}
    for filename, edits in PATCHES.items():
        source = root / filename
        before = source.read_text()
        after = before
        for old, new in edits:
            if after.count(new) == 1:
                continue
            if after.count(old) != 1:
                raise ValueError(f"Unexpected Android source: {filename}")
            after = after.replace(old, new)
        if before != after:
            changes[source] = after
    for source, text in changes.items():
        source.write_text(text)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    patch(parser.parse_args().root)
