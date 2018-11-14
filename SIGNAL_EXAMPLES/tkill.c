#include <pthread.h>
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
	/*cancel=Sig;*/
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
void* thr(void *Arg)
{
	if(0>my_read(0,buf,sizeof(buf)))
		/*perror("read")*/
			;
	pthread_exit(0);

}
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
		pthread_t ptid;
		if(0!=pthread_create(&ptid,0,thr,0)) die("pthread");
		enum { ms = 1000 } ;
		#if !TIMEOUT
			#define TIMEOUT 500
		#endif
		//setitimer(ITIMER_REAL,&(const struct itimerval){.it_value.tv_usec=TIMEOUT*ms},0);
		usleep(200);
		#if 1
			if(0!=(errno=pthread_kill(ptid,SIGUSR1))) die("kill");
		#else
			if(0!=(errno=pthread_cancel(ptid))) die("kill");
		#endif
		if(0!=(errno=pthread_join(ptid,0))) die("wait");
		//setitimer(ITIMER_REAL,&(const struct itimerval){},0);
	}
}
