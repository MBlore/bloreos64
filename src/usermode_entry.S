.section .bss
.align 8

.global usermode_kernel_rsp
usermode_kernel_rsp:
    .quad 0

.global usermode_kernel_return
usermode_kernel_return:
    .quad 0

.section .text

.global usermode_enter
.global usermode_exit

# Enter Ring 3 using an IRETQ privilege transition.
#
# RDI = user instruction pointer
# RSI = user stack pointer
usermode_enter:
    # Preserve the kernel's callee-saved registers.
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15

    # Remember the kernel context.
    mov %rsp, usermode_kernel_rsp(%rip)
    lea .return_to_kernel(%rip), %rax
    mov %rax, usermode_kernel_return(%rip)

    # Set user data segments.
    mov $0x1B, %ax
    mov %ax, %ds
    mov %ax, %es

    # Construct the Ring 3 IRETQ frame.
    pushq $0x1B
    push %rsi

    pushfq
    andq $~0x200, (%rsp)

    pushq $0x23
    push %rdi

    iretq

.return_to_kernel:
    # Restore the kernel's data segments.
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es

    # Restore the kernel's callee-saved registers.
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx

    ret

# Terminate the test user program and resume the kernel.
#
# Must only be called from the syscall handler after entering
# through usermode_enter.
usermode_exit:
    mov usermode_kernel_rsp(%rip), %rsp
    jmp *usermode_kernel_return(%rip)