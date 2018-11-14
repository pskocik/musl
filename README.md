MUSL with a thread-cancellation mechanism compatible with goto-based scope-cleanup
=====================================================================================

TLDR:
Designating a set of signals as cancellation/unwinding signals (asynchronous) and
turning them into synchronous errors (EINTR->ECANCELED) at cancellation points (typically long-blocking syscalls)
in a race-free way simplifies (all leak-free code becomes automatically cancellation-safe)
and generalizes (partial unwinding possible; failures in cleanup code possible (e.g., close() on a fd or the write() of a closing html tag)) cancellation handling in C.

With this scheme, most single threaded leak-free error propagating code becomes "cancellation-safe" without having to be
rewritten in terms of pthread_cleanup_{push,pop}.


Such an approach can be allowed with a small extension to the libc's API.
This modified musl implements such modifications.


The Problem
------------
The classical way to do scope cleanup in C is with gotos or equivalent code
(gotos generated with a transpiler or C code based on other equivalent mechanism)

Example:

		int helper_func(), get_resource(), close();
		int func(void)
		{
			int r;
			int resource0, resource1, resource2;
			if(0>(resource0=get_resource())) { r=-1; goto lbl0; }
			if(0>helper_func(0)) { r=-1; goto lbl1; }
			if(0>helper_func(1)) { r=-1; goto lbl1; }
			if(0>(resource1=get_resource())) { r=-1; goto lbl1; }
			if(0>helper_func(2)) { r=-1; goto lbl2; }
			if(0>helper_func(3)) { r=-1; goto lbl2; }
			if(0>(resource2=get_resource())) { r=-1; goto lbl2; }
			if(0>helper_func(3)) { r=-1; goto lbl3; }
			if(0>helper_func(4)) { r=-1; goto lbl3; }
			lbl3: if(0>close(resource2)) r=-1; //some cleanup functions (like close()) may fail
											   //to indicate IO or other errors
			lbl2: if(0>close(resource1)) r=-1;
			lbl1: if(0>close(resource0)) r=-1;
			lbl0:; return r;
		}


Unfortunately, with POSIX thread cancellation, such code won't be cancellation-safe
even though the equivalent C++ code using RAII would be (assuming glibc thread cancellation
mechanism which relies on exceptions).
Also, unlike with the C++ solution, the above C code won't be able to
"catch a cancellation" and cause it to stop before the thread's stack is fully unwound.
(A mechanism useful for e.g., signal based alarms).

POSIX offers no solution for the second problem (no partial unwinding).

For the first problem POSIX introduces the concept of cancellation safety wherein a function needs to use the
POSIX-provided pthread_cleanup_push/pthread_cleanup_pop mechanism for scope-cleanups instead of
the classical goto-based solution (any other solution for automating scope cleanup won't work either
if it doesn't use pthread_cleanup_{push,pop}).

This POSIX approach is clumsy (need to modify working reentrant single-threaded code,
need to create void-pointer-taking functions for what otherwise
would have been plain statements), doesn't provide for the possibility of
partial unwinding instead of complete cancellation, and like C++, it disallows the propagation
of failure information from cleanup functions (no failing destructors in C++ parlance).

Overview of the solution
------------
I've adapted MUSL to handle the classical solution, thereby making all correct non-leaking C code automatically cancellation-safe.

The idea is that a user registers a signal handler for a "cancelling" signal and in it, they

- set a well-known thread_local syscallwrapper_breaker flag
- call musl_try_to_break_syscall_wrapper(Uctx):

		void cancelling_handler(int Sig, siginfo_t *Info, void *Uctx)
		{
			#if 0
				//what I also do
				(void)maybe_dflt__(Sig,Info);
				/*broadcast the signal to other threads if it should be broadcasted*/
				if(broadcasted_eh__(BX_ro_ptr(Info))) return; /*will now get a targetted version*/
			#endif
			musl_cancellation_flags()->musl_syscall_breaker = Sig;
			(void)musl_sig_cancel__interrupt_syscall(Uctx);
			//do other stuff ...
			#if 0
				//what I also do
				//save entry mask
				cf->musl_sig_entry_mask = bx_sigset_t__to_ul64(&uctx->uc_sigmask);
				//set a specific mask to be set then this handler is left
				bx_sigset_t__from_ul64(&uctx->uc_sigmask, HNDLR_MASK);
			#endif
		}
	int main()
	{ //...
		sigaction(SIGUSR1, &(struct sigaction){ .sa_sigaction=cancelling_handler, .sa_flags=SA_SIGINFO|SA_INTERRUPT}, 0);
    }


After this SIGUSR1 will decidedly elicit a reponse in the 1 next potentially long-blocking syscall (which will move
the syscall_breaker information to saved_syscall_breaker, while clearing it).

If the signal arrives before the syscall, the thread-local musl_syscall_breaker flag wil get set and
the next long blocking call will be failed with ECANCELED without being entered (the flag will then get cleared).
If the signal arrives after while in the syscall wrapper after the flag check,
then the `(void)musl_sig_cancel__interrupt_syscall(Uctx)` call in the handler will break it by moving the Program Counter.
If the signal arrives while in kernel mode, the kernel will break it with EINTR and upon seing an EINTR
in conjunction with the musl_syscall_breaker flag, the syscall wrapper will translate it to ECANCELED.

If the signal arrives before the syscall, the thread-local musl_syscall_breaker flag wil get set and
the next long blocking call won't be enterered.


Correctly written (i.e., error-checking) non-leaking C code will react to the failure by
failing the current function, which should fail its caller, which should fail its
caller etc., etc. all while cleaning up resources allocated in their respective scope.
If the failure chain doesn't stop, at the current thread's root, the thread will
exit (=get cancelled).
