.section .text

.global context_switch
.type context_switch, @function

# Switch execution between two kernel thread contexts.
#
# RDI = pointer to outgoing thread_context_t
# RSI = pointer to incoming thread_context_t
#
# The return address remains on each thread's kernel stack.
# When the outgoing thread is scheduled again, execution resumes
# immediately after its original context_switch() call.
context_switch:
    # Save the outgoing thread's callee-saved registers.
    mov %rsp, 0(%rdi)
    mov %rbx, 8(%rdi)
    mov %rbp, 16(%rdi)
    mov %r12, 24(%rdi)
    mov %r13, 32(%rdi)
    mov %r14, 40(%rdi)
    mov %r15, 48(%rdi)

    # Restore the incoming thread's callee-saved registers.
    mov 8(%rsi), %rbx
    mov 16(%rsi), %rbp
    mov 24(%rsi), %r12
    mov 32(%rsi), %r13
    mov 40(%rsi), %r14
    mov 48(%rsi), %r15

    # Switch to the incoming thread's kernel stack.
    mov 0(%rsi), %rsp

    # Resume execution at the return address on that stack.
    ret

.size context_switch, .-context_switch