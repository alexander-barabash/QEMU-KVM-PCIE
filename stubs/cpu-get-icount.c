#include "qemu-common.h"
#include "qemu/timer.h"

int use_icount;

int64_t cpu_get_icount(void)
{
    abort();
}

int64_t cpu_get_instruction_counter(void)
{
    abort();
}
