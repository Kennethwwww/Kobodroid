/* Host side of the guest kill broker (gles/kd-kill.c). Android's kill() calls that the guest filter refuses arrive
 * on the abstract socket "kobodroid-kill" as "pid signal", with the caller's credentials from the kernel. A signal is
 * sent only when both the caller and the target are guest processes (in the session's freezer group, never Nickel or
 * other Kobo processes), and the caller is root, system, or the target's own uid, as Linux would require inside
 * Android. Exits when the stop file appears. */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define BROKER "kobodroid-kill"

static int guest(pid_t pid) {
    char path[64], line[160];
    int found = 0;
    snprintf(path, sizeof(path), "/proc/%d/cgroup", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) found |= strstr(line, ":freezer:/session-") != NULL;
    fclose(f);
    return found;
}

static long uid_of(pid_t pid) {
    char path[64], line[160];
    long uid = -1;
    snprintf(path, sizeof(path), "/proc/%d/status", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "Uid: %ld", &uid) == 1) break;
    fclose(f);
    return uid;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    setbuf(stdout, NULL);
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0), on = 1;
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    memcpy(addr.sun_path + 1, BROKER, sizeof(BROKER) - 1);
    if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on)) ||
        bind(fd, (struct sockaddr *)&addr, offsetof(struct sockaddr_un, sun_path) + 1 + sizeof(BROKER) - 1)) {
        perror("KOBO_KILL_BROKER");
        return 1;
    }
    puts("KOBO_KILL_BROKER=READY");
    unsigned served = 0, refused = 0;
    while (access(argv[1], F_OK)) {
        struct pollfd p = {fd, POLLIN, 0};
        if (poll(&p, 1, 1000) <= 0) continue;
        char data[64], control[CMSG_SPACE(sizeof(struct ucred))];
        struct sockaddr_un from;
        struct iovec iov = {data, sizeof(data) - 1};
        struct msghdr msg = {.msg_name = &from, .msg_namelen = sizeof(from), .msg_iov = &iov, .msg_iovlen = 1,
                             .msg_control = control, .msg_controllen = sizeof(control)};
        ssize_t n = recvmsg(fd, &msg, 0);
        if (n <= 0) continue;
        data[n] = 0;
        struct ucred *cred = NULL;
        for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c))
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_CREDENTIALS) cred = (struct ucred *)CMSG_DATA(c);
        int pid, sig, error = EPERM;
        if (cred && sscanf(data, "%d %d", &pid, &sig) == 2 && pid > 1 && sig >= 0 && sig < 65 &&
            guest(cred->pid) && guest(pid) &&
            (cred->uid == 0 || cred->uid == 1000 || (long)cred->uid == uid_of(pid))) {
            error = kill(pid, sig) ? errno : 0;
            ++served;
        } else {
            ++refused;
            printf("KOBO_KILL_REFUSED request=%s caller_pid=%d caller_uid=%d\n", data, cred ? (int)cred->pid : -1,
                   cred ? (int)cred->uid : -1);
        }
        char reply[16];
        int len = snprintf(reply, sizeof(reply), "%d", error);
        sendto(fd, reply, len, 0, (struct sockaddr *)&from, msg.msg_namelen);
    }
    printf("KOBO_KILL_BROKER_STOP served=%u refused=%u\n", served, refused);
    return 0;
}
