#!/bin/sh
# Read stock interfaces and test TAP only inside a disposable network namespace.
BASE=$1
BB=$BASE/busybox
echo "KOBO_SYSTEM_SURVEY_BEGIN phase=${2:-before}"
if [ "${2:-}" != after ]; then
"$BB" timeout 4 "$BASE/system-probe" || true
for tool in ip iptables iptables-save iw iwconfig udhcpc; do
    command -v "$tool" || true
done
ip -4 route show table all 2>/dev/null || true
if [ -r /proc/config.gz ]; then
    "$BB" zcat /proc/config.gz | "$BB" grep -E '^(# )?CONFIG_(NET_NS|TUN|VETH|IP_MULTIPLE_TABLES|IP_NF_IPTABLES|IP_NF_NAT|IP_NF_TARGET_MASQUERADE|NF_NAT|NF_CONNTRACK|USB_CONFIGFS|PM_SLEEP|SUSPEND)(=| )'
fi
fi
for node in /sys/class/power_supply/*/type /sys/class/power_supply/*/capacity /sys/class/power_supply/*/status /sys/class/power_supply/*/online /sys/power/state /sys/power/mem_sleep; do
    [ ! -r "$node" ] || { printf '%s ' "$node"; cat "$node"; }
done
for node in /sys/kernel/config/usb_gadget/*/UDC /sys/class/android_usb/*/state /sys/class/udc/*/state; do
    [ ! -r "$node" ] || { printf '%s ' "$node"; cat "$node"; }
done
# Capture prerequisites for a later suspend test without changing wake sources.
for node in /sys/power/wakeup_count /sys/power/pm_wakeup_irq; do
    [ ! -r "$node" ] || { printf '%s ' "$node"; "$BB" timeout 1 cat "$node" || true; }
done
for node in /sys/class/input/event*; do
    [ -r "$node/device/name" ] || continue
    name=$(cat "$node/device/name")
    case "$name" in gpio-keys|*Elan*)
        printf 'WAKE_INPUT %s %s\n' "${node##*/}" "$name"
        for attr in capabilities/key power/wakeup device/power/wakeup; do
            [ ! -r "$node/device/$attr" ] || { printf '%s ' "$attr"; cat "$node/device/$attr"; }
        done
    esac
done
echo KOBO_SYSTEM_SURVEY_END
