#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
    if (unshare(CLONE_NEWNET) != 0) {
        perror("KOBO_NET_NAMESPACE=FAIL");
        return 0;
    }
    puts("KOBO_NET_NAMESPACE=PASS");

    int fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        fd = open("/dev/tun", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("KOBO_NET_TAP_OPEN=FAIL");
        return 0;
    }
    struct ifreq tap = { .ifr_flags = IFF_TAP | IFF_NO_PI };
    strcpy(tap.ifr_name, "kdprobe%d");
    if (ioctl(fd, TUNSETIFF, &tap) != 0)
        perror("KOBO_NET_TAP_CREATE=FAIL");
    else
        puts("KOBO_NET_TAP_CREATE=PASS");
    close(fd);
    return 0;
}
