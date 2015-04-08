#define DATA_SIZE (1 << SHIFT)

#if DATA_SIZE == 8
#define SUFFIX q
#define LSUFFIX q
#define SDATA_TYPE  int64_t
#define DATA_TYPE  uint64_t
#define DATA_BIT_SIZE 64
#elif DATA_SIZE == 4
#define SUFFIX l
#define LSUFFIX l
#define SDATA_TYPE  int32_t
#define DATA_TYPE  uint32_t
#define DATA_BIT_SIZE 32
#elif DATA_SIZE == 2
#define SUFFIX w
#define LSUFFIX uw
#define SDATA_TYPE  int16_t
#define DATA_TYPE  uint16_t
#define DATA_BIT_SIZE 16
#elif DATA_SIZE == 1
#define SUFFIX b
#define LSUFFIX ub
#define SDATA_TYPE  int8_t
#define DATA_TYPE  uint8_t
#define DATA_BIT_SIZE 8
#else
#error unsupported data size
#endif

#define rr_replay_retreive_in glue(rr_replay_retreive_in, SUFFIX)
#define rr_replay_retreive_read glue(rr_replay_retreive_read, SUFFIX)
#define rr_do_record_in glue(rr_do_record_in, SUFFIX)
#define rr_do_record_read glue(rr_do_record_read, SUFFIX)
#define rr_prepare_in glue(rr_prepare_in, SUFFIX)
#define rr_in glue(rr_in, SUFFIX)
#define rr_prepare_out glue(rr_prepare_out, SUFFIX)
#define rr_out glue(rr_out, SUFFIX)
#define rr_read glue(rr_read, SUFFIX)
#define rr_write glue(rr_write, SUFFIX)

#define read_in_record glue(read_in_record, DATA_BIT_SIZE)
#define read_read_record glue(read_read_record, DATA_BIT_SIZE)

#define CPU_IN glue(CPU_IN, DATA_BIT_SIZE)
#define CPU_READ glue(CPU_READ, DATA_BIT_SIZE)

#define cpu_in_val glue(glue(cpu_in, DATA_BIT_SIZE), _val)
#define cpu_read_val glue(glue(cpu_read, DATA_BIT_SIZE), _val)

#define bscript_value_write glue(bscript_value_write, DATA_BIT_SIZE)
#define bscript_value_readu glue(bscript_value_readu, DATA_BIT_SIZE)
#define bscript_value_get glue(bscript_value_get, DATA_BIT_SIZE)
