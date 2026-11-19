/* Preloaded into every guest process. The guest syscall filter denies kill(): the guest shares the Kobo's PID
 * namespace, and a guest kill could reach Nickel. That also stopped Android from killing its own processes (force
 * stop, freeing memory, restarting apps). When the filter refuses a kill, this asks kd-killd on the host, which
 * signals the target only if it is a guest process the caller may signal. */
#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#define BROKER "kobodroid-kill"

static int ask_broker(pid_t pid, int sig) {
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return EPERM;
    /* An autobound abstract address, so the broker can answer. */
    struct sockaddr_un self = {.sun_family = AF_UNIX};
    struct sockaddr_un broker = {.sun_family = AF_UNIX};
    memcpy(broker.sun_path + 1, BROKER, sizeof(BROKER) - 1);
    socklen_t broker_len = offsetof(struct sockaddr_un, sun_path) + 1 + sizeof(BROKER) - 1;
    struct timeval timeout = {1, 0};
    int answer = EPERM;
    char request[32];
    int n = snprintf(request, sizeof(request), "%d %d", (int)pid, sig);
    if (bind(fd, (struct sockaddr *)&self, sizeof(sa_family_t)) == 0 &&
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
        sendto(fd, request, n, 0, (struct sockaddr *)&broker, broker_len) == n) {
        char reply[16] = "";
        if (recv(fd, reply, sizeof(reply) - 1, 0) > 0) sscanf(reply, "%d", &answer);
    }
    close(fd);
    return answer;
}

int kill(pid_t pid, int sig) {
    long result = syscall(__NR_kill, pid, sig);
    if (result == 0 || errno != EPERM || pid <= 0) return (int)result;
    int error = ask_broker(pid, sig);
    if (!error) return 0;
    errno = error;
    return -1;
}

/* Android's alarm and input loops register their fds with EPOLLWAKEUP. On the host kernel each such fd is a wakeup
 * source that stays active while an event is unread, and the guest is frozen while the Kobo sleeps: one alarm that
 * fires after the freeze kept every suspend refused (WAKEUP_PENDING all night). The host alone decides when the
 * Kobo sleeps, so the flag is dropped. */
#include <sys/epoll.h>
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
    struct epoll_event copy;
    if (event) {
        copy = *event;
        copy.events &= ~(unsigned)EPOLLWAKEUP;
        event = &copy;
    }
    return (int)syscall(__NR_epoll_ctl, epfd, op, fd, event);
}
