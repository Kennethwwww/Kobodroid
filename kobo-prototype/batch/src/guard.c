/* Android 7 variant of device-firstboot/src/guard-exec.c: same policy plus the narrow exceptions below
   (FUSE mount flag set; mknod of non-device nodes and of char 1:3). */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/audit.h>
#include <stddef.h>
#include <sys/syscall.h>
#define DENY(n) BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,(n),0,1), BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM)
int main(int argc,char **argv){
 if(argc<2)return 2;
 struct sock_filter code[]={
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,arch)),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,AUDIT_ARCH_ARM,1,0),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_KILL),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,nr)),
 /* No new superblocks after setup. Allow bind, namespace propagation and
    readonly bind remounts. Never allow writable remounts.
    Android 7 exception: the sdcard daemon mounts FUSE for emulated storage with exactly
    MS_NOSUID|MS_NODEV|MS_NOEXEC|MS_NOATIME. seccomp cannot read the fstype string, so this
    exact flag set is allowed; any other new-superblock flag combination stays EPERM. */
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,__NR_mount,0,18),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,args[3])),
 BPF_JUMP(BPF_JMP|BPF_JSET|BPF_K,MS_REMOUNT,0,4),
 BPF_JUMP(BPF_JMP|BPF_JSET|BPF_K,MS_RDONLY,0,2),
 BPF_JUMP(BPF_JMP|BPF_JSET|BPF_K,MS_BIND,0,1),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,MS_BIND,10,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,MS_BIND|MS_REC,9,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,MS_SLAVE|MS_REC,8,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,MS_PRIVATE|MS_REC,7,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,MS_SHARED|MS_REC,6,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,MS_PRIVATE,5,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,MS_SHARED,4,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,MS_SLAVE,3,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,MS_BIND|MS_REC|MS_SLAVE,2,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,MS_NOSUID|MS_NODEV|MS_NOEXEC|MS_NOATIME,1,0),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW),
 /* mknod: no device nodes. Block devices and character devices are EPERM, with one exception: Android 7
    init exits unless it can mknod a private /dev/__null__ (char 1:3) for its stdio. Regular files, FIFOs and
    sockets stay allowed: Android 7's FUSE sdcard daemon creates every file on emulated storage with
    mknod(S_IFREG) (it has no FUSE_CREATE), which an earlier, stricter rule broke ("Operation not permitted"). */
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,__NR_mknod,0,9),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,args[1])),
 BPF_STMT(BPF_ALU|BPF_AND|BPF_K,S_IFMT),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,S_IFBLK,5,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,S_IFCHR,1,0),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,args[2])),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,0x103,0,1),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,__NR_mknodat,0,9),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,args[2])),
 BPF_STMT(BPF_ALU|BPF_AND|BPF_K,S_IFMT),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,S_IFBLK,5,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,S_IFCHR,1,0),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,args[3])),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,0x103,0,1),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM),
#ifdef __NR_timerfd_create
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,__NR_timerfd_create,0,4),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,args[0])),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,8,1,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,9,0,1),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,nr)),
#endif
#ifdef __NR_timer_create
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,__NR_timer_create,0,4),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,args[0])),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,8,1,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,9,0,1),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,nr)),
#endif
#ifdef __NR_clock_nanosleep
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,__NR_clock_nanosleep,0,4),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,args[0])),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,8,1,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,9,0,1),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,nr)),
#endif
#ifdef __NR_clock_nanosleep_time64
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,__NR_clock_nanosleep_time64,0,4),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,args[0])),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,8,1,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,9,0,1),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,nr)),
#endif
 DENY(__NR_kill), DENY(__NR_tkill),
#ifdef __NR_process_vm_writev
 DENY(__NR_process_vm_writev),
#endif
 DENY(__NR_sethostname), DENY(__NR_setdomainname),
 /* Kobo has no network namespaces, so the guest shares the host stack. Apps get TCP/UDP over IPv4/IPv6.
    Raw/packet sockets and route/netfilter netlink stay blocked, as do the interface ioctls below, so the guest
    can use the host network but cannot configure it. Storage daemon may listen for uevents. */
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,__NR_socket,0,13),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,args[0])),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,1,10,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,2,1,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,10,0,4),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,args[1])),
 BPF_STMT(BPF_ALU|BPF_AND|BPF_K,0xf),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,1,5,0),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,2,4,3),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,16,0,2),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,args[2])),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,15,1,0),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,nr)),
 /* Interface-control ioctls can otherwise operate via a Unix socket. */
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,__NR_ioctl,0,4),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,args[1])),
 BPF_STMT(BPF_ALU|BPF_AND|BPF_K,0xff00),
 BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,0x8900,0,1),
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM),
 BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,nr)),
 DENY(__NR_umount2), DENY(__NR_pivot_root),
 DENY(__NR_reboot), DENY(__NR_settimeofday), DENY(__NR_clock_settime),
 DENY(__NR_adjtimex), DENY(__NR_clock_adjtime),
 DENY(__NR_init_module), DENY(__NR_finit_module), DENY(__NR_delete_module),
 DENY(__NR_kexec_load), DENY(__NR_ptrace), DENY(__NR_bpf), DENY(__NR_perf_event_open),
 DENY(__NR_swapon), DENY(__NR_swapoff),
 DENY(__NR_syslog), DENY(__NR_open_by_handle_at),
#ifdef __NR__sysctl
 DENY(__NR__sysctl),
#endif
#ifdef __NR_mount_setattr
 DENY(__NR_mount_setattr),
#endif
#ifdef __NR_kexec_file_load
 DENY(__NR_kexec_file_load),
#endif
#ifdef __NR_clock_settime64
 DENY(__NR_clock_settime64), DENY(__NR_clock_adjtime64),
#endif
#ifdef __NR_fsopen
 DENY(__NR_fsopen), DENY(__NR_fsmount), DENY(__NR_move_mount), DENY(__NR_open_tree), DENY(__NR_fsconfig),
#endif
 BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW)};
 struct sock_fprog p={sizeof(code)/sizeof(code[0]),code};
 if(prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)||prctl(PR_SET_SECCOMP,SECCOMP_MODE_FILTER,&p)){perror("seccomp");return 3;}
 errno=0;if(syscall(__NR_clock_settime,-1,NULL)!=-1||errno!=EPERM)return 4;
 execvp(argv[1],argv+1);perror("exec");return 5;
}
