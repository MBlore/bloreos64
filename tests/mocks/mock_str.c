/* Suppress kernel diagnostic output during unit tests. */
void kprintf(const char format[], ...)
{
    (void)format;
}