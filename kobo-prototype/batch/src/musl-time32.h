/* musl renames the 32-bit time syscalls; restore the kernel 4.9 names the filters use. */
#include <sys/syscall.h>
#if !defined(__NR_settimeofday) && defined(__NR_settimeofday_time32)
#define __NR_settimeofday __NR_settimeofday_time32
#endif
#if !defined(__NR_clock_settime) && defined(__NR_clock_settime32)
#define __NR_clock_settime __NR_clock_settime32
#define SYS_clock_settime __NR_clock_settime32
#endif
#if !defined(__NR_clock_adjtime) && defined(__NR_clock_adjtime32)
#define __NR_clock_adjtime __NR_clock_adjtime32
#endif
#if !defined(__NR_clock_nanosleep) && defined(__NR_clock_nanosleep_time32)
#define __NR_clock_nanosleep __NR_clock_nanosleep_time32
#endif
