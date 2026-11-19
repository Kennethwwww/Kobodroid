# Executed after the offline Android framework is ready.
session_home() {
    # A fresh image has no package registry until Android finishes booting.
    launcher_uid=$($BB awk '$1 == "kindleroid.launcher" && $2 ~ /^[0-9]+$/ {print $2; exit}' /data/system/packages.list)
    case "$launcher_uid" in
        ''|*[!0-9]*) echo LAUNCHER_BRIDGE=NO_UID; return 1 ;;
    esac
    $BB chown "$launcher_uid:$launcher_uid" /dev/kobodroid-ui/channel &&
        $BB ln -s channel "/dev/kobodroid-ui/$launcher_uid" || return 1
    echo "LAUNCHER_BRIDGE=READY uid=$launcher_uid"
    if [ ! -f /data/local/kobo-session-defaults-v1 ]; then
        configured=1
        for scale in window_animation_scale transition_animation_scale animator_duration_scale; do
            run 8 /system/bin/sh /system/bin/settings put global "$scale" 0 || configured=0
        done
        run 10 /system/bin/sh /system/bin/settings put global device_provisioned 1 || configured=0
        run 10 /system/bin/sh /system/bin/settings put secure user_setup_complete 1 || configured=0
        run 10 /system/bin/sh /system/bin/settings put system screen_off_timeout 2147483647 || configured=0
        if [ "$configured" = 1 ]; then
            $BB touch /data/local/kobo-session-defaults-v1
            echo ANDROID_DEFAULTS_INITIALIZED=1
        fi
    else
        echo ANDROID_DEFAULTS_REUSED=1
    fi
    # Kobodroid defaults, once: Kobo 拼音 (in the launcher) as the keyboard, and installs from apps allowed (self
    # updates, downloaded APKs), since there is no Settings screen for unknown sources here.
    if [ ! -f /data/local/kobodroid-defaults-v2 ]; then
        ime_component=kindleroid.launcher/.PinyinIme
        run 15 /system/bin/sh /system/bin/ime enable "$ime_component" &&
            run 15 /system/bin/sh /system/bin/ime set "$ime_component" &&
            run 10 /system/bin/sh /system/bin/settings put secure install_non_market_apps 1 &&
            $BB touch /data/local/kobodroid-defaults-v2 && echo KOBODROID_DEFAULTS=SET
    fi
    if [ -f /kd-install.apk ]; then
        import=$($BB mktemp -d /data/local/tmp/kobo-install.XXXXXX) || return 1
        $BB chmod 0755 "$import"
        $BB cp /kd-install.apk "$import/app.apk" || return 1
        $BB chmod 0644 "$import/app.apk"
        run 120 /system/bin/sh /system/bin/pm install -r -f "$import/app.apk" > /data/kobo-install-result.txt 2>&1
        $BB cat /data/kobo-install-result.txt
        $BB rm "$import/app.apk"
        $BB rmdir "$import"
        if $BB grep -qx Success /data/kobo-install-result.txt; then
            echo APK_IMPORT=PASS
        else
            echo APK_IMPORT=FAIL
        fi
    fi
    home_result=0
    run 45 /system/bin/sh /system/bin/am start -a android.intent.action.MAIN -c android.intent.category.HOME -n kindleroid.launcher/.HomeActivity > /data/kobo-home-start.txt 2>&1 || home_result=$?
    $BB cat /data/kobo-home-start.txt
    if [ "$home_result" != 0 ]; then
        echo "ANDROID_HOME_START_EXIT=$home_result"
        run 8 /system/bin/logcat -d -b main -b system -b crash -v threadtime -t 120
        return 1
    fi
    if $BB grep -Eq '^(Error|Exception)' /data/kobo-home-start.txt; then return 1; fi
    # On reentry Activity Manager can resume an existing home before am -W
    # registers its draw waiter. Wait for the home stack directly.
    resumed=0
    for attempt in $($BB seq 1 20); do
        if run 5 /system/bin/dumpsys activity activities |
                $BB grep -qE 'mResumedActivity:.* kindleroid\.launcher/\.HomeActivity[ } ]'; then
            resumed=1
            break
        fi
        $BB sleep 1
    done
    if [ "$resumed" != 1 ]; then
        echo ANDROID_HOME_RESUME=TIMEOUT
        return 1
    fi
    set -- $($BB pidof surfaceflinger)
    [ "$#" = 1 ] || return 1
    surface=$1
    echo ANDROID_HOME_STARTED=1
    phase home
    # APKs copied to Kobodroid-APK on the Kobo storage install now; the launcher lists the results.
    (for apk in /kd-apk/*.apk; do
        [ -f "$apk" ] || continue
        name=${apk##*/}
        import=$($BB mktemp -d /data/local/tmp/kobo-install.XXXXXX) || break
        $BB chmod 0755 "$import"
        if $BB cp "$apk" "$import/app.apk"; then
            $BB chmod 0644 "$import/app.apk"
            out=$(run 300 /system/bin/sh /system/bin/pm install -r "$import/app.apk" 2>&1 | $BB tail -n 1)
        else
            out="Failure [copy]"
        fi
        $BB rm -rf "$import"
        if [ "$out" = Success ]; then verdict=PASS folder=installed; else verdict=FAIL folder=failed; fi
        $BB mkdir -p "/kd-apk/$folder" && $BB mv -f "$apk" "/kd-apk/$folder/"
        printf '%s\t%s\t%s\n' "$verdict" "$name" "$out" >> /data/local/tmp/kobodroid-install.tsv
        $BB chmod 0644 /data/local/tmp/kobodroid-install.tsv
        echo "APK_FOLDER_INSTALL $verdict name=$name"
    done) &
    if [ -r /sys/class/power_supply/bd71827_bat/type ]; then
        (echo ANDROID_BATTERY_BEGIN
         run 6 /system/bin/dumpsys battery
         echo ANDROID_BATTERY_END) &
    fi
    CLASSPATH=/kdlib/session-input.jar /kdlib/guard /system/bin/app_process -Xmx24m /kdlib SessionInput &
    (/kdlib/guard /kdlib/capture-frame "$surface" || $BB touch /bridge/stream-error) &
    # The session lasts until the panel returns; the panel's watchdog guards against hangs.
    while :; do
        [ ! -f /bridge/stop ] || return 0
        if [ -f /bridge/result ]; then
            read -r status < /bridge/result
            echo "ANDROID_HOME_PANEL_EXIT=$status"
            [ "$status" = 0 ]
            return $?
        fi
        if [ -f /bridge/stream-error ]; then
            echo ANDROID_HOME_FRAME_STREAM_FAILED=1
            return 1
        fi
        if [ -f /bridge/memory.reclaim ] && $BB rm /bridge/memory.reclaim; then
            before=$($BB awk '/^MemAvailable:/ {print $2}' /proc/meminfo)
            echo "ANDROID_MEMORY_RECLAIM=BEGIN available_kib=$before"
            if run 8 /system/bin/sh /system/bin/am kill-all; then
                after=$($BB awk '/^MemAvailable:/ {print $2}' /proc/meminfo)
                echo "ANDROID_MEMORY_RECLAIM=DONE available_kib=$after"
            else
                echo ANDROID_MEMORY_RECLAIM=FAILED
            fi
        fi
        $BB sleep 1
    done
}
