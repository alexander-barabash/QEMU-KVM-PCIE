#include "rr-template-head.h"

#if DATA_SIZE < 8
bool rr_replay_retreive_in(uint32_t addr, DATA_TYPE *val);
bool rr_do_record_in(uint32_t addr, DATA_TYPE val);
#endif

bool rr_replay_retreive_read(uint64_t addr, DATA_TYPE *val);
bool rr_do_record_read(uint64_t addr, DATA_TYPE val);

#if DATA_SIZE < 8
static inline void rr_prepare_in(uint32_t addr)
{
}

static inline void rr_in(uint32_t addr, DATA_TYPE *val)
{
    if (rr_replay) {
        DATA_TYPE orig_val = *val;
        rr_replay = rr_replay_retreive_in(addr, val);
        if (orig_val != *val) {
            RR_DEBUG_WARNING("Replayed value differs in rr_in 0x%x not 0x%x",
                             (int)*val, (int)orig_val);
        }
    }
    rr_record = rr_record && rr_do_record_in(addr, *val);
}

static inline void rr_prepare_out(uint32_t addr, DATA_TYPE val)
{
}

static inline void rr_out(uint32_t addr, DATA_TYPE val)
{
}
#endif

static inline void rr_read(uint64_t addr, DATA_TYPE *val)
{
    if (rr_replay) {
        DATA_TYPE orig_val = *val;
        rr_replay = rr_replay_retreive_read(addr, val);
        if (orig_val != *val) {
            RR_DEBUG_WARNING("Replayed value differs in rr_read 0x%"PRIx64" not 0x%"PRIx64,
                             (uint64_t)*val, (uint64_t)orig_val);
        }
    }
    rr_record = rr_record && rr_do_record_read(addr, *val);
}

static inline void rr_write(uint64_t addr, DATA_TYPE val)
{
}

#include "rr-template-tail.h"
