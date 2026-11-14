.section .text

.global ISR_Handler_HPET
.global ISR_Handler_LAPIC_Timer

.extern _handle_timer
.extern _handle_lapic_timer

.macro TIMER_ISR name, handler
\name:
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

    # Save the interrupt-frame stack pointer.
    mov %rsp, %r12

    # Align the stack before calling C.
    and $-16, %rsp

    call \handler

    # Restore the interrupt-frame stack pointer.
    mov %r12, %rsp

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
.endm

TIMER_ISR ISR_Handler_HPET, _handle_timer
TIMER_ISR ISR_Handler_LAPIC_Timer, _handle_lapic_timer