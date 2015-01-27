#ifndef __QEMU_BSCRIPT_STRING_VALUE_H__
#define __QEMU_BSCRIPT_STRING_VALUE_H__

#include "qemu/bscript.h"

struct bscript_string_value {
    /* all fields private */
    /* use access functions below. */
    struct bstream *bstream;
    void *data_buffer;
    uint32_t data_buffer_size;
    char *string;
};

static inline void bscript_string_value_init(struct bscript_string_value *s,
                                             struct bstream *bstream)
{
    s->bstream = bstream;
}

static inline void bscript_string_value_clear(struct bscript_string_value *s)
{
    g_free(s->data_buffer);
    s->data_buffer = NULL;
    s->data_buffer_size = 0;
    s->string = NULL;
}

static inline struct bscript_string_value *
bscript_string_value_create(struct bstream *bstream)
{
    struct bscript_string_value *s = g_malloc0(sizeof(*s));
    bscript_string_value_init(s, bstream);
    return s;
}

static inline void bscript_string_value_destroy(struct bscript_string_value *s)
{
    bscript_string_value_clear(s);
    g_free(s);
}

static inline char *bscript_string_value_get(struct bscript_string_value *s)
{
    return s->string;
}

static inline bool bscript_string_value_read(struct bscript_string_value *s)
{
    return bscript_read_string(s->bstream, &s->data_buffer, &s->data_buffer_size,
                               &s->string);
}

#endif

