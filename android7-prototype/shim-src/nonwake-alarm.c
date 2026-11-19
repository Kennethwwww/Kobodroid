/* SPDX-License-Identifier: MIT.
 * Android 10's Java alarm fallback crashes for OnAlarmListener alarms.
 * Return real non-waking timerfds, preserving their clock/expiry semantics.
 * Physical-host wakeups are unsupported; seccomp rejects raw ALARM IDs too.
 * This library targets ARM32, using its actual Linux syscall ABI header.
 */
#include <asm/unistd.h>
extern long syscall(long, ...);
int timerfd_create(int clockid, int flags) {
    if (clockid==8) clockid=0; /* REALTIME_ALARM -> REALTIME */
    if (clockid==9) clockid=7; /* BOOTTIME_ALARM -> BOOTTIME */
    return (int)syscall(__NR_timerfd_create, clockid, flags);
}
