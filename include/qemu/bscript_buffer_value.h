#ifndef __QEMU_BSCRIPT_BUFFER_VALUE_H__
#define __QEMU_BSCRIPT_BUFFER_VALUE_H__

#include "qemu/bscript.h"

struct bscript_buffer_value {
    /* all fields private */
    /* use access functions below. */
    struct bstream *bstream;
    void *data_buffer;
    uint32_t data_buffer_size;
    uint32_t data_size;
};

static inline void bscript_buffer_value_init(struct bscript_buffer_value *s,
                                             struct bstream *bstream)
{
    s->bstream = bstream;
}

static inline void bscript_buffer_value_clear(struct bscript_buffer_value *s)
{
    g_free(s->data_buffer);
    s->data_buffer = NULL;
    s->data_buffer_size = 0;
    s->data_size = 0;
}

static inline struct bscript_buffer_value *
bscript_buffer_value_create(struct bstream *bstream)
{
    struct bscript_buffer_value *s = g_malloc0(sizeof(*s));
    bscript_buffer_value_init(s, bstream);
    return s;
}

static inline void bscript_buffer_value_destroy(struct bscript_buffer_value *s)
{
    bscript_buffer_value_clear(s);
    g_free(s);
}

static inline void *bscript_buffer_value_get(struct bscript_buffer_value *s,
                                             uint32_t *data_size)
{
    *data_size = s->data_size;
    return s->data_buffer;
}

static inline bool bscript_buffer_value_read(struct bscript_buffer_value *s)
{
    return bscript_read_data(s->bstream, &s->data_buffer, &s->data_buffer_size,
                             &s->data_size);
}

#endif

