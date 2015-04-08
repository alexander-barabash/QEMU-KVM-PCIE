#ifndef __QEMU_RR_INTERNAL_H__
#define __QEMU_RR_INTERNAL_H__

#include "rr.h"
#include "qom/cpu.h"
#include "qemu/timer.h"
#include "qemu/bscript_value.h"
#include "qemu/bscript_string_value.h"
#include "qemu/bscript_buffer_value.h"

#define NUM_FLAG_BITS 5
#define NUM_ICOUNT_BITS (64 - NUM_FLAG_BITS)

typedef enum {
    NO_RECORD = 0,
    INITIAL_TIME = 1,
    CPU_START = 2,
    INTERRUPT_REQUEST = 3,
    INTNO = 4,
    EXIT_REQUEST_0 = 5,
    EXIT_REQUEST_1 = 6,
    AS_WRITE = 7,
    CPU_IN8 = 8,
    CPU_IN16 = 9,
    CPU_IN32 = 10,
    CPU_READ8 = 11,
    CPU_READ16 = 12,
    CPU_READ32 = 13,
    CPU_READ64 = 14,
    ADDRESS_SPACE = 15,
    CLOCK_WARP = 16,
    NUM_RECORD_KINDS = (1 << NUM_FLAG_BITS),
} RecordKind;

const char *rr_record_kind_name(RecordKind kind);

struct rr_stream {
    struct bstream *bstream;
    int next_intno;
    int do_exit_request;
    uint32_t *p_interrupt_request;
    struct bscript_value *as;
    struct bscript_value *entry;
    struct bscript_value *cpu_index;
    struct bscript_value *interrupt_request;
    struct bscript_value *intno;
    struct bscript_value *io_as_id;
    struct bscript_value *io_addr;
    struct bscript_value *cpu_in_addr;
    struct bscript_value *cpu_in8_val;
    struct bscript_value *cpu_in16_val;
    struct bscript_value *cpu_in32_val;
    struct bscript_value *cpu_read_addr;
    struct bscript_value *cpu_read8_val;
    struct bscript_value *cpu_read16_val;
    struct bscript_value *cpu_read32_val;
    struct bscript_value *cpu_read64_val;
    struct bscript_value *clock_warp;
    struct bscript_string_value *as_name;
    struct bscript_buffer_value *io_data;
};

void rr_init_debug(void);
void rr_init_bscript_values(struct rr_stream *rr_stream);

static inline uint64_t get_current_icount(void)
{
    uint64_t icount;
    CPUState *cpu = current_cpu;
    if (cpu) {
        rr_reading_clock = true;
    }
    icount = cpu_get_instruction_counter();
    if (cpu) {
        rr_reading_clock = false;
    }
    return icount + rr_current_icount_bias;
}

extern time_t rr_initial_time;

static inline time_t get_rr_time(void)
{
    return rr_initial_time + cpu_get_icount() / get_ticks_per_sec();
}

#endif
