.section .bss
.align 8

usermode_current_context:
    .quad 0

.section .text

.global usermode_enter
.global usermode_exit

# Enter Ring 3 using an IRETQ privilege transition.
#
# RDI = user instruction pointer
# RSI = user stack pointer
# RDX = process page-table root (physical address)
# RCX = pointer to usermode_context_t
usermode_enter:
    # Preserve the kernel's callee-saved registers.
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15

    # Remember which context belongs to the executing user thread.
    mov %rcx, usermode_current_context(%rip)

    # Save the kernel return context into the current thread's usermode context.
    mov %rsp, 0(%rcx) # kernel_rsp

    lea .return_to_kernel(%rip), %rax
    mov %rax, 8(%rcx) # kernel_return

    mov %cr3, %rax
    mov %rax, 16(%rcx) # kernel_cr3

    # Switch to the process address space.
    mov %rdx, %cr3

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

# Terminate the user program and resume its kernel thread.
#
# Restore the saved kernel stack and address space before returning.
usermode_exit:
    # Retrieve the current thread's saved kernel context.
    mov usermode_current_context(%rip), %rcx

    # Load everything before switching address spaces.
    mov 0(%rcx), %rsp
    mov 8(%rcx), %rdx
    mov 16(%rcx), %rax

    # Restore the kernel address space.
    mov %rax, %cr3

    # Resume the saved kernel execution context.
    jmp *%rdx