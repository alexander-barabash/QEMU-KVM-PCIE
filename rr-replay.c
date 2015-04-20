#include "rr-internal.h"

typedef bool ReadRecordFunc(void);
typedef void ApplyRecordFunc(void);

static struct rr_stream rr_stream;

static inline uint64_t get_next_icount(void)
{
    return bscript_value_get64(rr_stream.entry);
}

static inline uint32_t get_next_op(void)
{
    return bscript_value_get_flag(rr_stream.entry);
}

static inline int64_t get_next_clock_warp(void)
{
    return bscript_value_get64(rr_stream.clock_warp);
}

static inline uint32_t get_next_reg(void)
{
    return bscript_value_get32(rr_stream.reg);
}

static inline uint32_t get_next_reg32_val(void)
{
    return bscript_value_get32(rr_stream.reg32_val);
}

static inline int get_next_as_id(void)
{
    return bscript_value_get32(rr_stream.as);
}

static char *get_next_as_name(void)
{
    return bscript_string_value_get(rr_stream.as_name);
}

static inline int get_next_cpu_index(void)
{
    return bscript_value_get32(rr_stream.cpu_index);
}

static inline int get_next_interrupt_request(void)
{
    return bscript_value_get32(rr_stream.interrupt_request);
}

static inline int get_next_intno(void)
{
    return bscript_value_get32(rr_stream.intno);
}

static inline uint32_t get_next_cpu_in_addr(void)
{
    return bscript_value_get32(rr_stream.cpu_in_addr);
}

static inline uint64_t get_next_cpu_read_addr(void)
{
    return bscript_value_get64(rr_stream.cpu_read_addr);
}

static inline int32_t get_next_io_as_id(void)
{
    return bscript_value_get32(rr_stream.io_as_id);
}

static inline uint64_t get_next_io_addr(void)
{
    return bscript_value_get64(rr_stream.io_addr);
}

static inline void *get_next_io_data(uint32_t *data_size)
{
    return bscript_buffer_value_get(rr_stream.io_data, data_size);
}

static inline bool is_before(uint64_t icount1, uint64_t icount2)
{
    int64_t delta = icount1 - icount2;
    if (delta < 0) {
        return true;
    }
    if (delta + (1LL << (NUM_ICOUNT_BITS - 1)) < 0) {
        return true;
    }
    return false;
}

static bool rr_read_next_record(void);
static bool rr_replay_pending(ApplyRecordFunc **special_apply_record_fn);
static bool rr_replay_loop(void);

#define MAX_AS_REPLAY_ID 0x10000
typedef struct AddressSpaceReplayData
{
    AddressSpace *as;
    int as_id;
} AddressSpaceReplayData;

static GHashTable *get_as_replay_table(void)
{
    static GHashTable *the_table;
    if (the_table == NULL) {
        the_table = g_hash_table_new(g_str_hash, g_str_equal);
    }
    return the_table;
}

static AddressSpaceReplayData *get_as_replay_data(const char *as_name)
{
    AddressSpaceReplayData *as_data = NULL;
    if (as_name) {
        as_data = g_hash_table_lookup(get_as_replay_table(), as_name);
        if (!as_data) {
            as_data = g_malloc0(sizeof(*as_data));
            g_hash_table_replace(get_as_replay_table(),
                                 g_strdup(as_name),
                                 as_data);
        }
    }
    return as_data;
}

static AddressSpace **get_as_replay_array(unsigned min_size)
{
    static AddressSpace **the_array;
    static unsigned array_size;
    if (min_size > array_size) {
        the_array = (AddressSpace **)g_realloc(the_array,
                                               min_size * sizeof(*the_array));
        memset(the_array + array_size, 0,
               (min_size - array_size) * sizeof(*the_array));
        array_size = min_size;
    }
    return the_array;
}

static void register_address_space(void)
{
    const char *as_name = get_next_as_name();
    int as_id = get_next_as_id();
    AddressSpaceReplayData *as_data = get_as_replay_data(as_name);
    if (!as_data) {
        RR_DEBUG_ERROR("Attempt to register unnamed address space as_id=%d", as_id);
        return;
    }
    if ((as_id < 0) || (as_id >= MAX_AS_REPLAY_ID)) {
        RR_DEBUG_ERROR("Attempt to register address space %s as_id=%d",
                         as_name, as_id);
        return;
    }
    as_data->as_id = as_id;
    get_as_replay_array(as_id + 1)[as_id] = as_data->as;
    RR_DEBUG("Registered address space %s as_id=%d",
            as_name, as_id);
}

void rr_replay_address_space(AddressSpace *as)
{
    AddressSpaceReplayData *as_data = get_as_replay_data(as->name);
    int as_id;
    if (!as_data) {
        RR_DEBUG_ERROR("Attempt to register unnamed address space");
        return;
    }
    as_data->as = as;
    as_id = as_data->as_id;
    if (as_id) {
        get_as_replay_array(as_id + 1)[as_id] = as;
        RR_DEBUG("Created address space %s as_id=%d",
                as->name, as_id);
    } else {
        RR_DEBUG("Created address space %s", as->name);
    }
}

static void replay_as_write(void)
{
    int as_id = get_next_io_as_id();
    if ((as_id >= 0) || (as_id < MAX_AS_REPLAY_ID)) {
        AddressSpace *as = get_as_replay_array(as_id + 1)[as_id];
        if (as) {
            uint32_t data_size;
            void *data = get_next_io_data(&data_size);
            uint64_t addr = get_next_io_addr();
            bool old_rr_replaying = rr_replaying;

            RR_DEBUG("Address space write %s addr=0x%"PRIx64" size=%d",
                     as->name, addr, data_size);

            rr_replaying = true;
            // WERE TRYING TO REMOVE THIS
            address_space_rw(as, addr, data, data_size, true);
            rr_replaying = old_rr_replaying;
        } else {
            RR_DEBUG_ERROR("Address space %d not found", as_id);
        }
    } else {
        RR_DEBUG_ERROR("Address space %d not found", as_id);
    }
}

static void replay_clock_warp(void)
{
    add_icount_clock_bias(get_next_clock_warp());
}

static void replay_reg32(void)
{
    int cpu_index = get_next_cpu_index();
    uint32_t reg = get_next_reg();
    uint32_t reg32_val = get_next_reg32_val();
    CPUState *next_cpu;
    CPUArchState *env;
    if (reg < (int)(sizeof(env->regs) / sizeof(env->regs[0]))) {
        for (next_cpu = first_cpu; next_cpu; next_cpu = CPU_NEXT(next_cpu)) {
            if (next_cpu->cpu_index == cpu_index) {
                env = next_cpu->env_ptr;
                env->regs[reg] = reg32_val;
                break;
            }
        }
    }
}

static void do_replay_initial_time(void)
{
}

static void do_replay_cpu_start(void)
{
    if (get_next_cpu_index() != current_cpu->cpu_index) {
        current_cpu->exit_request = 1;
        /* TODO: switch CPU. */
    } else {
        RR_DEBUG("cpu_start");
    }
}

bool rr_replay_cpu_start(void)
{
    static ApplyRecordFunc *special_apply_record_fn[NUM_RECORD_KINDS] = {
        [CPU_START] = do_replay_cpu_start,
    };
    return rr_replay_pending(special_apply_record_fn);
}

bool rr_replay_after_io_event(void)
{
    static ApplyRecordFunc *special_apply_record_fn[NUM_RECORD_KINDS] = {
        [AS_WRITE] = replay_as_write,
        [REG32] = replay_reg32,
    };
    return rr_replay_pending(special_apply_record_fn);
}

static void do_replay_interrupt_request(void)
{
    *rr_stream.p_interrupt_request = get_next_interrupt_request();
    RR_DEBUG("Handling interrupt request");
}

bool rr_do_replay_interrupt_request(uint32_t *interrupt_request)
{
    static ApplyRecordFunc *special_apply_record_fn[NUM_RECORD_KINDS] = {
        [INTERRUPT_REQUEST] = do_replay_interrupt_request,
    };
    rr_stream.p_interrupt_request = interrupt_request;
    *interrupt_request = 0;
    return rr_replay_pending(special_apply_record_fn);
}

static void do_replay_intno(void)
{
    rr_stream.next_intno = get_next_intno();
}

bool rr_do_replay_intno(int *intno)
{
    static ApplyRecordFunc *special_apply_record_fn[NUM_RECORD_KINDS] = {
        [INTNO] = do_replay_intno,
    };
    if (rr_replay_pending(special_apply_record_fn)) {
        *intno = rr_stream.next_intno;
        return true;
    } else {
        return false;
    }
}

static void do_replay_exit_request(void)
{
    rr_stream.do_exit_request = 1;
    RR_DEBUG("exit_request");
}

bool rr_replay_exit_request(int *exit_request, int stage)
{
    static ApplyRecordFunc *special_apply_record_fn_0[NUM_RECORD_KINDS] = {
        [EXIT_REQUEST_0] = do_replay_exit_request,
    };
    static ApplyRecordFunc *special_apply_record_fn_1[NUM_RECORD_KINDS] = {
        [EXIT_REQUEST_1] = do_replay_exit_request,
    };
    ApplyRecordFunc **special_apply_record_fn = (stage == 0) ?
        special_apply_record_fn_0 : special_apply_record_fn_1;
     rr_stream.do_exit_request = 0;
    if (rr_replay_pending(special_apply_record_fn)) {
        *exit_request = rr_stream.do_exit_request;
        return true;
    } else {
        return false;
    }
}

#define SHIFT 0
#include "rr-replay-template.h"
#define SHIFT 1
#include "rr-replay-template.h"
#define SHIFT 2
#include "rr-replay-template.h"
#define SHIFT 3
#include "rr-replay-template.h"

static bool read_as_record(void)
{
    return
        bscript_value_read(rr_stream.as) &&
        bscript_string_value_read(rr_stream.as_name);
}

static bool read_as_write_record(void)
{
    return
        bscript_value_read(rr_stream.io_as_id) &&
        bscript_value_read(rr_stream.io_addr) &&
        bscript_buffer_value_read(rr_stream.io_data);
}

static bool read_cpu_start_record(void)
{
    return bscript_value_read(rr_stream.cpu_index);
}

static bool read_initial_time(void)
{
    int64_t t;
    return bscript_read_s64(rr_stream.bstream, &t) &&
        ((rr_initial_time = t), true);
}

static bool read_interrupt_request_record(void)
{
    return bscript_value_read(rr_stream.interrupt_request);
}

static bool read_intno_record(void)
{
    return bscript_value_read(rr_stream.intno);
}

static bool read_exit_request_record(void)
{
    return true;
}

static bool read_clock_warp(void)
{
    return bscript_value_read(rr_stream.clock_warp);
}

static bool read_reg32(void)
{
    return bscript_value_read(rr_stream.reg) &&
        bscript_value_read(rr_stream.cpu_index);
        bscript_value_read(rr_stream.reg32_val);
}

static ReadRecordFunc *read_record_fn[NUM_RECORD_KINDS] = {
    [INITIAL_TIME] = read_initial_time,
    [ADDRESS_SPACE] = read_as_record,
    [CPU_START] = read_cpu_start_record,
    [INTERRUPT_REQUEST] = read_interrupt_request_record,
    [INTNO] = read_intno_record,
    [EXIT_REQUEST_0] = read_exit_request_record,
    [EXIT_REQUEST_1] = read_exit_request_record,
    [AS_WRITE] = read_as_write_record,
    [CPU_IN8] = read_in_record8,
    [CPU_IN16] = read_in_record16,
    [CPU_IN32] = read_in_record32,
    [CPU_READ8] = read_read_record8,
    [CPU_READ16] = read_read_record16,
    [CPU_READ32] = read_read_record32,
    [CPU_READ64] = read_read_record64,
    [CLOCK_WARP] = read_clock_warp,
    [REG32] = read_reg32,
};

static ApplyRecordFunc *apply_record_fn[NUM_RECORD_KINDS] = {
    [INITIAL_TIME] = do_replay_initial_time,
    [ADDRESS_SPACE] = register_address_space,
    [CLOCK_WARP] = replay_clock_warp,
};

static bool read_next_record_header(void)
{
    return bscript_value_read(rr_stream.entry);
}

static bool rr_read_next_record(void)
{
    uint32_t next_op;
    if (read_next_record_header() &&
        (next_op = get_next_op()) &&
        (RR_DEBUG("Record Entry %s at %"PRId64,
                  rr_record_kind_name(next_op), get_next_icount()), true) &&
        (next_op < NUM_RECORD_KINDS) &&
        read_record_fn[next_op] &&
        read_record_fn[next_op]()) {
        return true;
    } else {
        RR_DEBUG_ERROR("End of replay data");
        return false;
    }
}

static bool rr_replay_pending(ApplyRecordFunc **special_apply_record_fn)
{
    uint64_t current_icount = get_current_icount();
    uint64_t next_icount;
    static uint64_t last_wrong_next_icount;
    uint32_t next_op;
    ApplyRecordFunc *fn;

    while (rr_replay) {
        next_op = get_next_op();
        next_icount = get_next_icount();
        if (is_before(next_icount, current_icount)) {
            if (next_icount != last_wrong_next_icount) {
                RR_DEBUG_ERROR("next record before current point: %"PRId64,
                               next_icount);
                last_wrong_next_icount = next_icount;
            }
        }
        if (is_before(current_icount, next_icount)) {
            switch (next_op) {
            default:
                cpu_set_rr_deadline(next_icount - rr_current_icount_bias);
                break;
            case CPU_IN8:
            case CPU_IN16:
            case CPU_IN32:
            case CPU_READ8:
            case CPU_READ16:
            case CPU_READ32:
            case CPU_READ64:
                cpu_set_rr_deadline(next_icount - rr_current_icount_bias); // TRYING
                break;
            }
            break;
        }
        if (!next_op || (next_op >= NUM_RECORD_KINDS)) {
            rr_replay = false;
            break;
        }
        if (special_apply_record_fn) {
            fn = special_apply_record_fn[next_op];
        } else {
            fn = NULL;
        }
        if (!fn) {
            fn = apply_record_fn[next_op];
        }
        if (!fn) {
            switch (next_op) {
            case CPU_START:
            case INTERRUPT_REQUEST:
            case INTNO:
            case EXIT_REQUEST_0:
            case EXIT_REQUEST_1:
            case AS_WRITE:
            case REG32:
                cpu_set_rr_deadline_immediate();
                break;
            }
            break;
        }
        fn();
        rr_replay = rr_replay && rr_read_next_record();
    }
    return rr_replay;
}

static bool rr_replay_loop(void)
{
    rr_replay =
        rr_replay && rr_read_next_record() && rr_replay_pending(NULL);
    return rr_replay;
}

void rr_replay_time(time_t *ti)
{
    *ti = get_rr_time();
}

bool rr_replay_init(const char *file)
{
    rr_stream.bstream = bstream_init_for_input(file, "Replay initialization failed");

    if (!rr_stream.bstream) {
        return false;
    }

    rr_init_debug();
    rr_init_bscript_values(&rr_stream);
    return (rr_replay = rr_replay_loop());
}

bool rr_replay;
bool rr_replaying;
