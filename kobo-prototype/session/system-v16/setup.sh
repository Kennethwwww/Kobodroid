#!/bin/sh
set -eu
BASE=$1
BB=$BASE/busybox
A=/tmp/kobo-core-root-$$
mkdir -p "$A"
$BB mount -t tmpfs -o mode=0755,size=48m kobo-core "$A"
cd "$A"
# Keep the host mountpoint/report private, then use normal guest permissions
# for a filesystem shared by Android's multiple UIDs.
echo "GUEST_SETUP_UMASK previous=$(umask) new=0022"
umask 022
$BB tar -xzf "$BASE/root.tar.gz"
# The shared root targets low-memory Kindles. The Kobo has 1 GiB, so give apps phone-sized heaps.
$BB sed -i -e 's/^ro.config.low_ram=.*/ro.config.low_ram=false/' \
    -e 's/^dalvik.vm.heapgrowthlimit=.*/dalvik.vm.heapgrowthlimit=128m/' \
    -e 's/^dalvik.vm.heapsize=.*/dalvik.vm.heapsize=256m/' \
    -e 's/^dalvik.vm.heapminfree=.*/dalvik.vm.heapminfree=2m/' \
    -e 's/^dalvik.vm.heapmaxfree=.*/dalvik.vm.heapmaxfree=8m/' \
    -e 's/^ro.kindleroid.fb.height=.*/ro.kindleroid.fb.height=1680/' \
    -e 's/^ro.kindleroid.fb.fps=.*/ro.kindleroid.fb.fps=20/' default.prop
# fps paces Android's vsync. At the Kindle's 5 every frame of a page change waited 200 ms, and opening a page
# takes about five frames; vsync only runs while something is drawing, so idle use is unchanged.
# Kobodroid launcher (with Android's status bar) and the framework overlay sizing that bar.
$BB cp "$BASE/launcher.apk" vendor/app/KindleroidLauncher/KindleroidLauncher.apk
$BB rm -rf vendor/app/KindleroidLauncher/oat
$BB mkdir -p vendor/overlay
$BB cp "$BASE/framework-overlay.apk" vendor/overlay/KobodroidFramework.apk
# The device Android reports, as in its settings.
$BB sed -i -e 's/^ro.product.brand=.*/ro.product.brand=Kobo/' \
    -e 's/^ro.product.manufacturer=.*/ro.product.manufacturer=Rakuten Kobo/' \
    -e 's/^ro.product.model=.*/ro.product.model=Kobo Libra Colour/' default.prop
grep -q '^ro.product.model=' default.prop || printf 'ro.product.brand=Kobo\nro.product.manufacturer=Rakuten Kobo\nro.product.model=Kobo Libra Colour\n' >> default.prop
zone=$($BB awk 'NR == 1 {print $1}' "$BASE/timezone" 2>/dev/null)
echo "persist.sys.timezone=${zone:-Asia/Shanghai}" >> default.prop
$BB mkdir -p system data dev proc sys kdlib kd-sysctl cache tmp
$BB mount -t tmpfs -o mode=0770,size=16m,nosuid,nodev cache cache
$BB mount -t tmpfs -o mode=0777,size=8m,nosuid,nodev tmp tmp
$BB mkdir -p kd-empty-apps
$BB mount -t tmpfs -o ro,size=4k,mode=0555 kd-empty-apps kd-empty-apps
"$BASE/runner" --image "$BASE/system.img" "$A/system" 1
"$BASE/runner" --image "$BASE/data.img" "$A/data" 0
$BB mount -o remount,noatime "$A/data"
$BB mount --bind kdoverlay/libbinder.so system/lib/libbinder.so
$BB mount -o remount,bind,ro system/lib/libbinder.so
$BB mount --bind kdoverlay/servicemanager system/bin/servicemanager
$BB mount -o remount,bind,ro system/bin/servicemanager
$BB mount --bind "$BASE/services-offline.jar" system/framework/services.jar
$BB mount -o remount,bind,ro system/framework/services.jar
# The overlaid jar must not use the original jar's preoptimized code.
$BB touch kd-empty-services-odex
$BB mount --bind kd-empty-services-odex system/framework/oat/arm/services.odex
$BB mount -o remount,bind,ro system/framework/oat/arm/services.odex
# Hide unneeded image APKs and retain only the in-memory graphics dispatch.
while read app; do [ ! -d "system/$app" ] || $BB mount --bind kd-empty-apps "system/$app"; done < kdboot/hide-apps
$BB mount --bind kdoverlay/handheld_core_hardware.xml system/etc/permissions/handheld_core_hardware.xml
# GLES dispatch with SurfaceFlinger's 1:1 layer path (gles/kd-gles-dispatch.c: keyboard lines no longer break).
$BB cp "$BASE/libGLES_kindleroid.so" vendor/lib/kdgl/libGLES_kindleroid.so
$BB mount --bind vendor/lib/kdgl/libGLES_kindleroid.so system/lib/egl/libGLES_android.so
$BB cp "$BASE/busybox" kdlib/busybox
$BB cp "$BASE/guard" kdlib/guard
$BB cp "$BASE/policy-check" kdlib/policy-check
$BB cp "$BASE/batch.sh" batch.sh
$BB cp "$BASE/run-limit" kdlib/run-limit
$BB cp "$BASE/installer-probe" kdlib/installer-probe
# Kill broker client (gles/kd-kill.c), preloaded into every Android service and app after the existing shims.
$BB cp "$BASE/libkdkill.so" kdlib/libkdkill.so
$BB sed -i 's@^\( *export LD_PRELOAD .*\)$@\1:/kdlib/libkdkill.so@' init.kindle.rc
$BB mount -t tmpfs -o mode=0755,size=8m tmpfs dev
for node in 'null 1 3' 'zero 1 5' 'random 1 8' 'urandom 1 9'; do
 set -- $node
 $BB mknod -m 666 "dev/$1" c "$2" "$3"
done
vnd=$($BB cat /sys/class/misc/vndbinder/dev)
$BB mknod -m 666 dev/binder c "${vnd%:*}" "${vnd#*:}"
if [ -r /sys/class/misc/ashmem/dev ]; then
 ash=$($BB cat /sys/class/misc/ashmem/dev)
 $BB mknod -m 666 dev/ashmem c "${ash%:*}" "${ash#*:}"
else
 $BB sed -i 's@/kdlib/libkdnonwakealarm.so@/kdlib/libkdashmem.so:/kdlib/libkdnonwakealarm.so@' init.kindle.rc
fi
fuse=$($BB cat /sys/class/misc/fuse/dev)
$BB mknod -m 666 dev/fuse c "${fuse%:*}" "${fuse#*:}"
$BB mkdir -p dev/socket dev/__properties__ dev/pts dev/input dev/graphics dev/block
$BB chmod 0755 dev/socket dev/__properties__ dev/input dev/graphics dev/block
$BB mount -t tmpfs -o mode=0755,size=8m,nosuid,nodev,noexec mnt mnt
$BB mkdir -p mnt/runtime/default mnt/runtime/read mnt/runtime/write storage
$BB mount --bind mnt/runtime/default storage
$BB mount -t devpts -o newinstance,ptmxmode=0666,mode=0620,gid=5 devpts dev/pts
$BB ln -s pts/ptmx dev/ptmx
# Property areas are per-session state; never reuse files created by an earlier init.
$BB rm -rf data/properties
$BB mkdir -p data/properties data/dalvik-cache/arm data/local/tmp
# Repair the cache directory retained from the earlier umask077 sessions.
$BB chmod 0771 data/dalvik-cache/arm
KD_BB="$BB" $BB sh kdboot/kd-data-layout.sh data
# Plain private ext4; initialize actual user0 directories without a vold cryptfs handshake.
$BB mkdir -p data/user_de/0 data/system_de/0 data/system_ce/0 data/misc_de/0 data/misc_ce/0
$BB chown 1000:1000 data/user_de data/user_de/0 data/system_de data/system_de/0 data/system_ce data/system_ce/0 data/misc_de data/misc_de/0 data/misc_ce data/misc_ce/0
$BB chmod 0771 data/user_de data/user_de/0
$BB chmod 0770 data/system_de/0 data/system_ce/0 data/misc_de/0 data/misc_ce/0
# vold normally prepares this; without it installd cannot create JIT profile directories.
$BB mkdir -p data/misc/profiles/cur/0
$BB chown 1000:1000 data/misc/profiles/cur/0
$BB chmod 0771 data/misc/profiles/cur/0
# Both Android paths use the same filtered, read-only mount.
$BB mkdir -p dev/kobodroid-books
$BB mount --bind /mnt/onboard dev/kobodroid-books
$BB mount -o remount,bind,ro dev/kobodroid-books
for hidden in .kobo .adds; do
    [ ! -d "dev/kobodroid-books/$hidden" ] || $BB mount -t tmpfs -o ro,size=4k,mode=0555 kd-hidden "dev/kobodroid-books/$hidden"
done
if [ -d data/media/0 ]; then
    $BB mkdir -p data/media/0/Kobo
    $BB chown 1023:1023 data/media/0/Kobo
    $BB mount --rbind dev/kobodroid-books data/media/0/Kobo
fi
$BB chmod 0711 data/properties
$BB mount --bind data/properties dev/__properties__
echo '[property storage diagnostic]'
if "$BASE/fs-probe" "$A/data/properties"; then label_status=0; else label_status=$?; fi
case "$label_status" in
 0) echo PROPERTY_INIT_MODE=original ;;
 95) $BB cp init-kobo-compat init; echo PROPERTY_INIT_MODE=kobo-label-compat ;;
 *) echo PROPERTY_INIT_MODE=refused; exit 11 ;;
esac
echo '[SELinux kernel configuration]'
$BB zcat /proc/config.gz | $BB grep -E '^(# )?CONFIG_(SECURITY_SELINUX|SECURITYFS|EXT4_FS_SECURITY|TMPFS_XATTR|DEFAULT_SECURITY)' || true
echo '[inherited process context]'
$BB cat /proc/self/attr/current 2>/dev/null || true
printf '\n' 
: > data/init-kmsg.txt
$BB touch dev/.coldboot_done
# A regular-file kmsg lets each new logger overwrite offset zero. A private
# pipe has kernel-like write semantics and one append-only collector instead.
$BB mkfifo -m 600 dev/kmsg
$BB cat dev/kmsg >> data/init-kmsg.txt &
$BB mount -t proc -o nosuid,nodev,noexec proc proc
# Hide global control trees and existing host processes. New guest processes need writable attr/exec.
$BB mkdir -p kd-masked
$BB mount --bind kd-masked proc/sys
for entry in sysrq-trigger kcore kmsg keys timer_list; do
 if [ -e "proc/$entry" ]; then
  $BB touch "kd-masked/$entry"
  $BB mount --bind "kd-masked/$entry" "proc/$entry"
 fi
done
$BB mount --bind kdboot/cmdline proc/cmdline
for entry in irq fs bus scsi; do
 if [ -d "proc/$entry" ]; then $BB mount --bind kd-masked "proc/$entry"; fi
done
$BB mkdir -p kd-empty
$BB mount -t tmpfs -o ro,size=4k,mode=0555 kd-empty kd-empty
for hostpid in proc/[0-9]*; do
 [ -d "$hostpid" ] || continue
 $BB mount --bind kd-empty "$hostpid" 2>/dev/null || true
done
# Init must read back its own entropy settings; these are private regular files.
$BB mkdir -p proc/sys/vm proc/sys/kernel
printf '16\n' > proc/sys/vm/mmap_rnd_bits
printf '16\n' > proc/sys/vm/mmap_rnd_compat_bits
printf '2\n' > proc/sys/kernel/kptr_restrict
$BB mkdir -p .old
if [ -f "$BASE/install.apk" ]; then
    apk_size=$($BB stat -c %s "$BASE/install.apk")
    if [ "$apk_size" -gt 0 ] && [ "$apk_size" -le 134217728 ]; then
        $BB touch kd-install.apk
        $BB mount --bind "$BASE/install.apk" kd-install.apk
        $BB mount -o remount,bind,ro kd-install.apk
    else
        echo APK_IMPORT_REFUSED_SIZE
    fi
fi
$BB mkdir -m 0700 bridge
case "${KOBO_BRIDGE_DIR:-}" in /tmp/kobo-android.*/bridge) ;; *) exit 16;; esac
$BB mount --bind "$KOBO_BRIDGE_DIR" bridge
# Bind the channel before entering Android. The launcher UID is assigned by Package Manager.
$BB mkdir -m 0700 "$KOBO_BRIDGE_DIR/ui"
$BB mkdir -p dev/kobodroid-ui/channel
$BB mount --bind "$KOBO_BRIDGE_DIR/ui" dev/kobodroid-ui/channel
# APK folder on the Kobo storage: installed at the next start, then moved to installed/ or failed/.
$BB mkdir -p /mnt/onboard/Kobodroid-APK
$BB mkdir -p kd-apk
$BB mount --bind /mnt/onboard/Kobodroid-APK kd-apk
$BB cp "$BASE/guest.sh" kdlib/guest.sh
$BB cp "$BASE/session-input.jar" kdlib/session-input.jar
$BB cp "$BASE/capture-frame" kdlib/capture-frame
# Expose only read-only battery attributes to the existing Android health service.
for supply in bd71827_bat bd71827_ac; do
    source=/sys/class/power_supply/$supply
    [ -r "$source/type" ] || continue
    target=sys/class/power_supply/$supply
    $BB mkdir -p "$target"
    for attribute in type capacity status voltage_now temp online; do
        [ -r "$source/$attribute" ] || continue
        $BB touch "$target/$attribute"
        $BB mount --bind "$source/$attribute" "$target/$attribute"
        $BB mount -o remount,bind,ro "$target/$attribute"
    done
done
$BB pivot_root . .old
cd /
/kdlib/busybox umount -l /.old
/kdlib/busybox rmdir /.old
# Propagation is confined to this detached guest tree, for Android storage namespaces.
/kdlib/busybox mount --make-rshared /
# Rebind after sharing: the early private bind would otherwise have a different peer group.
/kdlib/busybox umount /storage
/kdlib/busybox mount --bind /mnt/runtime/default /storage
/kdlib/busybox mount --make-rslave /storage
/kdlib/guard /kdlib/policy-check
/kdlib/guard /kdlib/busybox env -i PATH=/system/bin:/kdlib ANDROID_ROOT=/system ANDROID_DATA=/data /init --second-stage &
initpid=$!
set +e
/kdlib/busybox sh /batch.sh
batch_status=$?
printf '\n[INIT LOG TAIL]\n'
/kdlib/busybox tail -c 50000 /data/init-kmsg.txt
exit "$batch_status"
