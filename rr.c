#include "rr-internal.h"
#include "qemu/bscript_value.h"
#include "exec/cpu-common.h"

void rr_init_bscript_values(struct rr_stream *rr_stream)
{
    struct bstream *bstream = rr_stream->bstream;
    rr_stream->entry = bscript_value_create(bstream, NUM_ICOUNT_BITS, NUM_FLAG_BITS, true);
    rr_stream->as = bscript_value_create(bstream, 32, 0, false);
    rr_stream->cpu_index = bscript_value_create(bstream, 32, 0, false);
    rr_stream->interrupt_request = bscript_value_create(bstream, 32, 0, false);
    rr_stream->intno = bscript_value_create(bstream, 32, 0, false);
    rr_stream->io_as_id = bscript_value_create(bstream, 32, 0, false);
    rr_stream->io_addr = bscript_value_create(bstream, 64, 0, false);
    rr_stream->cpu_in_addr = bscript_value_create(bstream, 32, 0, false);
    rr_stream->cpu_in8_val = bscript_value_create(bstream, 8, 0, false);
    rr_stream->cpu_in16_val = bscript_value_create(bstream, 16, 0, false);
    rr_stream->cpu_in32_val = bscript_value_create(bstream, 32, 0, false);
    rr_stream->cpu_read_addr = bscript_value_create(bstream, 64, 0, false);
    rr_stream->cpu_read8_val = bscript_value_create(bstream, 8, 0, false);
    rr_stream->cpu_read16_val = bscript_value_create(bstream, 16, 0, false);
    rr_stream->cpu_read32_val = bscript_value_create(bstream, 32, 0, false);
    rr_stream->cpu_read64_val = bscript_value_create(bstream, 64, 0, false);
    rr_stream->clock_warp = bscript_value_create(bstream, 64, 0, false);
    rr_stream->as_name = bscript_string_value_create(bstream);
    rr_stream->io_data = bscript_buffer_value_create(bstream);
}

const char *rr_record_kind_name(RecordKind kind)
{
    switch(kind) {
#define KIND_NAME(kind) case kind: return #kind
        KIND_NAME(NO_RECORD);
        KIND_NAME(INITIAL_TIME);
        KIND_NAME(CPU_START);
        KIND_NAME(INTERRUPT_REQUEST);
        KIND_NAME(INTNO);
        KIND_NAME(EXIT_REQUEST_0);
        KIND_NAME(EXIT_REQUEST_1);
        KIND_NAME(AS_WRITE);
        KIND_NAME(CPU_IN8);
        KIND_NAME(CPU_IN16);
        KIND_NAME(CPU_IN32);
        KIND_NAME(CPU_READ8);
        KIND_NAME(CPU_READ16);
        KIND_NAME(CPU_READ32);
        KIND_NAME(CPU_READ64);
        KIND_NAME(ADDRESS_SPACE);
        KIND_NAME(CLOCK_WARP);
        KIND_NAME(NUM_RECORD_KINDS);
    default:
        break;
    }
    return "unknown";
}

void rr_init_debug(void)
{
    const char *debug_level_string = getenv("RR_DEBUG");
    int debug_level;

    if (debug_level_string) {
        if (*debug_level_string) {
            debug_level = *debug_level_string - '0';
        } else {
            debug_level = 0;
        }
    } else {
        debug_level = 1;
    }
    switch (debug_level) {
    case 4:
        rr_debug_more = true;
    case 3:
        rr_debug = true;
    case 2:
        rr_debug_warning = true;
    default:
    case 1:
        rr_debug_error = true;
    case 0:
        break;
    }
}

void rr_do_bh_schedule(void)
{
    if (current_cpu) {
        cpu_set_rr_bh_deadline(get_current_icount() - rr_current_icount_bias);
        RR_DEBUG("Setting up bh deadline to %"PRId64,
                 get_current_icount() - rr_current_icount_bias);
    } else {
        RR_DEBUG("rr_do_bh_schedule off CPU at %"PRId64,
                 get_current_icount() - rr_current_icount_bias);
    }
}

bool rr_do_bh_no_schedule(void)
{
    if (current_cpu) {
        return false;
    } else {
        return false;
    }
}

uint64_t rr_get_current_icount(void)
{
    return get_current_icount();
}

bool rr_deterministic_init(void)
{
    rr_init_debug();
    return true;
}

bool rr_deterministic;
bool rr_exit;
time_t rr_initial_time;
int64_t rr_current_icount_bias;
bool rr_reading_clock;

bool rr_debug_error;
bool rr_debug_warning;
bool rr_debug;
bool rr_debug_more;

bool rr_debug_dummy;
