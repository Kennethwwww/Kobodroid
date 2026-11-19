#!/bin/sh
set -u
umask 077
if [ "${1:-freeze}" = freeze ]; then export KOBO_FREEZE_NICKEL=1; else unset KOBO_FREEZE_NICKEL; fi
BASE=/mnt/onboard/.adds/kobodroid
mkdir -p "$BASE/reports" || exit 1
OUT="$BASE/reports/batch-$(date +%Y%m%d-%H%M%S)-$$.txt"
exec >"$OUT" 2>&1
echo KOBODROID_V73
echo "NICKEL_HANDOFF_REQUESTED=${KOBO_FREEZE_NICKEL:-0}"
printf 'SESSION_TIME entry %s\n' "$(cut -d ' ' -f1 /proc/uptime)"
date -u
awk -F, '{print "model=" substr($1,1,4),"kernel=" $2,"firmware=" $3}' /mnt/onboard/.kobo/version
case "$(cut -d, -f3 /mnt/onboard/.kobo/version)" in 4.45.23646) ;; *) echo 'REFUSED firmware'; exit 1;; esac
case "$(cut -c1-4 /mnt/onboard/.kobo/version)" in N428) ;; *) echo 'REFUSED model'; exit 1;; esac
cd "$BASE" || exit 1
exec 9>.deployment.lock
# Nickel is already visible during the last seconds of a normal session's cleanup.
waited=0
until /usr/bin/flock -n 9; do
    [ "$waited" -lt 5 ] || { echo 'REFUSED deployment or session active'; exit 1; }
    [ "$waited" -ne 0 ] || echo 'SESSION_LOCK=WAIT cleanup or deployment'
    sleep 1
    waited=$((waited + 1))
done
[ "$waited" -eq 0 ] || echo "SESSION_LOCK=ACQUIRED waited_s=$waited"
[ ! -d .deploy-pending ] || { echo 'REFUSED interrupted deployment'; exit 1; }
rm -f "$BASE/cancel.request"
# The 584 MB system image is hashed once; later starts compare its size and time with that verification.
stamp=$("$BASE/busybox" stat -c '%s %Y' system.img)
if [ "$(cat system.img.verified 2>/dev/null)" = "$stamp" ]; then
    "$BASE/busybox" grep -v '  system.img$' checksums.sha256 > "$BASE/checksums.fast"
    sums="$BASE/checksums.fast"
    echo "SYSTEM_IMAGE_VERIFIED=cached"
else
    rm -f system.img.verified
    sums=checksums.sha256
fi
if ! "$BASE/busybox" sha256sum -c "$sums"; then echo 'REFUSED package checksum'; exit 1; fi
[ "$sums" != checksums.sha256 ] || printf '%s\n' "$stamp" > system.img.verified
. "$BASE/zram.sh"
zram_restore || { echo ZRAM_RESTORE=FAIL; exit 1; }
"$BASE/busybox" sh "$BASE/system-survey.sh" "$BASE"
snapshot() {
 for n in nickel sickel hindenburg; do
  for p in $(pidof "$n"); do
   [ -r "/proc/$p/stat" ] || continue
   awk -v name="$n" '{print name,$1,$22}' "/proc/$p/stat"
  done
 done
}
host_state() {
 echo MOUNTS
 cat /proc/mounts
 echo IPV4_RULES
 ip rule show 2>/dev/null || true
 echo IPV4_ROUTES
 ip -4 route show table all 2>/dev/null || true
 echo IPV6_RULES
 ip -6 rule show 2>/dev/null || true
 echo IPV6_ROUTES
 ip -6 route show table all 2>/dev/null | sed 's/ expires [0-9][0-9]*sec//g' || true
 echo SWAP
 cat /proc/swaps
 echo HOST_TUNABLES
 for f in /proc/sys/vm/mmap_rnd_bits /proc/sys/vm/mmap_rnd_compat_bits /proc/sys/kernel/kptr_restrict /proc/sys/kernel/randomize_va_space /proc/sys/net/ipv4/ip_forward; do
  [ ! -r "$f" ] || { printf '%s ' "$f"; cat "$f"; }
 done
 echo LOOP_BINDINGS
 for f in /sys/block/loop*/loop/backing_file; do [ ! -f "$f" ] || { printf '%s ' "$f"; cat "$f"; }; done
}
snapshot > "$OUT.native-before"
host_state > "$OUT.host-before"
"$BASE/busybox" sha256sum "$OUT.host-before" | cut -d' ' -f1 > "$OUT.state-before"
work=$("$BASE/busybox" mktemp -d /tmp/kobo-android.XXXXXX) || exit 1
mkdir -m 0700 "$work/bridge" || exit 1
export KOBO_BRIDGE_DIR="$work/bridge"
trap 'touch "$work/bridge/stop"; wait "$host_pid"; zram_restore; rm -rf "$work"' EXIT
/bin/sh "$BASE/host.sh" "$BASE" "$work" 9>&- &
host_pid=$!
printf 'SESSION_TIME runner_start %s\n' "$(cut -d ' ' -f1 /proc/uptime)"
/bin/sh "$BASE/power.sh" battery start
zram_trial=${KOBO_ZRAM:-0}
# Allow the next ordinary NickelMenu launch to run the same one-session trial.
if [ -f "$BASE/.zram-next" ] && rm -f "$BASE/.zram-next"; then
    zram_trial=1
fi
if [ "$zram_trial" = 1 ] && ! zram_start; then
    echo ZRAM_ENABLED=UNAVAILABLE
    zram_restore || { echo ZRAM_RESTORE=FAIL; exit 1; }
fi
"$BASE/runner" "$BASE" 9>&-
result=$?
touch "$work/bridge/stop"
wait "$host_pid"
echo "HOST_PANEL_EXIT=$?"
echo RUNNER_EXIT=$result
zram_restore || echo ZRAM_RESTORE=FAIL
/bin/sh "$BASE/power.sh" battery exit
snapshot > "$OUT.native-after"
# Let deferred loop AUTOCLEAR and Binder release finish before comparing.
sleep 2
host_state > "$OUT.host-after"
"$BASE/busybox" sha256sum "$OUT.host-after" | cut -d' ' -f1 > "$OUT.state-after"
if cmp -s "$OUT.native-before" "$OUT.native-after"; then echo NATIVE_PROCESS_IDENTITIES=UNCHANGED; else echo NATIVE_PROCESS_IDENTITIES=CHANGED; fi
if cmp -s "$OUT.state-before" "$OUT.state-after"; then echo HOST_STATE_COMPARISON=UNCHANGED; else echo HOST_STATE_COMPARISON=DIFFERENT; fi
printf 'SESSION_TIME entry_complete %s\n' "$(cut -d ' ' -f1 /proc/uptime)"
"$BASE/busybox" sh "$BASE/system-survey.sh" "$BASE" after
echo ENTRY_COMPLETE=1
