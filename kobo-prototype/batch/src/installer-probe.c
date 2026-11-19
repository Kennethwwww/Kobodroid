/* Fixed installd ping; exercise the actual socket as root and Android system.
 * Never sends commands which alter application or host state. */
#define _GNU_SOURCE
#include <errno.h>
#include <grp.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
static int failure(const char *step) {
 printf("INSTALLER_PING uid=%u step=%s errno=%d error=%s\n",(unsigned)getuid(),step,errno,strerror(errno));
 return 1;
}
static int transfer(int fd,void *p,size_t n,int writing) {
 unsigned char *b=p;
 while(n) {
  struct pollfd f={fd,writing?POLLOUT:POLLIN,0};
  int r=poll(&f,1,3000);
  if(r<=0){if(!r)errno=ETIMEDOUT;return -1;}
  ssize_t k=writing?send(fd,b,n,MSG_NOSIGNAL):read(fd,b,n);
  if(k<=0){if(!k)errno=ECONNRESET;return -1;}b+=k;n-=k;
 }
 return 0;
}
static int ping(void) {
 struct stat s;
 if(lstat("/dev/socket/installd",&s))return failure("stat");
 printf("INSTALLER_SOCKET uid=%u owner=%u group=%u mode=%o inode=%llu\n",(unsigned)getuid(),s.st_uid,s.st_gid,s.st_mode&07777,(unsigned long long)s.st_ino);
 int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
 if(fd<0)return failure("socket");
 struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,"/dev/socket/installd");
 if(connect(fd,(struct sockaddr*)&a,sizeof(a))) {
  if(errno!=EINPROGRESS){close(fd);return failure("connect");}
  struct pollfd p={fd,POLLOUT,0};int e=0;socklen_t len=sizeof(e);
  if(poll(&p,1,3000)<=0){errno=ETIMEDOUT;close(fd);return failure("connect_wait");}
  if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&e,&len)||e){if(e)errno=e;close(fd);return failure("connect_error");}
 }
 uint16_t n=4;char msg[16]="ping";
 if(transfer(fd,&n,2,1)||transfer(fd,msg,4,1)||transfer(fd,&n,2,0)){close(fd);return failure("protocol");}
 if(n<1||n>=sizeof(msg)){close(fd);errno=EPROTO;return failure("length");}
 memset(msg,0,sizeof(msg));
 if(transfer(fd,msg,n,0)){close(fd);return failure("reply");}close(fd);
 printf("INSTALLER_PING uid=%u reply=%s result=%s\n",(unsigned)getuid(),msg,strcmp(msg,"0")?"FAIL":"PASS");
 return !!strcmp(msg,"0");
}
int main(int argc,char **argv) {
 setbuf(stdout,NULL);
 if(argc==2&&!strcmp(argv[1],"--java-system")) {
  if(setgroups(0,NULL)||setgid(1000)||setuid(1000))return failure("credentials");
  printf("JAVA_INSTALLER_EXEC uid=%u gid=%u\n",(unsigned)getuid(),(unsigned)getgid());
  setenv("CLASSPATH","/kdbin/kobo-probe.jar",1);
  execl("/system/bin/app_process","app_process","-Xmx64m","/kdbin","KoboInstallerProbe","system",NULL);
  return failure("java_exec");
 }
 if(argc!=1)return 2;
 int a=ping(),status;pid_t child=fork();
 if(child<0)return failure("fork");
 if(!child){if(setgroups(0,NULL)||setgid(1000)||setuid(1000))_exit(failure("credentials"));_exit(ping());}
 if(waitpid(child,&status,0)<0)return failure("wait");
 return a||!WIFEXITED(status)||WEXITSTATUS(status);
}
