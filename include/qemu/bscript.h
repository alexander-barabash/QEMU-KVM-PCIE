#ifndef __QEMU_BSCRIPT_H__
#define __QEMU_BSCRIPT_H__

#include "qemu/bstream.h"

#include <stdint.h>
#include <stdbool.h>

static inline bool bscript_write_u8(struct bstream *bstream, uint8_t c)
{
    return bstream_write_raw_data(bstream, &c, 1);
}
static inline bool bscript_read_u8(struct bstream *bstream, uint8_t *c)
{
    return bstream_read_raw_data(bstream, c, 1);
}

extern bool bscript_write_u16(struct bstream *bstream, uint16_t us);
extern bool bscript_read_u16(struct bstream *bstream, uint16_t *us);
extern bool bscript_write_u32(struct bstream *bstream, uint32_t u);
extern bool bscript_read_u32(struct bstream *bstream, uint32_t *u);
extern bool bscript_write_u64(struct bstream *bstream, uint64_t number);
extern bool bscript_read_u64(struct bstream *bstream, uint64_t *number);
extern bool bscript_write_symmetric_u32(struct bstream *bstream, uint32_t u);
extern bool bscript_read_symmetric_u32(struct bstream *bstream, uint32_t *u);
extern bool bscript_write_symmetric_u64(struct bstream *bstream, uint64_t ull);
extern bool bscript_read_symmetric_u64(struct bstream *bstream, uint64_t *ull);

static inline bool bscript_write_s8(struct bstream *bstream, int8_t c)
{
    return bscript_write_u8(bstream, (uint8_t)c);
}
static inline bool bscript_read_s8(struct bstream *bstream, int8_t *c)
{
    return bscript_read_u8(bstream, (uint8_t *)c);
}

static inline bool bscript_write_s16(struct bstream *bstream, int16_t number)
{
    if (number < 0) {
        return bscript_write_u16(bstream, (uint16_t)(((~number) << 1) | 1));
    } else {
        return bscript_write_u16(bstream, (uint16_t)(number << 1));
    }
}
static inline bool bscript_read_s16(struct bstream *bstream, int16_t *number)
{
    uint16_t u;
    if (!bscript_read_u16(bstream, &u)) {
        return false;
    }
    if (u & 1) {
        *number = (int16_t)(~(u >> 1));
    } else {
        *number = (int16_t)(u >> 1);
    }
    return true;
}

static inline bool bscript_write_s32(struct bstream *bstream, int32_t number)
{
    if (number < 0) {
        return bscript_write_u32(bstream, ((~number) << 1) | 1);
    } else {
        return bscript_write_u32(bstream, number << 1);
    }
}
static inline bool bscript_read_s32(struct bstream *bstream, int32_t *number)
{
    uint32_t u;
    if (!bscript_read_u32(bstream, &u)) {
        return false;
    }
    if (u & 1) {
        *number = ~(u >> 1);
    } else {
        *number = (u >> 1);
    }
    return true;
}

static inline bool bscript_write_s64(struct bstream *bstream, int64_t number)
{
    if (number < 0) {
        return bscript_write_u64(bstream, ((~number) << 1) | 1);
    } else {
        return bscript_write_u64(bstream, number << 1);
    }
}
static inline bool bscript_read_s64(struct bstream *bstream, int64_t *number)
{
    uint64_t u;
    if (!bscript_read_u64(bstream, &u)) {
        return false;
    }
    if (u & 1) {
        *number = ~(u >> 1);
    } else {
        *number = (u >> 1);
    }
    return true;
}

extern bool bscript_write_string(struct bstream *bstream, const uint8_t *s);
extern bool bscript_write_data(struct bstream *bstream, const void *data,
                               uint32_t size);

/*
 * *data_buffer should point to buffer allocated by one of the glib routines,
 *              or be NULL.
 * *data_buffer_size should contain the allocated size of *data_buffer,
 *              or zero if *data_buffer is NULL.
 * On return, *data_buffer may be reallocated using a glib routine.
 *            The allocation size will be found in *data_buffer_size.
 *
 * On success, NULL or a NULL-terminated string is returned in *string,
 *             and true is returned.
 * On failure, false is returned.
 */
extern bool bscript_read_string(struct bstream *bstream,
                                /* inout */ void **data_buffer,
                                /* inout */ uint32_t *data_buffer_size,
                                uint8_t **string);

/*
 * *data_buffer should point to buffer allocated by one of the glib routines,
 *              or be NULL.
 * *data_buffer_size should contain the allocated size of *data_buffer,
 *              or zero if *data_buffer is NULL.
 * On return, *data_buffer may be reallocated using a glib routine.
 *            The allocation size will be found in *data_buffer_size.
 *
 * On success, the data is returned in *data_buffer
 *             and the size of the data in *data_size;
 *             and true is returned.
 * On failure, false is returned.
 */
extern bool bscript_read_data(struct bstream *bstream,
                              /* inout */ void **data_buffer,
                              /* inout */ uint32_t *data_buffer_size,
                              uint32_t *data_size);

#endif
