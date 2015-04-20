#ifndef __QEMU_RR_H__
#define __QEMU_RR_H__

#include "qemu/typedefs.h"
#include "exec/memory.h"
#include <stdint.h>
#include <stdbool.h>

enum device_endian;

extern bool rr_record;
extern bool rr_replay;
extern bool rr_deterministic;
extern bool rr_exit;
extern bool rr_reading_clock;
extern bool rr_replaying;

uint64_t rr_get_current_icount(void);

extern bool rr_debug;
extern bool rr_debug_more;
extern bool rr_debug_error;
extern bool rr_debug_warning;
extern bool rr_debug_dummy;

#define RR_COND_DEBUG_AT(icount, cond, format, ...)     \
    (rr_debug_dummy = (cond) &&                         \
     (fprintf(stderr, "RR@ %"PRId64" " format "\n",     \
              icount, ##__VA_ARGS__), true))
#define RR_DEBUG_AT(icount, ...) \
    RR_COND_DEBUG_AT(icount, rr_debug, __VA_ARGS__)
#define RR_DEBUG_MORE_AT(icount, ...) \
    RR_COND_DEBUG_AT(icount, rr_debug_more, __VA_ARGS__)
#define RR_DEBUG_ERROR_AT(icount, ...) \
    RR_COND_DEBUG_AT(icount, rr_debug_error, __VA_ARGS__)
#define RR_DEBUG_WARNING_AT(icount, ...) \
    RR_COND_DEBUG_AT(icount, rr_debug_warning, __VA_ARGS__)

#define RR_CURR_ICOUNT()                       \
    (rr_deterministic ?                        \
     rr_get_current_icount() :                 \
     ((uint64_t)0 - 1))
#define RR_COND_DEBUG(cond, format, ...) \
    RR_COND_DEBUG_AT(RR_CURR_ICOUNT(), cond, format, ##__VA_ARGS__)
#define RR_DEBUG(...) RR_COND_DEBUG(rr_debug, __VA_ARGS__)
#define RR_DEBUG_MORE(...) RR_COND_DEBUG(rr_debug_more, __VA_ARGS__)
#define RR_DEBUG_ERROR(...) RR_COND_DEBUG(rr_debug_error, __VA_ARGS__)
#define RR_DEBUG_WARNING(...) RR_COND_DEBUG(rr_debug_warning, __VA_ARGS__)

bool rr_deterministic_init(void);
bool rr_record_init(const char *file);
bool rr_replay_init(const char *file);

bool rr_record_address_space(AddressSpace *as);
void rr_replay_address_space(AddressSpace *as);

static inline void rr_address_space(AddressSpace *as) {
    rr_record = rr_record && rr_record_address_space(as);
    if (rr_replay) {
        rr_replay_address_space(as);
    }
}

bool rr_record_cpu_start(void);
bool rr_replay_cpu_start(void);
static inline void rr_cpu_start(void)
{
    rr_replay = rr_replay && rr_replay_cpu_start();
    rr_record = rr_record && rr_record_cpu_start();
}

bool rr_replay_after_io_event(void);
static inline void rr_after_io_event(void)
{
    rr_replay = rr_replay && rr_replay_after_io_event();
}

void rr_do_bh_schedule(void);
static inline void rr_bh_schedule(void)
{
    if (rr_deterministic) {
        rr_do_bh_schedule();
    }
}

bool rr_do_bh_no_schedule(void);
static inline bool rr_bh_no_schedule(void)
{
    if (rr_deterministic) {
        return rr_do_bh_no_schedule();
    } else {
        return false;
    }
}

bool rr_do_replay_interrupt_request(uint32_t *interrupt_request);
static inline void rr_replay_interrupt_request(uint32_t *interrupt_request,
                                               uint32_t pass_mask)
{
    rr_replay = rr_replay && (((*interrupt_request & pass_mask) != 0) ||
                              rr_do_replay_interrupt_request(interrupt_request));
}

bool rr_do_record_interrupt_request(uint32_t interrupt_request);
static inline void rr_record_interrupt_request(uint32_t interrupt_request)
{
    rr_record = rr_record && rr_do_record_interrupt_request(interrupt_request);
}

bool rr_replay_exit_request(int *exit_request, int stage);
bool rr_record_exit_request(int stage);
static inline int rr_exit_request(int exit_request, int stage)
{
    int replay_exit_request = 0;
    rr_replay = rr_replay && rr_replay_exit_request(&replay_exit_request, stage);
    exit_request = exit_request || replay_exit_request;
#if 0
    if (!exit_request && rr_deterministic) {
        exit_request = (num_outstanding_ram_buffers() > 0);
        if (exit_request) {
            RR_DEBUG("Has %d outstanding_ram_buffers", 
                     num_outstanding_ram_buffers());
        }
    }
#endif
    if (exit_request) {
        rr_record = rr_record && rr_record_exit_request(stage);
    }
    return exit_request;
}

bool rr_do_record_intno(int intno);
static inline void rr_record_intno(int intno)
{
    rr_record = rr_record && ((intno < 0) || rr_do_record_intno(intno));
}

bool rr_do_replay_intno(int *intno);
static inline void rr_replay_intno(int *intno)
{
    rr_replay = rr_replay && rr_do_replay_intno(intno);
}

/*
 * Returns true if the request has been handled by replay.
 */
static inline
bool rr_prepare_address_space_write(AddressSpace *as, hwaddr addr, uint8_t *buf,
                                    int len)
{
    return rr_replay && !rr_replaying;
    ///* WERE TRYING THIS: */ return false;
}

bool rr_record_write(AddressSpace *as, uint64_t addr, const void *data,
                     uint32_t size);
static inline
void rr_address_space_write(AddressSpace *as, hwaddr addr, uint8_t *buf,
                            int len)
{
    rr_record = rr_record && rr_record_write(as, addr, buf, len);
}

extern int64_t rr_current_icount_bias;

void rr_replay_time(time_t *ti);
static inline void rr_time(time_t *ti)
{
    if (rr_replay) {
        rr_replay_time(ti);
    }
}

bool rr_do_record_clock_warp(int64_t warp_delta, uint64_t current_icount);
static inline void rr_record_clock_warp(int64_t warp_delta, uint64_t current_icount)
{
    rr_record = rr_record && rr_do_record_clock_warp(warp_delta, current_icount);
}

bool rr_do_record_reg32(int cpu_index, uint32_t reg, uint32_t reg32_val);
static inline void rr_record_reg32(int cpu_index, uint32_t reg,
                                   uint32_t reg32_val) {
    rr_record = rr_record && rr_do_record_reg32(cpu_index, reg, reg32_val);
}

#define SHIFT 0
#include "rr-template.h"
#define SHIFT 1
#include "rr-template.h"
#define SHIFT 2
#include "rr-template.h"
#define SHIFT 3
#include "rr-template.h"

#endif
