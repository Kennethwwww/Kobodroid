#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
static long now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec;}
int main(int argc,char**argv){if(argc<3)return 2;int seconds=atoi(argv[1]);if(seconds<1||seconds>900)return 2;pid_t owner=getpid(),c=fork();if(c<0)return 2;if(!c){prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=owner)_exit(126);setpgid(0,0);execvp(argv[2],argv+2);_exit(127);}setpgid(c,c);long end=now()+seconds;int status;for(;;){pid_t p=waitpid(c,&status,WNOHANG);if(p==c)return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);if(p<0&&errno!=EINTR)return 126;if(now()>=end){kill(-c,SIGKILL);kill(c,SIGKILL);while(waitpid(c,&status,0)<0&&errno==EINTR){}fprintf(stderr,"COMMAND_TIMEOUT=%s seconds=%d\n",argv[2],seconds);return 124;}usleep(100000);}}
