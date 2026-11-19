#!/bin/sh
# Kobo Wi-Fi and suspend for the Kobodroid panel, following KOReader's Kobo scripts for this MTK model
# (PLATFORM=mt8113t-ntx, WIFI_MODULE=wlan_drv_gen4m: the radio is switched through WMT, not by unloading modules).
# Nickel's dhcpcd keeps running in manager mode; it is only asked to release and rebind wlan0.
export PATH=/sbin:/usr/sbin:/bin:/usr/bin
battery_sample() {
    battery=/sys/class/power_supply/bd71827_bat
    awk -v stage="$1" '
        FNR == 1 && FILENAME ~ /uptime$/ { uptime = $1 }
        /^POWER_SUPPLY_(STATUS|CAPACITY|CHARGE_NOW|CURRENT_AVG)=/ {
            fields = fields " " substr($0, 14)
        }
        END { printf "SESSION_BATTERY stage=%s uptime_s=%s%s\n", stage, uptime, fields }
    ' /proc/uptime "$battery/uevent"
}
case "${1:-}" in
battery)
    battery_sample "${2:-manual}"
    ;;
wifi-off)
    dhcpcd -d -k wlan0 >/dev/null 2>&1
    wpa_cli -i wlan0 terminate >/dev/null 2>&1
    ifconfig wlan0 down
    echo 0 > /dev/wmtWifi
    echo WIFI_OFF=PASS
    ;;
wifi-on)
    echo 0xDB9DB9 > /proc/driver/wmt_dbg
    echo "7 9 0" > /proc/driver/wmt_dbg
    sleep 1
    echo 0xDB9DB9 > /proc/driver/wmt_dbg
    echo "7 9 1" > /proc/driver/wmt_dbg
    echo 1 > /dev/wmtWifi
    sleep 1
    ifconfig wlan0 up
    pkill -0 wpa_supplicant ||
        wpa_supplicant -D nl80211 -s -i wlan0 -c /etc/wpa_supplicant/wpa_supplicant.conf -C /var/run/wpa_supplicant -B
    if pkill -0 dhcpcd; then dhcpcd -n wlan0 >/dev/null 2>&1; else dhcpcd -d -t 30 -w wlan0 >/dev/null 2>&1 & fi
    for i in $(seq 1 60); do
        if ip -4 route show default | grep -q ' dev wlan0'; then
            iwpriv wlan0 set_power_mode "${2:-2}" >/dev/null 2>&1
            echo WIFI_ON=PASS; exit 0
        fi
        sleep 0.5
    done
    echo WIFI_ON=NO_ROUTE
    exit 1
    ;;
wifi-ps)
    # MTK power mode: 0 always awake (Nickel's setting), 2 fast power save (radio sleeps between beacons, full
    # speed under traffic: idle reading 174 -> 104 mA), 1 maximum power save (76 mA but 20 MB took 10-33 s, not 1 s).
    # With Wi-Fi off there is nothing to set (wifi-on sets it later); never fail the caller.
    if iwpriv wlan0 set_power_mode "${2:-2}" >/dev/null 2>&1; then echo "WIFI_PS=${2:-2}"; else echo "WIFI_PS=SKIPPED"; fi
    ;;
suspend)
    # Never while plugged in: on MTK that crashes the kernel (KOReader and Nickel both skip it).
    if [ "$(cat /sys/class/power_supply/bd71827_ac/online 2>/dev/null)" != 0 ]; then echo SUSPEND=SKIP_PLUGGED; exit 4; fi
    # The session holds a user wake lock while Android runs; it would refuse every suspend. Drop it for the
    # suspend and take it back afterwards.
    locks=$(tr ' ' '\n' < /sys/power/wake_lock | grep '^kindleroid-batch-')
    for lock in $locks; do echo "$lock" > /sys/power/wake_unlock; done
    relock() { for lock in $locks; do echo "$lock" > /sys/power/wake_lock; done; }
    echo 1 > /sys/power/state-extended || { relock; echo SUSPEND=REFUSED_EXTENDED; exit 2; }
    sleep 2
    sync
    count=$(cat /sys/power/wakeup_count)
    if ! echo "$count" > /sys/power/wakeup_count; then
        echo 0 > /sys/power/state-extended
        relock
        echo "SUSPEND=WAKEUP_PENDING $(dmesg | grep 'active wakeup source' | tail -n 1 | sed 's/.*source: //')"
        exit 3
    fi
    # Recheck after the preparation delay, when a cable may have been inserted.
    if [ "$(cat /sys/class/power_supply/bd71827_ac/online 2>/dev/null)" != 0 ]; then
        echo 0 > /sys/power/state-extended
        relock
        echo SUSPEND=SKIP_PLUGGED
        exit 4
    fi
    battery_sample suspend
    echo mem > /sys/power/state
    status=$?
    echo 0 > /sys/power/state-extended
    relock
    usleep 100000
    battery_sample resume
    echo "SUSPEND=RESUMED status=$status"
    exit "$status"
    ;;
*)
    exit 2
    ;;
esac
