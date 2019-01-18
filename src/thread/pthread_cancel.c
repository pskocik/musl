#define _GNU_SOURCE
#include <string.h>
#include <errno.h>
#include "pthread_impl.h"
#include "syscall.h"
#include "libc.h"
#include <stdio.h>

long __cancel();
long __syscall_cp_asm(volatile void *, volatile sig_atomic_t*, syscall_arg_t,
                      syscall_arg_t, syscall_arg_t, syscall_arg_t,
                      syscall_arg_t, syscall_arg_t, syscall_arg_t);


hidden extern const char __cp_begin[1], __cp_end[1], __cp_end_break[1], __cp_break[1], __cp_cancel[1];

struct musl_sig_cancel_flags *musl_sig_cancel_flags(void)
{
	return &(__pthread_self())->cancel_flags;
}

int musl_sig_cancel__interrupt_syscall(void /*ucontext_t*/ *Uctx)
{
	/*If between the beginning of the assemnly layer of sycall wrapper and kernel entry,
	  force a return of -EINTR as if the func were already in kernel mode  */
	ucontext_t *uc = Uctx;
	uintptr_t pc = uc->uc_mcontext.MC_PC;
	if (pc >= (uintptr_t)__cp_begin && pc < (uintptr_t)__cp_end_break) {
		uc->uc_mcontext.MC_PC = (uintptr_t)__cp_break;
		return 0;
	}
	return -1;
}

int musl_sig_cancel__test(void)
{
	struct musl_sig_cancel_flags *cf = musl_sig_cancel_flags();
	int r; return ((r=cf->musl_sig_syscall_breaker)) ?
		(cf->musl_sig_syscall_breaker=0, cf->musl_sig_active=r) : cf->musl_sig_active;
}

long __syscall_cp_c(syscall_arg_t nr,
                    syscall_arg_t u, syscall_arg_t v, syscall_arg_t w,
                    syscall_arg_t x, syscall_arg_t y, syscall_arg_t z)
{
	pthread_t self;
	long r;
	int st;

	if ((st=(self=__pthread_self())->canceldisable)
	    && (st==PTHREAD_CANCEL_DISABLE || nr==SYS_close))
		return __syscall(nr, u, v, w, x, y, z);

	r = __syscall_cp_asm(&self->cancel, &self->cancel_flags.musl_sig_syscall_breaker, nr, u, v, w, x, y, z);

	if (r==-EINTR){
		if (nr!=SYS_close && self->cancel &&
			self->canceldisable != PTHREAD_CANCEL_DISABLE)
			return __cancel();
		else{
			/*on EINTR with musl_sig_syscall_breaker set, move musl_sig_syscall_breaker to musl_sig_active*/
			/*so that the info is maintained but the next cp syscall is enterrable*/
			if (self->cancel_flags.musl_sig_syscall_breaker) {
				self->cancel_flags.musl_sig_active = self->cancel_flags.musl_sig_syscall_breaker;
				self->cancel_flags.musl_sig_syscall_breaker = 0;
				r = -ECANCELED;
			}
		}
	}
	return r;
}

long __cancel()
{
	pthread_t self = __pthread_self();
	if (self->canceldisable == PTHREAD_CANCEL_ENABLE || self->cancelasync)
		pthread_exit(PTHREAD_CANCELED);
	self->canceldisable = PTHREAD_CANCEL_DISABLE;
	return -ECANCELED;
}

static void _sigaddset(sigset_t *set, int sig)
{
	unsigned s = sig-1;
	set->__bits[s/8/sizeof *set->__bits] |= 1UL<<(s&8*sizeof *set->__bits-1);
}

static void cancel_handler(int sig, siginfo_t *si, void *ctx)
{
	pthread_t self = __pthread_self();
	ucontext_t *uc = ctx;
	uintptr_t pc = uc->uc_mcontext.MC_PC;

	a_barrier();
	if (!self->cancel || self->canceldisable == PTHREAD_CANCEL_DISABLE) return;

	_sigaddset(&uc->uc_sigmask, SIGCANCEL);

	if (self->cancelasync || pc >= (uintptr_t)__cp_begin && pc < (uintptr_t)__cp_end) {
		uc->uc_mcontext.MC_PC = (uintptr_t)__cp_cancel;
#ifdef CANCEL_GOT
		uc->uc_mcontext.MC_GOT = CANCEL_GOT;
#endif
		return;
	}

	__syscall(SYS_tkill, self->tid, SIGCANCEL);
}

void __testcancel()
{
	pthread_t self = __pthread_self();
	if (self->cancel && !self->canceldisable)
		__cancel();
}

static void init_cancellation()
{
	struct sigaction sa = {
		.sa_flags = SA_SIGINFO | SA_RESTART,
		.sa_sigaction = cancel_handler
	};
	memset(&sa.sa_mask, -1, _NSIG/8);
	__libc_sigaction(SIGCANCEL, &sa, 0);
}

int pthread_cancel(pthread_t t)
{
	static int init;
	if (!init) {
		init_cancellation();
		init = 1;
	}
	a_store(&t->cancel, 1);
	if (t == pthread_self()) {
		if (t->canceldisable == PTHREAD_CANCEL_ENABLE && t->cancelasync)
			pthread_exit(PTHREAD_CANCELED);
		return 0;
	}
	return pthread_kill(t, SIGCANCEL);
}
