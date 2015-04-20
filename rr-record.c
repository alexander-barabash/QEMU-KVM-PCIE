#include "rr-internal.h"

static struct rr_stream rr_stream;

static bool record_entry_at(uint64_t current_icount, RecordKind op)
{
    RR_DEBUG_AT(current_icount, "Record Entry %s", rr_record_kind_name(op));
    return bscript_value_write64_flag(rr_stream.entry, current_icount, op);
}

static bool record_entry(RecordKind op)
{
    return record_entry_at(get_current_icount(), op);
}

bool rr_record_address_space(AddressSpace *as)
{
    RR_DEBUG("Address space %s as_id=%d", as->name, as->as_record_id);
    return
        record_entry(ADDRESS_SPACE) &&
        bscript_value_write32(rr_stream.as, as->as_record_id) &&
        bscript_write_string(rr_stream.bstream, as->name);
}

static inline bool do_record_initial_time(void)
{
    if (!rr_initial_time) {
        time(&rr_initial_time);
    }

    return
        record_entry(INITIAL_TIME) &&
        bscript_write_s64(rr_stream.bstream, rr_initial_time);
}

static inline bool do_record_cpu_start(int cpu_index)
{
    return
        record_entry(CPU_START) &&
        bscript_value_write32(rr_stream.cpu_index, cpu_index);
}

bool rr_record_cpu_start(void)
{
    if (current_cpu->cpu_index == bscript_value_get32(rr_stream.cpu_index)) {
        return true;
    }

    return do_record_cpu_start(current_cpu->cpu_index);
}

bool rr_do_record_interrupt_request(uint32_t interrupt_request)
{
    if (!interrupt_request) {
        return true;
    }

    RR_DEBUG("interrupt_request %d", interrupt_request);

    return
        record_entry(INTERRUPT_REQUEST) &&
        bscript_value_write32(rr_stream.interrupt_request, interrupt_request);
}

bool rr_do_record_intno(int intno)
{
    return
        record_entry(INTNO) &&
        bscript_value_write32(rr_stream.intno, intno);
}

bool rr_record_exit_request(int stage)
{
    uint32_t op = (stage == 0) ? EXIT_REQUEST_0 : EXIT_REQUEST_1;

    RR_DEBUG("exit_request %d", stage);

    return record_entry(op);
}

bool rr_record_write(AddressSpace *as,
                     uint64_t addr,
                     const void *data,
                     uint32_t size)
{
    RR_DEBUG("Address space write %s size=%d",
            as->name, size);

    return
        record_entry(AS_WRITE) &&
        bscript_value_write32(rr_stream.io_as_id, as->as_record_id) &&
        bscript_value_write64(rr_stream.io_addr, addr) &&
        bscript_write_data(rr_stream.bstream, data, size);
}

bool rr_do_record_clock_warp(int64_t warp_delta, uint64_t current_icount)
{
    return
        record_entry_at(current_icount, CLOCK_WARP) &&
        bscript_value_write64(rr_stream.clock_warp, warp_delta);        
}

bool rr_do_record_reg32(int cpu_index, uint32_t reg, uint32_t reg32_val)
{
    return
        record_entry(REG32) &&
        bscript_value_write32(rr_stream.cpu_index, cpu_index) &&
        bscript_value_write32(rr_stream.reg, reg) &&
        bscript_value_write32(rr_stream.reg32_val, reg32_val);
}

#define SHIFT 0
#include "rr-record-template.h"
#define SHIFT 1
#include "rr-record-template.h"
#define SHIFT 2
#include "rr-record-template.h"
#define SHIFT 3
#include "rr-record-template.h"

bool rr_record_init(const char *file)
{
    rr_stream.bstream = bstream_init_for_output(file, "Record initialization failed");

    if (!rr_stream.bstream) {
        return false;
    }

    rr_init_debug();
    rr_init_bscript_values(&rr_stream);
    return (rr_record = do_record_initial_time() && do_record_cpu_start(0));
}

bool rr_record;
