.section .text

.global ISR_Handler_Syscall
.extern syscall_dispatch

ISR_Handler_Syscall:
    # Save all general-purpose registers.
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    cld

    # First argument: system-call number from saved RAX.
    mov 112(%rsp), %rdi

    # Second argument: privilege level from interrupted CS.
    mov 128(%rsp), %rsi
    and $3, %rsi

    # Preserve the saved-register stack pointer and align for C.
    mov %rsp, %r12
    and $-16, %rsp

    call syscall_dispatch

    # Restore the saved-register stack pointer.
    mov %r12, %rsp

    # Replace saved RAX with the system-call return value.
    mov %rax, 112(%rsp)

    # Restore all general-purpose registers.
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax

    iretq