
.section .text

.global _start
.extern main

# Entry point for a BloreOS user-space executable.
#
# Initial RSP points to:
#   [RSP + 0]  argc
#   [RSP + 8]  argv[0]
#   [RSP + 16] argv[1]
#   ...
#
# Pass argc in RDI and argv in RSI, following
# the System V AMD64 calling convention.
_start:
    mov (%rsp), %rdi
    lea 8(%rsp), %rsi

    call main

    # main() returns an integer exit status in EAX.
    # SYS_EXIT = 4, status passed in RDI.
    mov %eax, %edi
    mov $4, %eax
    int $0x80

    # SYS_EXIT must never return.
1:
    jmp 1b
