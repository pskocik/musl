#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
volatile sig_atomic_t cancel;
void action(int Sig, siginfo_t *Info, void *Uctx)
{

	cancel=Sig;
#if defined(musl_sig_cancel__interrupt_syscall) && !FRAGILE
	musl_sig_cancel_flags()->musl_sig_syscall_breaker=Sig;
	musl_sig_cancel__interrupt_syscall(Uctx);
#endif

}
int testcancel(void)
{
	int r;return ( (r=cancel) )?
#if defined(musl_sig_cancel__interrupt_syscall) && !FRAGILE
		(cancel=0,musl_sig_cancel__test(),r)
#else
		(cancel=0,r)
#endif
		: r;
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
	sigaction(SIGALRM,&(const struct sigaction){
				.sa_sigaction=action,
				.sa_flags=SA_SIGINFO,
			}, 0);
	long i=0, top = V[1] ? atoi(V[1]) : 100000;
	sigset_t sigs, old;
	sigfillset(&sigs); sigdelset(&sigs,SIGINT); sigdelset(&sigs,SIGTERM); sigdelset(&sigs,SIGQUIT);

	srand(time(0));
	for(;i<top;i++){
		sigprocmask(SIG_SETMASK, &sigs, &old);
		(void)testcancel();
		fprintf(stderr, "%ld \n", i);
		sigprocmask(SIG_SETMASK, &old, 0);

		if(0>setitimer(ITIMER_REAL, &(struct itimerval){ .it_value = { .tv_usec=1} }, NULL))
			die("setitimer");
		if(0>my_read(0,buf,sizeof(buf))) /*perror("read")*/ ;
		cancel=0;
	}
}
