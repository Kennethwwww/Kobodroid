#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/xattr.h>
int main(int argc,char**argv){
 if(argc!=2)return 2;
 char p[1024];snprintf(p,sizeof p,"%s/.kobo-xattr-probe-%d",argv[1],getpid());
 int fd=open(p,O_RDWR|O_CREAT|O_EXCL,0600);if(fd<0){printf("XATTR_OPEN errno=%d\n",errno);return 1;}
 int ordinary_ok=0,label_result=-1,label_errno=0;
 const char *keys[]={"user.kindleroid_probe","security.selinux"};
 for(int i=0;i<2;i++){
  const char *v=i?"u:object_r:default_prop:s0":"probe";
  errno=0;int r=fsetxattr(fd,keys[i],v,strlen(v)+1,0),e=errno;
  #ifdef KOBO_LAB_LABEL95
  if(i){r=-1;e=95;puts("LAB_ONLY injected label errno95");}
#endif
  if(!i)ordinary_ok=(r==0);else {label_result=r;label_errno=e;}
  printf("XATTR key=%s result=%d errno=%d (%s)\n",keys[i],r,r?e:0,r?strerror(e):"ok");
  char b[128];errno=0;ssize_t n=fgetxattr(fd,keys[i],b,sizeof b);e=errno;
  printf("XATTR_READ key=%s bytes=%ld errno=%d\n",keys[i],(long)n,n<0?e:0);
 }
 close(fd);unlink(p);
 if(!ordinary_ok)return 10;
 if(label_result==0)return 0;
 if(label_errno!=95)return 10;
 char context[64]={0};int c=open("/proc/self/attr/current",O_RDONLY);
 if(c<0)return 10;
 ssize_t n=read(c,context,sizeof(context)-1);close(c);
 if(n<=0||strcmp(context,"kernel"))return 10;
 FILE *m=fopen("/proc/mounts","r");if(!m)return 10;
 char line[1024];while(fgets(line,sizeof line,m))if(strstr(line," selinuxfs ")){fclose(m);return 10;}fclose(m);
 puts("PROPERTY_LABEL_COMPAT_GATE=PASS inherited=kernel label_errno=95 no_selinuxfs=1");return 95;
}
