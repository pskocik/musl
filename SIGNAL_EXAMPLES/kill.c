#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>


int cancel;
void action(int Sig, siginfo_t *Info, void *Uctx)
{
	cancel=Sig;
#if defined(musl_sig_cancel__interrupt_syscall) && !FRAGILE
	musl_sig_cancel_flags()->musl_sig_syscall_breaker=Sig;
	musl_sig_cancel__interrupt_syscall(Uctx);
#endif

}
static void die(char const *Msg) { perror(Msg); exit(1); }
ssize_t my_read (int Fd, void *Buf, size_t Sz)
{
	errno=0;
	if(!cancel) return read(Fd,Buf,Sz);
	else{ errno=ECANCELED; return -1; }
}
static char buf[1<<10];
int main(int C, char **V)
{
	pid_t pid;
	sigaction(SIGUSR1,&(const struct sigaction){
				.sa_sigaction=action,
				.sa_flags=SA_SIGINFO,
			}, 0);
	long i=0, top = V[1] ? atoi(V[1]) : 100000;
	for(;i<top;i++){
		fprintf(stderr,"%ld\n", i);
		if(0>(pid=fork())) die("fork");
		if(0==pid){
			if(0>my_read(0,buf,sizeof(buf)))
				/*perror("read")*/
					;
			 //as_unsafe_cleanup();
			_exit(0);
		}else{
			enum { ms = 1000 } ;
			#if !TIMEOUT
				#define TIMEOUT 500
			#endif
			usleep(1);
			if(0>kill(pid,SIGUSR1)) die("kill");
			if(0>wait(0)) die("wait");
		}
	}
}
