#include <bits/errno.h>
.text
.global __cp_begin
.hidden __cp_begin
.global __cp_end
.hidden __cp_end
.global __cp_end_break
.hidden __cp_end_break
.global __cp_break
.hidden __cp_break
.global __cp_cancel
.hidden __cp_cancel
.hidden __cancel
.global __syscall_cp_asm
.hidden __syscall_cp_asm
.type   __syscall_cp_asm,@function
__syscall_cp_asm:

/*
 long __syscall_cp_asm(volatile void *Cancel, volatile sig_atomic_t *Fresh, syscall_arg_t,
                      syscall_arg_t, syscall_arg_t, syscall_arg_t,
                      syscall_arg_t, syscall_arg_t, syscall_arg_t);

 USER CALL:			rdi, rsi, rdx, rcx r8, r9 rsp+8 rsp+16 rsp+24
 SYSTEM CALL: rax | rdi, rsi, rdx, r10, r8, r9 (destroys rcx && r11)
 */
__cp_begin:
	mov (%rdi),%eax
	test %eax, %eax
	jnz __cp_cancel

	mov (%rsi),%eax
	test %eax, %eax
	jnz __cp_break

	mov %rdx,%rax
	mov %rcx,%rdi
	mov %r8,%rsi
	mov %r9,%rdx
	mov 8(%rsp),%r10
	mov 16(%rsp),%r8
	mov 24(%rsp),%r9

__cp_end_break:
	syscall
__cp_end:
	ret

__cp_break:
	movq $-EINTR,%rax //could be already ECANCELED but k-code inside syscall is going to be interrupted w/ EINTR anyway
	ret

__cp_cancel:
	jmp __cancel
