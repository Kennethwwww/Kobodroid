#!/bin/sh
# Request an ordinary session exit on a positive USB data-link transition.
# The stock UI owns gadget configuration and the mass-storage prompt.
base=$1
bridge=$2
usb=$3
attached() {
    state=
    IFS= read -r state < "$usb" || return 1
    case "$state" in attached|powered|default|addressed|configured) return 0;; esac
    return 1
}
[ -r "$usb" ] || exit 0
# An already-attached link is not an insertion during this session.
if attached; then
    echo USB_HANDOFF=SKIP_ALREADY_ATTACHED
    exit 0
fi
while [ ! -f "$bridge/stop" ] && [ ! -f "$bridge/result" ]; do
    if attached; then
        "$base/busybox" sleep 0.3
        if attached && [ ! -f "$bridge/stop" ] && [ ! -f "$bridge/result" ]; then
            echo "USB_HANDOFF=REQUEST_EXIT state=$state"
            : > "$bridge/stop"
            exit 0
        fi
    fi
    "$base/busybox" sleep 0.3
done
