#!/bin/sh
set -eu
base=$1
work=$2
cd "$work"
export KOBO_RUN_LIMIT="$base/run-limit"
export LD_LIBRARY_PATH=/mnt/onboard/.adds/koreader/libs
# Kobo keeps the clock in UTC and applies its time zone inside Nickel; the panel uses this POSIX zone.
TZ=$("$base/busybox" awk 'NR == 1 {print $2}' "$base/timezone" 2>/dev/null)
export TZ="${TZ:-CST-8}" KOBODROID_SETTINGS="$base/settings" KOBODROID_BASE="$base"
# List descriptors for the page-key device. An open descriptor is not proof of a grab.
for event in /sys/class/input/event*; do
    [ "$(cat "$event/device/name" 2>/dev/null)" = gpio-keys ] || continue
    node=/dev/input/${event##*/}
    echo "PAGE_KEY_DEVICE=$node"
    "$base/run-limit" 8 "$base/busybox" ls -l /proc/[0-9]*/fd/* 2>/dev/null |
        "$base/busybox" awk -v node="$node" '$NF == node && $(NF-1) == "->" {print $(NF-2)}' |
        while IFS= read -r link; do
            process=${link%%/fd/*}
            [ -r "$process/comm" ] || continue
            read -r name < "$process/comm" || continue
            printf "PAGE_KEY_FD pid=%s process=%s\n" "${process##*/}" "$name"
        done
done
# The guest shares the host network stack; its resolver queries 127.0.0.1:53.
# Stock Kobo leaves lo down; raise it for the session and restore it on exit.
loopback=
if ! ip addr show lo | "$base/busybox" grep -q 'inet 127.0.0.1/'; then
    ip addr add 127.0.0.1/8 dev lo && ip link set lo up && loopback=1
fi
# Nickel leaves the second core offline; Android is CPU-bound on one core.
cpu1=/sys/devices/system/cpu/cpu1/online
cpu1_was=
if [ -w "$cpu1" ] && [ "$(cat "$cpu1")" = 0 ]; then
    echo 1 > "$cpu1" && cpu1_was=0
fi
echo "KOBO_CPUS_ONLINE=$(cat /sys/devices/system/cpu/online)"
"$base/busybox" sh "$base/power.sh" wifi-ps 2 || true
"$base/dns-forward" "$work/bridge/stop" &
dns=$!
# Signals Android sends to its own processes (kd-killd.c).
"$base/kd-killd" "$work/bridge/stop" &
killd=$!
usb_watch=
trap '
    for child in "$dns" "$killd" "$usb_watch"; do
        [ -n "$child" ] || continue
        kill "$child" 2>/dev/null || true
        wait "$child" 2>/dev/null || true
    done
    if [ -n "$loopback" ]; then
        ip addr del 127.0.0.1/8 dev lo 2>/dev/null || true
        ip link set lo down 2>/dev/null || true
    fi
    [ -z "$cpu1_was" ] || echo "$cpu1_was" > "$cpu1" 2>/dev/null || true
    "$base/busybox" sh "$base/power.sh" wifi-ps 0 >/dev/null || true
' EXIT
# The panel starts at once: it pauses Nickel and shows the loading screen until Android's first frame.
if [ -r /sys/class/udc/11211000.usb/state ]; then
    "$base/busybox" sh "$base/usb-handoff.sh" "$base" "$work/bridge" /sys/class/udc/11211000.usb/state &
    usb_watch=$!
fi
printf 'SESSION_TIME panel_start %s\n' "$(cut -d ' ' -f1 /proc/uptime)"
status=0
"$base/kobodroid-panel" || status=$?
printf '%s\n' "$status" > bridge/result.tmp
printf 'SESSION_TIME panel_return %s\n' "$(cut -d ' ' -f1 /proc/uptime)"
"$base/busybox" awk '$2 == "00000000" {print "KOBO_ROUTE_AT_PANEL_RETURN=" $1}' /proc/net/route
mv bridge/result.tmp bridge/result
# Wi-Fi was on when Android started but is off now: give it back to Kobo, without delaying the exit.
if [ -f bridge/wifi-restore ]; then
    setsid "$base/busybox" sh "$base/power.sh" wifi-on 0 > /dev/null 2>&1 < /dev/null &
    echo KOBO_WIFI_RESTORE=STARTED
fi
exit "$status"
