.altmacro

# Macro for generating the interrupt handler for a specific vector which expands
# in to the text section.
.macro THUNK num
    interrupt_thunk_\num:
        # Move the macro param number in to RDI (our fault vector number).
        mov $\num, %rdi
        cld
        call _handle_fault
        hlt
.endm

# Macro for writing out the label address in the data section.
.macro THUNKADDR num
    .quad interrupt_thunk_\num
.endm

# Generate the data table containing the label addresses for the generated labels.
.section .data
.global isr_thunks
.align 8
isr_thunks:
.set i,0
.rept 32
    THUNKADDR %i
    .set i,i+1
.endr

# Generate the interrupt handler code from the macro for each vector.
.section .text
.set i,0
.rept 32
    THUNK %i
    .set i,i+1
.endr
