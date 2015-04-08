#include "rr-template-head.h"

#define get_next_cpu_in glue(get_next_cpu_in, DATA_BIT_SIZE)
#define get_next_cpu_read glue(get_next_cpu_read, DATA_BIT_SIZE)

#if DATA_SIZE < 8
static inline DATA_TYPE get_next_cpu_in(void)
{
    return bscript_value_get(rr_stream.cpu_in_val);
}

bool rr_replay_retreive_in(uint32_t addr, DATA_TYPE *val)
{
    int64_t icount_bias = get_next_icount() - get_current_icount();
    RR_DEBUG("IN address 0x%x", addr);

    if (get_next_op() != CPU_IN) {
        RR_DEBUG_ERROR("IN: next_op is %s and not %s",
                rr_record_kind_name(get_next_op()), rr_record_kind_name(CPU_IN));
        return false;
    }
    if (addr != get_next_cpu_in_addr()) {
        RR_DEBUG_ERROR("Stored IN address 0x%x", get_next_cpu_in_addr());
        return false;
    }
    if (icount_bias) {
        RR_DEBUG_ERROR("IN: Wrong icount by %"PRId64, icount_bias);
        shift_instruction_counter(icount_bias);
    }
    *val = get_next_cpu_in();    
    RR_DEBUG("IN value %d 0x%x", DATA_BIT_SIZE, *val);
    return rr_replay_loop();
}

static bool read_in_record(void)
{
    return
        bscript_value_read(rr_stream.cpu_in_addr) &&
        bscript_value_read(rr_stream.cpu_in_val);
}
#endif

static inline DATA_TYPE get_next_cpu_read(void)
{
    return bscript_value_get(rr_stream.cpu_read_val);
}

bool rr_replay_retreive_read(uint64_t addr, DATA_TYPE *val)
{
    uint64_t current_icount = get_current_icount();
    int64_t icount_bias = get_next_icount() - current_icount;
    RR_DEBUG("Read address 0x%"PRIx64, addr);
    if (get_next_op() != CPU_READ) {
        RR_DEBUG_ERROR("READ: next_op is %s and not %s",
                rr_record_kind_name(get_next_op()), rr_record_kind_name(CPU_READ));
        return false;
    }
    if (addr != get_next_cpu_read_addr()) {
        RR_DEBUG_ERROR("Stored read address 0x%"PRIx64, get_next_cpu_read_addr());
        rr_debug = false;
        return false;
    }
    if (get_next_icount() != current_icount) {
        RR_DEBUG_ERROR("READ: Wrong icount by %"PRId64, icount_bias);
        shift_instruction_counter(icount_bias);
    }
    if (current_icount == 1119285248) {
        fprintf(stderr, "HERE\n");
    }
    *val = get_next_cpu_read();
    RR_DEBUG("Read value %d 0x%"PRIx64, DATA_BIT_SIZE, (uint64_t)*val);
    return rr_replay_loop();
}

static bool read_read_record(void)
{
    return
        bscript_value_read(rr_stream.cpu_read_addr) &&
        bscript_value_read(rr_stream.cpu_read_val);
}

#undef get_next_cpu_in
#undef get_next_cpu_read

#include "rr-template-tail.h"
