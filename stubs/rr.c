#include "rr.h"
#include "qemu/main-loop.h"
bool rr_replay;
bool rr_record;
bool rr_deterministic;
bool rr_exit;
bool rr_reading_clock;
bool rr_debug;
bool rr_debug_dummy;
void rr_do_bh_schedule(void)
{
}
bool rr_do_bh_no_schedule(void)
{
    return false;
}
