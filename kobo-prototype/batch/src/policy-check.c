#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
int main(void){
 errno=0;if(syscall(SYS_clock_settime,-1,NULL)!=-1||errno!=EPERM)return 1;
 int tcp=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);if(tcp<0)return 2;
 errno=0;if(ioctl(tcp,0x890B,NULL)!=-1||errno!=EPERM)return 9;close(tcp);
 int udp6=socket(AF_INET6,SOCK_DGRAM,0);if(udp6<0)return 10;close(udp6);
 errno=0;if(socket(AF_INET,SOCK_RAW,1)!=-1||errno!=EPERM)return 11;
 errno=0;if(socket(AF_PACKET,SOCK_RAW,0)!=-1||errno!=EPERM)return 12;
 errno=0;if(socket(AF_NETLINK,SOCK_DGRAM,0)!=-1||errno!=EPERM)return 13;
 errno=0;if(kill(getpid(),0)!=-1||errno!=EPERM)return 3;
 errno=0;if(ptrace(PTRACE_TRACEME,0,0,0)!=-1||errno!=EPERM)return 4;
 errno=0;if(socket(AF_NETLINK,SOCK_RAW,0)!=-1||errno!=EPERM)return 5;
 int u=socket(AF_UNIX,SOCK_STREAM,0);if(u<0)return 6;
 errno=0;if(ioctl(u,0x8914,NULL)!=-1||errno!=EPERM)return 7;close(u);
 int events=socket(AF_NETLINK,SOCK_DGRAM,15);if(events<0)return 8;close(events);
 puts("SECCOMP_POLICY=PASS");return 0;
}
