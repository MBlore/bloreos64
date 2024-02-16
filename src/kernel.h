#pragma once
void report_cpu_details();

// Halt and catch fire function.
static void hcf(void)
{
    asm("cli");
    for (;;) {
        asm("hlt");
    }
}
