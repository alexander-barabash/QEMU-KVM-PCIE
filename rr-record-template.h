#include "rr-template-head.h"

#if DATA_SIZE < 8
bool rr_do_record_in(uint32_t addr, DATA_TYPE val)
{
    return
        record_entry(CPU_IN) &&
        (RR_DEBUG("IN address 0x%x", addr),
         RR_DEBUG("IN value %d 0x%x", DATA_BIT_SIZE, (uint32_t)val),
         true) &&
        bscript_value_write32(rr_stream.cpu_in_addr, addr) &&
        bscript_value_write(rr_stream.cpu_in_val, val);
}
#endif

bool rr_do_record_read(uint64_t addr, DATA_TYPE val)
{
    return
        record_entry(CPU_READ) &&
        (RR_DEBUG("Read address 0x%"PRIx64, addr),
         RR_DEBUG("Read value %d 0x%"PRIx64, DATA_BIT_SIZE, (uint64_t)val),
         true) &&
        bscript_value_write64(rr_stream.cpu_read_addr, addr) &&
        bscript_value_write(rr_stream.cpu_read_val, val);
}

#include "rr-template-tail.h"
