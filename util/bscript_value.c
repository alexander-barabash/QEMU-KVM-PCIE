#include "qemu/bscript_value.h"
#include <glib.h>
#include <assert.h>

struct bscript_value *bscript_value_create(struct bstream *bstream,
                                           uint32_t val_width,
                                           uint32_t flag_width,
                                           bool ascending)
{
    struct bscript_value *s = (struct bscript_value *)g_malloc0(sizeof(*s));

    bscript_value_init(s, bstream, val_width, flag_width, ascending);
    return s;
}

void bscript_value_init(struct bscript_value *s,
                        struct bstream *bstream,
                        uint32_t val_width,
                        uint32_t flag_width,
                        bool ascending)
{
    assert(flag_width <= 32);

    s->bstream = bstream;
    s->val_bytes = 0;
    switch (val_width + flag_width) {
    case 1 ... 8:
        s->val_bytes = 1;
        break;
    case 9 ... 16:
        s->val_bytes = 2;
        break;
    case 17 ... 32:
        s->val_bytes = 4;
        break;
    case 33 ... 64:
        s->val_bytes = 8;
        break;
    }
    assert(s->val_bytes);
    s->val_mask.vu64 = (1ull << val_width) - 1;

    s->flag_width = flag_width;
    s->flag_mask = (1 << flag_width) - 1;
    s->ascending = ascending;
    s->val.vu64 = 0;
    s->old.vu64 = 0;
    s->flag = 0;
}

bool bscript_value_write(struct bscript_value *s)
{
    bool result = false;
    struct bstream *bstream = s->bstream;
    uint32_t val_bytes = s->val_bytes;
    uint32_t flag = s->flag & s->flag_mask;
    union bscript_value_val val;

    switch (val_bytes) {
    case 1:
    case 2:
    case 4:
        /* The computation is 32-bit. */
        val.vs32.v = s->val.vs32.v;
        val.vs32.v -= s->old.vs32.v;
        val.vs32.v <<= s->flag_width;
        val.vs32.v |= flag;
        switch (val_bytes) {
        case 1:
            if (s->ascending) {
                result = bscript_write_u8(bstream, val.vu8.v);
            } else {
                result = bscript_write_s8(bstream, val.vs8.v);
            }
            break;
        case 2:
            if (s->ascending) {
                result = bscript_write_u16(bstream, val.vu16.v);
            } else {
                result = bscript_write_s16(bstream, val.vs16.v);
            }
            break;
        case 4:
            if (s->ascending) {
                result = bscript_write_u32(bstream, val.vu32.v);
            } else {
                result = bscript_write_s32(bstream, val.vs32.v);
            }
            break;
        default:
            assert(false);
        }
        if (result) {
            s->old.vs32.v = s->val.vs32.v;
        }
        break;
    case 8:
        val.vs64 = s->val.vs64;
        val.vs64 -= s->old.vs64;
        val.vs64 <<= s->flag_width;
        val.vs64 |= flag;
        if (s->ascending) {
            result = bscript_write_u64(bstream, val.vu64);
        } else {
            result = bscript_write_s64(bstream, val.vs64);
        }
        if (result) {
            s->old.vs64 = s->val.vs64;
        }
        break;
    default:
        assert(false);
    }
    return result;
}

bool bscript_value_read(struct bscript_value *s)
{
    bool result = false;
    struct bstream *bstream = s->bstream;
    uint32_t val_bytes = s->val_bytes;
    union bscript_value_val delta;

    switch (val_bytes) {
    case 1:
    case 2:
        /* The computation is 32-bit. */
        delta.vu32.v = 0;
        /* fallthru */
    case 4:
        switch (val_bytes) {
        case 1:
            if (s->ascending) {
                result = bscript_read_u8(bstream, &delta.vu8.v);
            } else {
                result = bscript_read_s8(bstream, &delta.vs8.v);
            }
            break;
        case 2:
            if (s->ascending) {
                result = bscript_read_u16(bstream, &delta.vu16.v);
            } else {
                result = bscript_read_s16(bstream, &delta.vs16.v);
            }
            break;
        case 4:
            if (s->ascending) {
                result = bscript_read_u32(bstream, &delta.vu32.v);
            } else {
                result = bscript_read_s32(bstream, &delta.vs32.v);
            }
            break;
        default:
            assert(false);
        }
        if (result) {
            s->val.vs32.v = s->old.vs32.v;
            s->val.vs32.v += delta.vu32.v >> s->flag_width;
            s->val.vs32.v &= s->val_mask.vs32.v;
            s->old.vs32.v = s->val.vs32.v;
        }
        break;
    case 8:
        if (s->ascending) {
            result = bscript_read_u64(bstream, &delta.vu64);
        } else {
            result = bscript_read_s64(bstream, &delta.vs64);
        }
        if (result) {
            s->val.vs64 = s->old.vs64;
            s->val.vs64 += delta.vu64 >> s->flag_width;
            s->val.vs64 &= s->val_mask.vs64;
            s->old.vs64 = s->val.vs64;
        }
        break;
    default:
        assert(false);
    }
    if (result) {
        s->flag = delta.vu32.v & s->flag_mask;
    }
    return result;
}
