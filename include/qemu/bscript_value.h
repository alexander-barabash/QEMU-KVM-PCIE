#ifndef __QEMU_BSCRIPT_VALUE_H__
#define __QEMU_BSCRIPT_VALUE_H__

#include "qemu/bscript.h"

union bscript_value_val {
    struct {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        int32_t fill32;
        int16_t fill16;
        int8_t fill8;
#endif
        int8_t v;
    } vs8;
    struct {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        int32_t fill32;
        int16_t fill16;
        int8_t fill8;
#endif
        uint8_t v;
    } vu8;
    struct {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        int32_t fill32;
        int16_t fill16;
#endif
        int16_t v;
    } vs16;
    struct {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        int32_t fill32;
        int16_t fill16;
#endif
        uint16_t v;
    } vu16;
    struct {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        int32_t fill32;
#endif
        int32_t v;
    } vs32;
    struct {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        int32_t fill32;
#endif
        uint32_t v;
    } vu32;
    int64_t vs64;
    uint64_t vu64;
};

struct bscript_value {
    /* all fields private */
    /* use access functions below. */
    struct bstream *bstream;
    uint32_t val_bytes;
    uint32_t flag_width;
    bool ascending;
    uint32_t flag_mask;
    union bscript_value_val val_mask;
    union bscript_value_val old;
    union bscript_value_val val;
    uint32_t flag;
};

void bscript_value_init(struct bscript_value *s,
                        struct bstream *bstream,
                        uint32_t val_width,
                        uint32_t flag_width,
                        bool ascending);

struct bscript_value *bscript_value_create(struct bstream *bstream,
                                           uint32_t val_width,
                                           uint32_t flag_width,
                                           bool ascending);

static inline void bscript_value_set8(struct bscript_value *s, uint8_t v)
{
    s->val.vu8.v = v;
    s->val.vu32.v &= s->val_mask.vu32.v;
}
static inline void bscript_value_set16(struct bscript_value *s, uint16_t v)
{
    s->val.vu16.v = v;
    s->val.vu32.v &= s->val_mask.vu32.v;
}
static inline void bscript_value_set32(struct bscript_value *s, uint32_t v)
{
    s->val.vu32.v = v;
    s->val.vu32.v &= s->val_mask.vu32.v;
}
static inline void bscript_value_set64(struct bscript_value *s, uint64_t v)
{
    s->val.vu64 = v;
    s->val.vu64 &= s->val_mask.vu64;
}
static inline void bscript_value_set_flag(struct bscript_value *s, uint32_t flag)
{
    s->flag = flag;
}

static inline int8_t bscript_value_get8(struct bscript_value *s)
{
    return s->val.vs8.v;
}
static inline int16_t bscript_value_get16(struct bscript_value *s)
{
    return s->val.vs16.v;
}
static inline int32_t bscript_value_get32(struct bscript_value *s)
{
    return s->val.vs32.v;
}
static inline int64_t bscript_value_get64(struct bscript_value *s)
{
    return s->val.vs64;
}
static inline uint32_t bscript_value_get_flag(struct bscript_value *s)
{
    return s->flag;
}

bool bscript_value_write(struct bscript_value *s);
bool bscript_value_read(struct bscript_value *s);

static inline bool bscript_value_write8(struct bscript_value *s, uint8_t v)
{
    bscript_value_set8(s, v);
    return bscript_value_write(s);
}

static inline bool bscript_value_write16(struct bscript_value *s, uint16_t v)
{
    bscript_value_set16(s, v);
    return bscript_value_write(s);
}

static inline bool bscript_value_write32(struct bscript_value *s, uint32_t v)
{
    bscript_value_set32(s, v);
    return bscript_value_write(s);
}

static inline bool bscript_value_write64(struct bscript_value *s, uint64_t v)
{
    bscript_value_set64(s, v);
    return bscript_value_write(s);
}

static inline bool bscript_value_write32_flag(struct bscript_value *s,
                                              uint32_t v, uint32_t flag)
{
    bscript_value_set32(s, v);
    bscript_value_set_flag(s, flag);
    return bscript_value_write(s);
}

static inline bool bscript_value_write64_flag(struct bscript_value *s,
                                              uint64_t v, uint32_t flag)
{
    bscript_value_set64(s, v);
    bscript_value_set_flag(s, flag);
    return bscript_value_write(s);
}

static inline bool bscript_value_read8(struct bscript_value *s, int8_t *v)
{
    if (bscript_value_read(s)) {
        *v = bscript_value_get8(s);
        return true;
    } else {
        return false;
    }
}

static inline bool bscript_value_read16(struct bscript_value *s, int16_t *v)
{
    if (bscript_value_read(s)) {
        *v = bscript_value_get16(s);
        return true;
    } else {
        return false;
    }
}

static inline bool bscript_value_read32(struct bscript_value *s, int32_t *v)
{
    if (bscript_value_read(s)) {
        *v = bscript_value_get32(s);
        return true;
    } else {
        return false;
    }
}

static inline bool bscript_value_read64(struct bscript_value *s, int64_t *v)
{
    if (bscript_value_read(s)) {
        *v = bscript_value_get64(s);
        return true;
    } else {
        return false;
    }
}

static inline bool bscript_value_readu8(struct bscript_value *s, uint8_t *v)
{
    if (bscript_value_read(s)) {
        *v = bscript_value_get8(s);
        return true;
    } else {
        return false;
    }
}

static inline bool bscript_value_readu16(struct bscript_value *s, uint16_t *v)
{
    if (bscript_value_read(s)) {
        *v = bscript_value_get16(s);
        return true;
    } else {
        return false;
    }
}

static inline bool bscript_value_readu32(struct bscript_value *s, uint32_t *v)
{
    if (bscript_value_read(s)) {
        *v = bscript_value_get32(s);
        return true;
    } else {
        return false;
    }
}

static inline bool bscript_value_readu64(struct bscript_value *s, uint64_t *v)
{
    if (bscript_value_read(s)) {
        *v = bscript_value_get64(s);
        return true;
    } else {
        return false;
    }
}

static inline bool bscript_value_read32_flag(struct bscript_value *s,
                                             int32_t *v, uint32_t *flag)
{
    if (bscript_value_read(s)) {
        *v = bscript_value_get32(s);
        *flag = bscript_value_get_flag(s);
        return true;
    } else {
        return false;
    }
}

static inline bool bscript_value_read64_flag(struct bscript_value *s,
                                             int64_t *v, uint32_t *flag)
{
    if (bscript_value_read(s)) {
        *v = bscript_value_get64(s);
        *flag = bscript_value_get_flag(s);
        return true;
    } else {
        return false;
    }
}

static inline bool bscript_value_readu32_flag(struct bscript_value *s,
                                              uint32_t *v, uint32_t *flag)
{
    if (bscript_value_read(s)) {
        *v = bscript_value_get32(s);
        *flag = bscript_value_get_flag(s);
        return true;
    } else {
        return false;
    }
}

static inline bool bscript_value_readu64_flag(struct bscript_value *s,
                                              uint64_t *v, uint32_t *flag)
{
    if (bscript_value_read(s)) {
        *v = bscript_value_get64(s);
        *flag = bscript_value_get_flag(s);
        return true;
    } else {
        return false;
    }
}

#endif
