#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/utsname.h>
#include <linux/loop.h>
#include <sched.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <limits.h>
static char group[PATH_MAX];
static volatile sig_atomic_t stop;
#ifdef KOBO_WAKELOCK_FIXTURE
#define WAKE_LOCK_FILE "/tmp/kobo-wake-fixture/lock"
#define WAKE_UNLOCK_FILE "/tmp/kobo-wake-fixture/unlock"
#else
#define WAKE_LOCK_FILE "/sys/power/wake_lock"
#define WAKE_UNLOCK_FILE "/sys/power/wake_unlock"
#endif
static int wake_active;
static char wake_name[64];
static void wake_release(void){
 if(!wake_active)return;
 int fd=open(WAKE_UNLOCK_FILE,O_WRONLY|O_CLOEXEC|O_NOFOLLOW);
 int ok=0;if(fd>=0){size_t n=strlen(wake_name);ok=write(fd,wake_name,n)==(ssize_t)n;close(fd);}
 printf("HOST_WAKELOCK_RELEASE=%s\n",ok?"PASS":"unavailable-or-expired");wake_active=0;
}
static void wake_acquire(void){
 if(access(WAKE_LOCK_FILE,W_OK)||access(WAKE_UNLOCK_FILE,W_OK)){puts("HOST_WAKELOCK=unavailable");return;}
 snprintf(wake_name,sizeof wake_name,"kindleroid-batch-%d",getpid());
 char value[96];snprintf(value,sizeof value,"%s 1000000000000",wake_name);
 int fd=open(WAKE_LOCK_FILE,O_WRONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0){puts("HOST_WAKELOCK=unavailable");return;}
 size_t n=strlen(value);ssize_t done=write(fd,value,n);close(fd);
 if(done==(ssize_t)n){wake_active=1;puts("HOST_WAKELOCK=timed-1000s");}
 else {if(done>0){wake_active=1;wake_release();}puts("HOST_WAKELOCK=unavailable");}
}

static void stopped(int n){stop=n;}
static long seconds(void){struct timespec t;clock_gettime(CLOCK_BOOTTIME,&t);return t.tv_sec;}
static int put(const char *name,const char *s){char p[PATH_MAX];snprintf(p,sizeof p,"%s/%s",group,name);int f=open(p,O_WRONLY);if(f<0)return -1;int n=write(f,s,strlen(s));close(f);return n==(int)strlen(s)?0:-1;}
static long available(void){FILE*f=fopen("/proc/meminfo","r");if(!f)return 0;char s[256];long k=0;while(fgets(s,sizeof s,f))if(sscanf(s,"MemAvailable: %ld",&k)==1)break;fclose(f);return k;}
#ifdef KOBO_SESSION_MEMORY_RECLAIM
enum {
 SESSION_RESERVE_KIB = 256 * 1024,
 SESSION_EMERGENCY_KIB = 192 * 1024,
 SESSION_RESERVE_GRACE_SEC = 15
};

static const char *memory_stop_reason(long mem, long now, long *below_since)
{
 if (mem < SESSION_EMERGENCY_KIB) return "emergency";
 if (mem >= SESSION_RESERVE_KIB) {
  if (*below_since >= 0)
   printf("SESSION_MEMORY_RESERVE=RECOVERED available_kib=%ld\n", mem);
  *below_since = -1;
  return NULL;
 }
 if (*below_since < 0) {
  *below_since = now;
  printf("SESSION_MEMORY_RESERVE=GRACE available_kib=%ld seconds=%d\n",
         mem, SESSION_RESERVE_GRACE_SEC);
 }
 return now - *below_since >= SESSION_RESERVE_GRACE_SEC ? "sustained" : NULL;
}

static void memory_sample(const char *reason, long mem)
{
 char path[PATH_MAX], line[256];
 printf("SESSION_MEMORY_SAMPLE reason=%s uptime_s=%ld available_kib=%ld\n",
        reason, seconds(), mem);
 FILE *f = fopen("/proc/meminfo", "r");
 if (f) {
  while (fgets(line, sizeof line, f)) {
   if (!strncmp(line, "AnonPages:", 10) || !strncmp(line, "Shmem:", 6) ||
       !strncmp(line, "SUnreclaim:", 11) || !strncmp(line, "Cached:", 7) ||
       !strncmp(line, "SwapFree:", 9))
    printf("SESSION_MEMORY_HOST %s", line);
  }
  fclose(f);
 }
 snprintf(path, sizeof path, "%s/cgroup.procs", group);
 FILE *members = fopen(path, "r");
 if (!members) return;
 int pid;
 while (fscanf(members, "%d", &pid) == 1) {
  snprintf(path, sizeof path, "/proc/%d/status", pid);
  f = fopen(path, "r");
  if (!f) continue;
  char name[32] = "?";
  long rss = 0, anon = 0;
  while (fgets(line, sizeof line, f)) {
   if (sscanf(line, "Name: %31s", name) == 1) continue;
   if (sscanf(line, "VmRSS: %ld", &rss) == 1) continue;
   if (sscanf(line, "RssAnon: %ld", &anon) == 1) break;
  }
  fclose(f);
  if (rss >= 1024)
   printf("SESSION_MEMORY_PROCESS pid=%d name=%s rss_kib=%ld anon_kib=%ld\n",
          pid, name, rss, anon);
 }
 fclose(members);
}
#endif
static int tasks(int killthem){char p[PATH_MAX];snprintf(p,sizeof p,"%s/tasks",group);FILE*f=fopen(p,"r");if(!f)return -1;int n=0,pid;while(fscanf(f,"%d",&pid)==1){n++;if(killthem&&pid>1){printf("REAP_TASK pid=%d\n",pid);kill(pid,SIGKILL);}}fclose(f);return n;}
/* Freeze membership before signalling: live frozen tasks cannot exit and have their PIDs reused. */
static int clear_group(void){if(put("freezer.state","FROZEN"))return -1;char p[PATH_MAX],state[32];snprintf(p,sizeof p,"%s/freezer.state",group);int frozen=0;for(int i=0;i<100;i++){FILE*f=fopen(p,"r");state[0]=0;if(f){if(!fgets(state,sizeof state,f))state[0]=0;fclose(f);}if(!strncmp(state,"FROZEN",6)){frozen=1;break;}usleep(20000);}if(!frozen){put("freezer.state","THAWED");return -1;}tasks(1);put("freezer.state","THAWED");for(int i=0;i<100;i++){if(tasks(0)==0)return 0;usleep(20000);}return -1;}
static int recovery_preflight(void){
 int p[2];if(pipe(p))return -1;pid_t owner=getpid(),c=fork();
 if(!c){close(p[0]);prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=owner||put("tasks","0"))_exit(1);pid_t dad=getpid(),grand=fork();if(grand<0)_exit(2);if(!grand){prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=dad)_exit(4);setsid();for(;;)pause();}if(write(p[1],"R",1)!=1)_exit(3);for(;;)pause();}
 close(p[1]);if(c<0){close(p[0]);return -1;}struct pollfd f={p[0],POLLIN,0};char b=0;int ready=poll(&f,1,5000)>0&&read(p[0],&b,1)==1&&b=='R';close(p[0]);int r=clear_group();while(waitpid(-1,NULL,WNOHANG)>0){}return ready&&r==0?0:-1;
}
static int image(const char *path,const char *target,int ro){
#ifdef KOBO_LAB
 struct stat block;if(!stat(path,&block)&&S_ISBLK(block.st_mode))return mount(path,target,"ext4",MS_NODEV|MS_NOSUID|(ro?MS_RDONLY:0),NULL);
#endif
 int src=open(path,ro?O_RDONLY:O_RDWR);int ctl=open("/dev/loop-control",O_RDWR);if(src<0||ctl<0)return -1;int fd=-1;for(int i=0;i<16;i++){int n=ioctl(ctl,LOOP_CTL_GET_FREE);if(n<0)break;char p[64];snprintf(p,sizeof p,"/dev/loop%d",n);fd=open(p,O_RDWR);if(fd<0)break;if(!ioctl(fd,LOOP_SET_FD,src))break;close(fd);fd=-1;}close(ctl);close(src);if(fd<0)return -1;struct loop_info64 info={0};info.lo_flags=LO_FLAGS_AUTOCLEAR|(ro?LO_FLAGS_READ_ONLY:0);if(ioctl(fd,LOOP_SET_STATUS64,&info)){ioctl(fd,LOOP_CLR_FD,0);close(fd);return -1;}char p[64];snprintf(p,sizeof p,"/proc/self/fd/%d",fd);int r=mount(p,target,"ext4",MS_NODEV|MS_NOSUID|(ro?MS_RDONLY:0),NULL);if(r)ioctl(fd,LOOP_CLR_FD,0);close(fd);return r;}
/* NickelMenu and its SSH daemon can leave host descriptors open across exec.
 * The Android session needs only stdio until the runner opens its own files. */
static int close_inherited_fds(void) {
 DIR *directory = opendir("/proc/self/fd");
 if (!directory) return -1;
 int scan_fd = dirfd(directory);
 unsigned closed = 0;
 int error;
 for (;;) {
  errno = 0;
  struct dirent *entry = readdir(directory);
  if (!entry) {
   error = errno;
   break;
  }
  char *end;
  long fd = strtol(entry->d_name, &end, 10);
  if (*end || fd <= STDERR_FILENO || fd > INT_MAX || fd == scan_fd) continue;
  if (!close((int)fd)) ++closed;
 }
 closedir(directory);
 if (error) {
  errno = error;
  return -1;
 }
 printf("HOST_FDS_CLOSED=%u\n", closed);
 return 0;
}
static int binder_idle(void){
 struct stat b;if(stat("/dev/vndbinder",&b)||!S_ISCHR(b.st_mode))return -1;
 DIR*d=opendir("/proc");if(!d)return -1;struct dirent*e;int busy=0;
 while((e=readdir(d))){if(e->d_name[0]<'0'||e->d_name[0]>'9')continue;
  char p[PATH_MAX];snprintf(p,sizeof p,"/proc/%s/fd",e->d_name);DIR*f=opendir(p);if(!f){if(errno!=ENOENT)busy=1;continue;}struct dirent*x;
  while((x=readdir(f))){if(x->d_name[0]=='.')continue;char q[PATH_MAX];snprintf(q,sizeof q,"%s/%s",p,x->d_name);struct stat v;if(stat(q,&v)==0&&S_ISCHR(v.st_mode)&&v.st_rdev==b.st_rdev)busy=1;}closedir(f);
  snprintf(p,sizeof p,"/proc/%s/maps",e->d_name);FILE*m=fopen(p,"r");if(m){char line[1024];while(fgets(line,sizeof line,m))if(strstr(line,"/dev/vndbinder"))busy=1;fclose(m);}else if(errno!=ENOENT&&errno!=ESRCH)busy=1;
 }closedir(d);return busy?-1:0;
}
int main(int argc,char**argv){setvbuf(stdout,NULL,_IONBF,0);if(argc==5&&!strcmp(argv[1],"--image")){int r=image(argv[2],argv[3],atoi(argv[4]));if(r)perror("image mount");return r?1:0;}
 if(argc!=2)return 2;
 char base[PATH_MAX];if(!realpath(argv[1],base))return 2;struct utsname u;uname(&u);
#ifdef KOBO_LAB
 if(geteuid())
#else
 if(geteuid()||strcmp(u.release,"4.9.77")||strcmp(u.machine,"armv7l"))
#endif
{puts("REFUSED identity");return 3;}
 if(close_inherited_fds()){perror("close inherited descriptors");return 3;}
 char path[PATH_MAX];snprintf(path,sizeof path,"%s/session.lock",base);int lock=open(path,O_CREAT|O_RDWR|O_CLOEXEC,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){puts("REFUSED busy");return 3;}
 if(binder_idle()){puts("REFUSED vndbinder occupied or unverifiable");return 3;}
 puts("BINDER_IDLE=PASS");
 if(available()<600*1024){puts("REFUSED available-memory minimum=600MiB");return 3;}
 atexit(wake_release);wake_acquire();
 if(prctl(PR_SET_CHILD_SUBREAPER,1)||unshare(CLONE_NEWNS)||mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL)){perror("mount namespace");return 4;}
 puts("MOUNT_NAMESPACE=PASS");
 char cg[PATH_MAX];snprintf(cg,sizeof cg,"/tmp/kobo-core-cg-%d",getpid());if(mkdir(cg,0700)||mount("none",cg,"cgroup",MS_NOSUID|MS_NODEV|MS_NOEXEC,"freezer")){perror("private freezer mount");return 5;}
 snprintf(group,sizeof group,"%s/session-%d",cg,getpid());if(mkdir(group,0700)){perror("freezer group");return 5;}
 if(recovery_preflight()){puts("FREEZER_RECOVERY_PREFLIGHT=FAIL");return 6;}puts("FREEZER_RECOVERY_PREFLIGHT=PASS");
 int heart[2];if(pipe(heart))return 6;pid_t owner=getpid();pid_t guardian=fork();if(guardian==0){puts("GUARDIAN_READY");close(heart[1]);char b;fcntl(heart[0],F_SETFL,O_NONBLOCK);for(;;){int n=read(heart[0],&b,1);if(n==1&&b=='D')_exit(0);if(n==0||getppid()!=owner){puts("GUARDIAN_RECOVERY_START");int r=clear_group();printf("GUARDIAN_RECOVERY=%s\n",r?"FAIL":"PASS");if(!r)rmdir(group);umount(cg);rmdir(cg);wake_release();_exit(r?1:0);}usleep(500000);}}if(guardian<0)return 6;close(heart[0]);
 signal(SIGTERM,stopped);signal(SIGINT,stopped);signal(SIGHUP,stopped);
 pid_t worker=fork();if(worker==0){close(heart[1]);prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=owner||put("tasks","0")||unshare(CLONE_NEWNS)){perror("worker isolation");_exit(7);}char bb[PATH_MAX],setup[PATH_MAX];snprintf(bb,sizeof bb,"%s/busybox",base);snprintf(setup,sizeof setup,"%s/setup.sh",base);execl(bb,bb,"sh",setup,base,NULL);_exit(8);}
 printf("WORKER_STARTED pid=%d guardian=%d\n",worker,guardian);
 long start=seconds(),min_available=available();int status=0,done=0;char cancel[PATH_MAX];snprintf(cancel,sizeof cancel,"%s/cancel.request",base);if(worker<0)stop=1;
#ifdef KOBO_SESSION_MEMORY_RECLAIM
 char reclaim[PATH_MAX] = {0};
 const char *bridge = getenv("KOBO_BRIDGE_DIR");
 if (bridge && !strncmp(bridge, "/tmp/kobo-android.", 18)) {
  int n = snprintf(reclaim, sizeof reclaim, "%s/memory.reclaim", bridge);
  if (n < 0 || n >= (int)sizeof reclaim) reclaim[0] = 0;
 }
 long next_reclaim = 0;
 long below_since = -1;
 int baseline_sampled = 0;
#endif
 while (!stop) {
  long mem = available();
  if (mem < min_available) min_available = mem;
  if (access(cancel, F_OK) == 0) { puts("SESSION_STOP=cancel"); break; }
  pid_t r = waitpid(worker, &status, WNOHANG);
  if (r == worker) { done = 1; break; }
#ifdef KOBO_SESSION_MEMORY_RECLAIM
  struct timespec awake;
  clock_gettime(CLOCK_MONOTONIC, &awake);
  const char *reason = memory_stop_reason(mem, awake.tv_sec, &below_since);
  if (reason) {
   printf("SESSION_STOP=memory reason=%s available_kib=%ld\n", reason, mem);
   memory_sample("stop", mem);
   break;
  }
  long now = seconds();
  if (!baseline_sampled && now - start >= 60) {
   baseline_sampled = 1;
   memory_sample("baseline", mem);
  }
  if (reclaim[0] && mem < 320 * 1024 && now >= next_reclaim) {
   next_reclaim = now + 30;
   int fd = open(reclaim, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
   if (fd >= 0) {
    close(fd);
    printf("SESSION_MEMORY_RECLAIM=REQUEST available_kib=%ld\n", mem);
    memory_sample("reclaim", mem);
   }
  }
#else
  if (mem < 256 * 1024) {
   printf("SESSION_STOP=memory reserve=256MiB available_kib=%ld\n", mem);
   break;
  }
#endif
  usleep(500000);
 }
 printf("SESSION_RUNTIME_SECONDS=%ld minimum_available_kib=%ld\n",seconds()-start,min_available);
 int result=clear_group();printf("SESSION_CLEANUP=%s remaining=%d worker_status=%d\n",result?"FAIL":"PASS",tasks(0),status);if(!result){if(write(heart[1],"D",1)!=1)perror("guardian completion");}close(heart[1]);
 for(int i=0;i<50;i++){while(waitpid(-1,NULL,WNOHANG)>0){}if(kill(guardian,0)<0&&errno==ESRCH)break;usleep(100000);}if(!done)waitpid(worker,NULL,WNOHANG);
 snprintf(path,sizeof path,"/tmp/kobo-core-root-%d",worker);if(!result)rmdir(path);
 printf("BINDER_IDLE_AFTER=%s\n",binder_idle()?"FAIL":"PASS");
 int removed=rmdir(group);printf("FREEZER_GROUP_REMOVED=%d\n",removed==0);umount(cg);rmdir(cg);puts("REPORT_COMPLETE=1");return result?9:(!done||!WIFEXITED(status)||WEXITSTATUS(status)!=0)?10:0;
}
