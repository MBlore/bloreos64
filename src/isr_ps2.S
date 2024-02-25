.global ISR_Handler_PS2
.extern _handle_keyboard

ISR_Handler_PS2:
    # Save general registers.
    push %r15
    push %r14
    push %r13
    push %r12
    push %r11
    push %r10
    push %r9
    push %r8
    push %rbp
    push %rdi
    push %rsi
    push %rdx
    push %rcx
    push %rbx
    push %rax
    mov %es, %eax
    push %rax
    mov %ds, %eax
    push %rax

    # C functions expect the direction flag to be cleared on entry.
    cld

    call _handle_keyboard

    # Restore general registers.
    pop %rax
    mov %eax, %ds
    pop %rax
    mov %eax, %es
    pop %rax
    pop %rbx
    pop %rcx
    pop %rdx
    pop %rsi
    pop %rdi
    pop %rbp
    pop %r8
    pop %r9
    pop %r10
    pop %r11
    pop %r12
    pop %r13
    pop %r14
    pop %r15

    # Return from the interrupt.
    iretq