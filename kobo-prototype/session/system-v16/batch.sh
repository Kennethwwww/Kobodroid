#!/kdlib/busybox sh
# Fixed trusted commands only. Every Android process inherits the guest syscall filter.
BB=/kdlib/busybox
export PATH=/system/bin:/kdlib
export ANDROID_ROOT=/system ANDROID_DATA=/data
export BOOTCLASSPATH=$($BB sed -n 's/^    export BOOTCLASSPATH //p' /init.environ.rc)
export LD_PRELOAD=/kdlib/libkdnonwakealarm.so:/kdlib/libkdkill.so
[ -e /dev/ashmem ] || export LD_PRELOAD=/kdlib/libkdashmem.so:$LD_PRELOAD
run(){ seconds=$1; shift; /kdlib/run-limit "$seconds" /kdlib/guard "$@"; }
prop(){ run 5 /system/bin/getprop "$1"; }
start(){ run 5 /system/bin/setprop ctl.start "$1"; }
mark(){ echo "BATCH_RESULT $1=$2"; echo "$1=$2" >> /data/batch-results.txt; }
phase(){ echo "$1" > /bridge/phase.tmp && $BB mv /bridge/phase.tmp /bridge/phase; }
service_has(){ run 5 /system/bin/service check "$1" 2>/dev/null | $BB grep -q ': found$'; }
daemon_snapshot(){
 echo "[DAEMON SNAPSHOT $1]"
 $BB ls -ldn /dev /dev/socket /dev/socket/installd /data /data/data /data/user/0 2>&1
 $BB grep -E 'installd|zygote|vold' /proc/net/unix || true
 for name in installd vold zygote system_server; do
  echo "DAEMON name=$name state=$(prop init.svc.$name)"
  for p in $($BB pidof "$name"); do
   echo "DAEMON_PID name=$name pid=$p"
   $BB grep -E '^(Name|State|Uid|Gid|Cap|Seccomp|NoNewPrivs)' /proc/$p/status || true
   $BB cat /proc/$p/wchan /proc/$p/syscall 2>/dev/null || true
   $BB ls -ln /proc/$p/fd 2>/dev/null | $BB head -n 24
  done
 done
}
# Individual results are retained even if a dependent phase cannot run.
echo BATCH_BEGIN
: > /data/batch-results.txt
for i in $($BB seq 1 20); do
 [ "$(prop init.svc.servicemanager)" = running ] && break
 $BB sleep 1
done
sm=$(prop init.svc.servicemanager)
phase services
echo "SERVICEMANAGER_STATE=$sm"
if [ "$sm" != running ]; then
 mark BINDER SKIP_NO_SERVICEMANAGER
 mark SYSTEMSERVER SKIP_NO_SERVICEMANAGER
 mark APK_INSTALL SKIP_NO_SYSTEMSERVER
 echo BATCH_COMPLETE=1
 echo BATCH_OVERALL=PARTIAL
 exit 12
fi
# Drain logd continuously so repeated connection failures cannot prune the
# first daemon error. Three rotating files cap capture at about 1.5 MiB.
$BB rm -f /data/live-boot.txt /data/live-boot.txt.1 /data/live-boot.txt.2
# Warnings and crashes only (plus ActivityManager): a full log keeps the eMMC busy and costs battery.
/kdlib/guard /system/bin/logcat -b main -b system -b crash -v threadtime -f /data/live-boot.txt -r 512 -n 2 "*:W" ActivityManager:I AndroidRuntime:E KobodroidLauncher:I &
# Actual Android boot services, offline and with a memory-only graphics HAL.
for svc in surfaceflinger installd keystore gatekeeperd audioserver media mediaextractor mediacodec kdhealthd; do start "$svc"; done
$BB sleep 2
daemon_snapshot before-zygote
run 15 /kdlib/installer-probe
if [ "$?" != 0 ]; then
 # Do not ask init to restart: the host guard deliberately forbids kill().
 # A second read-only probe distinguishes initial readiness from a stuck service.
 echo INSTALLER_DELAYED_RECHECK=1
 $BB sleep 3
 run 15 /kdlib/installer-probe
 echo "INSTALLER_RECHECK_EXIT=$?"
 daemon_snapshot after-installer-recheck
fi
for i in $($BB seq 1 20); do service_has SurfaceFlinger && break; $BB sleep 1; done
service_has SurfaceFlinger && mark SURFACEFLINGER PASS || mark SURFACEFLINGER FAIL
phase zygote
start zygote
$BB awk '{print "SESSION_TIME zygote_requested", $1}' /proc/uptime
ready=0
for i in $($BB seq 1 480); do
 if [ -f /bridge/stop ]; then
  echo BOOT_CANCELLED=1
  exit 12
 fi
 if [ "$((i % 5))" = 0 ]; then
  zygote_crashes=$($BB grep -Ec "Service 'zygote'.*killed by signal (6|11)([^0-9]|$)" /data/init-kmsg.txt)
  if [ "${zygote_crashes:-0}" -ge 3 ]; then
   echo "ZYGOTE_CRASH_LOOP count=$zygote_crashes"
   break
  fi
 fi
 if [ "$((i % 30))" = 0 ]; then
  run 5 /system/bin/logcat -d -b crash -v threadtime >/data/early-crash.txt 2>&1
  fatal=0
  for serverpid in $($BB pidof system_server); do
   $BB awk -v pid="$serverpid" '$3 == pid && /FATAL EXCEPTION IN SYSTEM PROCESS/ {found=1} END {exit !found}' /data/early-crash.txt && fatal=1
  done
  if [ "$fatal" = 1 ]; then echo SYSTEMSERVER_FATAL_DETECTED=1; break; fi
 fi
 if [ "$((i % 30))" = 0 ]; then echo "BOOT_POLL iteration=$i zygote=$(prop init.svc.zygote) boot_completed=$(prop sys.boot_completed)"; fi
 if [ "$(prop sys.boot_completed)" = 1 ] && service_has package && service_has activity && service_has input_method; then
  if run 10 /system/bin/sh -c 'export CLASSPATH=/kdbin/kobo-probe.jar; exec app_process -Xmx64m /kdbin KoboReady'; then ready=1; break; fi
 fi
 $BB sleep 1
done
if [ "$ready" = 1 ]; then
 mark SYSTEMSERVER PASS
 $BB awk '{print "SESSION_TIME framework_ready", $1}' /proc/uptime
 phase framework
 . /kdlib/guest.sh
 session_home
 exit $?
fi
echo BOOT_FAILURE_LOG_BEGIN
run 15 /system/bin/logcat -d -b main -b system -b crash -v threadtime -t 240
echo BOOT_FAILURE_LOG_END
exit 12
