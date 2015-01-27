#include "qemu/bscript.h"
#include <glib.h>
#include <string.h>

static uint64_t HIGHEST_BYTE_MASK_FOR_U64 = ((uint64_t)0xFFU << (64 - 8));
static uint8_t NULL_MARKER = 0x80U;
static uint8_t SEVENBYTE_MASK = 0x7FU;
static uint8_t BYTE_MASK = 0xFFU;
static uint8_t HIGH_BIT_MASK = 0x80U;
static uint8_t SIXBYTE_MASK = 0x3FU;
static uint8_t FIRST_6BYTE_BIT_MASK = 0x80U;
static uint8_t LAST_6BYTE_BIT_MASK = 0x40U;

static inline uint64_t HIGHEST_BYTE_FOR_U64(uint64_t number)
{
    return number & HIGHEST_BYTE_MASK_FOR_U64;
}
static inline bool HAS_HIGHEST_BYTE_FOR_U64(uint64_t number)
{
    return HIGHEST_BYTE_FOR_U64(number) != 0;
}

static inline uint8_t LEAST_SIGNIFICANT_BYTE_AS_CHAR(uint64_t number)
{
    return (uint8_t)(number & BYTE_MASK);
}
static inline uint64_t WITHOUT_LEAST_SIGNIFICANT_BYTE(uint64_t number)
{
    return number >> 8;
}
static inline uint64_t WITHOUT_LEAST_SIGNIFICANT_BIT(uint64_t number)
{
    return number >> 1;
}

/* N must be between 0 and 10 */
static inline uint8_t SIXBYTE(int N, uint64_t number)
{
    return (uint8_t)((number >> (N * 6)) & SIXBYTE_MASK);
}
/* N must be between 0 and 8 */
static inline uint8_t SEVENBYTE(int N, uint64_t number)
{
    return (uint8_t)((number >> (N * 7)) & SEVENBYTE_MASK);
}

static inline uint8_t HIGH_7BYTE(uint8_t byte)
{
    return (uint8_t)(byte | HIGH_BIT_MASK);
}
static inline uint8_t LOW_7BYTE(uint8_t byte)
{
    return (uint8_t)(byte & ~HIGH_BIT_MASK);
}
static inline bool IS_HIGH_7BYTE(uint8_t byte)
{
    return ((byte & HIGH_BIT_MASK) != 0);
}

static inline uint8_t FIRST_6BYTE(uint8_t byte)
{
    return (uint8_t)(byte | FIRST_6BYTE_BIT_MASK);
}
static inline uint8_t LAST_6BYTE(uint8_t byte)
{
    return (uint8_t)(byte | LAST_6BYTE_BIT_MASK);
}
static inline uint8_t ONLY_6BYTE(uint8_t byte)
{
    return (uint8_t)(byte | (LAST_6BYTE_BIT_MASK | FIRST_6BYTE_BIT_MASK));
}
static inline uint8_t PURE_6BYTE(uint8_t byte)
{
    return (uint8_t)(byte & SIXBYTE_MASK);
}

static inline bool IS_FIRST_6BYTE(uint8_t byte)
{
    return (byte & FIRST_6BYTE_BIT_MASK) != 0;
}
static inline bool IS_LAST_6BYTE(uint8_t byte)
{
    return (byte & LAST_6BYTE_BIT_MASK) != 0;
}

bool bscript_write_u16(struct bstream *bstream, uint16_t us)
{
    uint8_t bytes[] = {
        SEVENBYTE(0, us),
        SEVENBYTE(1, us),
        SEVENBYTE(2, us),
    };
    uint8_t buffer[sizeof(bytes)];
    uint32_t index = 0;
    if (bytes[2]) {
        buffer[index++] = HIGH_7BYTE(bytes[2]);
        buffer[index++] = HIGH_7BYTE(bytes[1]);
    } else if (bytes[1]) {
        buffer[index++] = HIGH_7BYTE(bytes[1]);
    }
    buffer[index++] = bytes[0];
    return bstream_write_raw_data(bstream, buffer, index);
}

bool bscript_read_u16(struct bstream *bstream, uint16_t *us)
{
    uint8_t byte;
    uint32_t result;
    
    // 0
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    if (!IS_HIGH_7BYTE(byte)) {
        *us = (uint16_t)byte;
        return true;
    }
    result = LOW_7BYTE(byte);
    result <<= 7;
    
    // 1
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    if (!IS_HIGH_7BYTE(byte)) {
        *us = (uint16_t)(result | byte);
        return true;
    }
    result |= LOW_7BYTE(byte);
    result <<= 7;
    
    // 2
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    *us = (uint16_t)(result | byte);
    return true;
}

bool bscript_write_u32(struct bstream *bstream, uint32_t u)
{
    uint8_t bytes[] = {
        SEVENBYTE(0, u),
        SEVENBYTE(1, u),
        SEVENBYTE(2, u),
        SEVENBYTE(3, u),
        SEVENBYTE(4, u)
    };
    uint8_t buffer[sizeof(bytes)];
    uint32_t index = 0;
    if (bytes[4]) {
        buffer[index++] = HIGH_7BYTE(bytes[4]);
        buffer[index++] = HIGH_7BYTE(bytes[3]);
        buffer[index++] = HIGH_7BYTE(bytes[2]);
        buffer[index++] = HIGH_7BYTE(bytes[1]);
    } else if (bytes[3]) {
        buffer[index++] = HIGH_7BYTE(bytes[3]);
        buffer[index++] = HIGH_7BYTE(bytes[2]);
        buffer[index++] = HIGH_7BYTE(bytes[1]);
    } else if (bytes[2]) {
        buffer[index++] = HIGH_7BYTE(bytes[2]);
        buffer[index++] = HIGH_7BYTE(bytes[1]);
    } else if (bytes[1]) {
        buffer[index++] = HIGH_7BYTE(bytes[1]);
    }
    buffer[index++] = bytes[0];
    return bstream_write_raw_data(bstream, buffer, index);
}

static bool bscript_read_u32_or_null_marker(struct bstream *bstream,
                                            uint32_t *u,
                                            bool *is_null_marker)
{
    uint8_t byte;
    uint32_t result;

    *is_null_marker = false;

    // 0
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    if (!IS_HIGH_7BYTE(byte)) {
        *u = (uint32_t)byte;
        return true;
    }
    result = LOW_7BYTE(byte);
    if (!result) {
        *is_null_marker = true;
        return true;
    }
    result <<= 7;

    // 1
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    if (!IS_HIGH_7BYTE(byte)) {
        *u = result | byte;
        return true;
    }
    result |= LOW_7BYTE(byte);
    result <<= 7;
    
    // 2
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    if (!IS_HIGH_7BYTE(byte)) {
        *u = result | byte;
        return true;
    }
    result |= LOW_7BYTE(byte);
    result <<= 7;

    // 3
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    if (!IS_HIGH_7BYTE(byte)) {
        *u = result | byte;
        return true;
    }
    result |= LOW_7BYTE(byte);
    result <<= 7;

    // 4
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    *u = result | byte;
    return true;
}

bool bscript_read_u32(struct bstream *bstream, uint32_t *u)
{
    bool is_null_marker;
    return
        bscript_read_u32_or_null_marker(bstream, u, &is_null_marker) &&
        !is_null_marker;
}

bool bscript_write_u64(struct bstream *bstream, uint64_t number)
{
    bool is_super_long_long;
    uint8_t tail;
    uint8_t bytes[9];
    uint8_t buffer[sizeof(bytes)];
    uint32_t index = 0;
    if (HAS_HIGHEST_BYTE_FOR_U64(number)) {
        /*
         * If number does not fit into the 8 bytes of standard encoding
         * (i.e. its most significant byte is non-zero),
         * just chop off the least significant bit,
         * and write the least significant byte afterwards.
         */
        is_super_long_long = true;
        tail = LEAST_SIGNIFICANT_BYTE_AS_CHAR(number);
        number = WITHOUT_LEAST_SIGNIFICANT_BIT(number);
    } else {
        is_super_long_long = false;
        tail = 0;
    }
    bytes[0] = SEVENBYTE(0, number);
    bytes[1] = SEVENBYTE(1, number);
    bytes[2] = SEVENBYTE(2, number);
    bytes[3] = SEVENBYTE(3, number);
    bytes[4] = SEVENBYTE(4, number);
    bytes[5] = SEVENBYTE(5, number);
    bytes[6] = SEVENBYTE(6, number);
    bytes[7] = SEVENBYTE(7, number);
    bytes[8] = SEVENBYTE(8, number);
    if (is_super_long_long) {
        buffer[index++] = HIGH_7BYTE(bytes[8]);
        buffer[index++] = HIGH_7BYTE(bytes[7]);
        buffer[index++] = HIGH_7BYTE(bytes[6]);
        buffer[index++] = HIGH_7BYTE(bytes[5]);
        buffer[index++] = HIGH_7BYTE(bytes[4]);
        buffer[index++] = HIGH_7BYTE(bytes[3]);
        buffer[index++] = HIGH_7BYTE(bytes[2]);
        buffer[index++] = HIGH_7BYTE(bytes[1]);
        buffer[index++] = tail; /* This has 8 significant bits */
    } else if (bytes[7]) {
        buffer[index++] = HIGH_7BYTE(bytes[7]);
        buffer[index++] = HIGH_7BYTE(bytes[6]);
        buffer[index++] = HIGH_7BYTE(bytes[5]);
        buffer[index++] = HIGH_7BYTE(bytes[4]);
        buffer[index++] = HIGH_7BYTE(bytes[3]);
        buffer[index++] = HIGH_7BYTE(bytes[2]);
        buffer[index++] = HIGH_7BYTE(bytes[1]);
        buffer[index++] = bytes[0];
    } else if (bytes[6]) {
        buffer[index++] = HIGH_7BYTE(bytes[6]);
        buffer[index++] = HIGH_7BYTE(bytes[5]);
        buffer[index++] = HIGH_7BYTE(bytes[4]);
        buffer[index++] = HIGH_7BYTE(bytes[3]);
        buffer[index++] = HIGH_7BYTE(bytes[2]);
        buffer[index++] = HIGH_7BYTE(bytes[1]);
        buffer[index++] = bytes[0];
    } else if (bytes[5]) {
        buffer[index++] = HIGH_7BYTE(bytes[5]);
        buffer[index++] = HIGH_7BYTE(bytes[4]);
        buffer[index++] = HIGH_7BYTE(bytes[3]);
        buffer[index++] = HIGH_7BYTE(bytes[2]);
        buffer[index++] = HIGH_7BYTE(bytes[1]);
        buffer[index++] = bytes[0];
    } else if (bytes[4]) {
        buffer[index++] = HIGH_7BYTE(bytes[4]);
        buffer[index++] = HIGH_7BYTE(bytes[3]);
        buffer[index++] = HIGH_7BYTE(bytes[2]);
        buffer[index++] = HIGH_7BYTE(bytes[1]);
        buffer[index++] = bytes[0];
    } else if (bytes[3]) {
        buffer[index++] = HIGH_7BYTE(bytes[3]);
        buffer[index++] = HIGH_7BYTE(bytes[2]);
        buffer[index++] = HIGH_7BYTE(bytes[1]);
        buffer[index++] = bytes[0];
    } else if (bytes[2]) {
        buffer[index++] = HIGH_7BYTE(bytes[2]);
        buffer[index++] = HIGH_7BYTE(bytes[1]);
        buffer[index++] = bytes[0];
    } else if (bytes[1]) {
        buffer[index++] = HIGH_7BYTE(bytes[1]);
        buffer[index++] = bytes[0];
    } else {
        buffer[index++] = bytes[0];
    }
    return bstream_write_raw_data(bstream, buffer, index);
}

bool bscript_read_u64(struct bstream *bstream, uint64_t *number)
{
    uint8_t byte;
    uint64_t result;

    // 0
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    if (!IS_HIGH_7BYTE(byte)) {
        *number = (uint64_t)byte;
        return true;
    }
    result = LOW_7BYTE(byte);
    result <<= 7;

    // 1
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    if (!IS_HIGH_7BYTE(byte)) {
        *number = result | byte;
        return true;
    }
    result |= LOW_7BYTE(byte);
    result <<= 7;

    // 2
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    if (!IS_HIGH_7BYTE(byte)) {
        *number = result | byte;
        return true;
    }
    result |= LOW_7BYTE(byte);
    result <<= 7;

    // 3
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    if (!IS_HIGH_7BYTE(byte)) {
        *number = result | byte;
        return true;
    }
    result |= LOW_7BYTE(byte);
    result <<= 7;

    // 4
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    if (!IS_HIGH_7BYTE(byte)) {
        *number = result | byte;
        return true;
    }
    result |= LOW_7BYTE(byte);
    result <<= 7;

    // 5
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    if (!IS_HIGH_7BYTE(byte)) {
        *number = result | byte;
        return true;
    }
    result |= LOW_7BYTE(byte);
    result <<= 7;

    // 6
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    if (!IS_HIGH_7BYTE(byte)) {
        *number = result | byte;
        return true;
    }
    result |= LOW_7BYTE(byte);
    result <<= 7;

    // 7
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    if (!IS_HIGH_7BYTE(byte)) {
        *number = result | byte;
        return true;
    }
    result |= LOW_7BYTE(byte);
    result <<= 8; // NB

    // 8
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    *number = result | byte;
    return true;
}

bool bscript_write_symmetric_u32(struct bstream *bstream, uint32_t u)
{
    uint8_t bytes[] = {
        SIXBYTE(0, u),
        SIXBYTE(1, u),
        SIXBYTE(2, u),
        SIXBYTE(3, u),
        SIXBYTE(4, u),
        SIXBYTE(5, u),
    };
    uint8_t buffer[sizeof(bytes)];
    uint32_t index = 0;
    if (bytes[5]) {
        buffer[index++] = FIRST_6BYTE(bytes[5]);
        buffer[index++] = bytes[4];
        buffer[index++] = bytes[3];
        buffer[index++] = bytes[2];
        buffer[index++] = bytes[1];
        buffer[index++] = LAST_6BYTE(bytes[0]);
    } else if (bytes[4]) {
        buffer[index++] = FIRST_6BYTE(bytes[4]);
        buffer[index++] = bytes[3];
        buffer[index++] = bytes[2];
        buffer[index++] = bytes[1];
        buffer[index++] = LAST_6BYTE(bytes[0]);
    } else if (bytes[3]) {
        buffer[index++] = FIRST_6BYTE(bytes[3]);
        buffer[index++] = bytes[2];
        buffer[index++] = bytes[1];
        buffer[index++] = LAST_6BYTE(bytes[0]);
    } else if (bytes[2]) {
        buffer[index++] = FIRST_6BYTE(bytes[2]);
        buffer[index++] = bytes[1];
        buffer[index++] = LAST_6BYTE(bytes[0]);
    } else if (bytes[1]) {
        buffer[index++] = FIRST_6BYTE(bytes[1]);
        buffer[index++] = LAST_6BYTE(bytes[0]);
    } else {
        buffer[index++] = ONLY_6BYTE(bytes[0]);
    }
    return bstream_write_raw_data(bstream, buffer, index);
}

bool bscript_read_symmetric_u32(struct bstream *bstream, uint32_t *u)
{
    uint8_t byte;
    uint32_t result;

    // 0
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    result = PURE_6BYTE(byte);
    if (IS_LAST_6BYTE(byte)) {
        *u = result;
        return true;
    }
    result <<= 6;

    // 1
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    result |= PURE_6BYTE(byte);
    if (IS_LAST_6BYTE(byte)) {
        *u = result;
        return true;
    }
    result <<= 6;

    // 2
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    result |= PURE_6BYTE(byte);
    if (IS_LAST_6BYTE(byte)) {
        *u = result;
        return true;
    }
    result <<= 6;
  
    // 3
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    result |= PURE_6BYTE(byte);
    if (IS_LAST_6BYTE(byte)) {
        *u = result;
        return true;
    }
    result <<= 6;
  
    // 4
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    result |= PURE_6BYTE(byte);
    if (IS_LAST_6BYTE(byte)) {
        *u = result;
        return true;
    }
    result <<= 6;
  
    // 5
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    *u = result | PURE_6BYTE(byte);
    return true;
}

bool bscript_write_symmetric_u64(struct bstream *bstream, uint64_t ull)
{
    uint8_t bytes[] = {
        SIXBYTE(0, ull),
        SIXBYTE(1, ull),
        SIXBYTE(2, ull),
        SIXBYTE(3, ull),
        SIXBYTE(4, ull),
        SIXBYTE(5, ull),
        SIXBYTE(6, ull),
        SIXBYTE(7, ull),
        SIXBYTE(8, ull),
        SIXBYTE(9, ull),
        SIXBYTE(10, ull),
    };
    uint8_t buffer[sizeof(bytes)];
    uint32_t index = 0;
    if (bytes[10]) {
        buffer[index++] = FIRST_6BYTE(bytes[10]);
        buffer[index++] = bytes[9];
        buffer[index++] = bytes[8];
        buffer[index++] = bytes[7];
        buffer[index++] = bytes[6];
        buffer[index++] = bytes[5];
        buffer[index++] = bytes[4];
        buffer[index++] = bytes[3];
        buffer[index++] = bytes[2];
        buffer[index++] = bytes[1];
        buffer[index++] = LAST_6BYTE(bytes[0]);
    } else if (bytes[9]) {
        buffer[index++] = FIRST_6BYTE(bytes[9]);
        buffer[index++] = bytes[8];
        buffer[index++] = bytes[7];
        buffer[index++] = bytes[6];
        buffer[index++] = bytes[5];
        buffer[index++] = bytes[4];
        buffer[index++] = bytes[3];
        buffer[index++] = bytes[2];
        buffer[index++] = bytes[1];
        buffer[index++] = LAST_6BYTE(bytes[0]);
    } else if (bytes[8]) {
        buffer[index++] = FIRST_6BYTE(bytes[8]);
        buffer[index++] = bytes[7];
        buffer[index++] = bytes[6];
        buffer[index++] = bytes[5];
        buffer[index++] = bytes[4];
        buffer[index++] = bytes[3];
        buffer[index++] = bytes[2];
        buffer[index++] = bytes[1];
        buffer[index++] = LAST_6BYTE(bytes[0]);
    } else if (bytes[7]) {
        buffer[index++] = FIRST_6BYTE(bytes[7]);
        buffer[index++] = bytes[6];
        buffer[index++] = bytes[5];
        buffer[index++] = bytes[4];
        buffer[index++] = bytes[3];
        buffer[index++] = bytes[2];
        buffer[index++] = bytes[1];
        buffer[index++] = LAST_6BYTE(bytes[0]);
    } else if (bytes[6]) {
        buffer[index++] = FIRST_6BYTE(bytes[6]);
        buffer[index++] = bytes[5];
        buffer[index++] = bytes[4];
        buffer[index++] = bytes[3];
        buffer[index++] = bytes[2];
        buffer[index++] = bytes[1];
        buffer[index++] = LAST_6BYTE(bytes[0]);
    } else if (bytes[5]) {
        buffer[index++] = FIRST_6BYTE(bytes[5]);
        buffer[index++] = bytes[4];
        buffer[index++] = bytes[3];
        buffer[index++] = bytes[2];
        buffer[index++] = bytes[1];
        buffer[index++] = LAST_6BYTE(bytes[0]);
    } else if (bytes[4]) {
        buffer[index++] = FIRST_6BYTE(bytes[4]);
        buffer[index++] = bytes[3];
        buffer[index++] = bytes[2];
        buffer[index++] = bytes[1];
        buffer[index++] = LAST_6BYTE(bytes[0]);
    } else if (bytes[3]) {
        buffer[index++] = FIRST_6BYTE(bytes[3]);
        buffer[index++] = bytes[2];
        buffer[index++] = bytes[1];
        buffer[index++] = LAST_6BYTE(bytes[0]);
    } else if (bytes[2]) {
        buffer[index++] = FIRST_6BYTE(bytes[2]);
        buffer[index++] = bytes[1];
        buffer[index++] = LAST_6BYTE(bytes[0]);
    } else if (bytes[1]) {
        buffer[index++] = FIRST_6BYTE(bytes[1]);
        buffer[index++] = LAST_6BYTE(bytes[0]);
    } else {
        buffer[index++] = ONLY_6BYTE(bytes[0]);
    }
    return bstream_write_raw_data(bstream, buffer, index);
}

bool bscript_read_symmetric_u64(struct bstream *bstream, uint64_t *ull)
{
    uint8_t byte;
    uint64_t result;

    // 0
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    result = PURE_6BYTE(byte);
    if (IS_LAST_6BYTE(byte)) {
        *ull = result;
        return true;
    }
    result <<= 6;

    // 1
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    result |= PURE_6BYTE(byte);
    if (IS_LAST_6BYTE(byte)) {
        *ull = result;
        return true;
    }
    result <<= 6;

    // 2
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    result |= PURE_6BYTE(byte);
    if (IS_LAST_6BYTE(byte)) {
        *ull = result;
        return true;
    }
    result <<= 6;

    // 3
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    result |= PURE_6BYTE(byte);
    if (IS_LAST_6BYTE(byte)) {
        *ull = result;
        return true;
    }
    result <<= 6;

    // 4
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    result |= PURE_6BYTE(byte);
    if (IS_LAST_6BYTE(byte)) {
        *ull = result;
        return true;
    }
    result <<= 6;

    // 5
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    result |= PURE_6BYTE(byte);
    if (IS_LAST_6BYTE(byte)) {
        *ull = result;
        return true;
    }
    result <<= 6;

    // 6
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    result |= PURE_6BYTE(byte);
    if (IS_LAST_6BYTE(byte)) {
        *ull = result;
        return true;
    }
    result <<= 6;

    // 7
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    result |= PURE_6BYTE(byte);
    if (IS_LAST_6BYTE(byte)) {
        *ull = result;
        return true;
    }
    result <<= 6;

    // 8
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    result |= PURE_6BYTE(byte);
    if (IS_LAST_6BYTE(byte)) {
        *ull = result;
        return true;
    }
    result <<= 6;

    // 9
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    result |= PURE_6BYTE(byte);
    if (IS_LAST_6BYTE(byte)) {
        *ull = result;
        return true;
    }
    result <<= 6;

    // 10
    if (!bscript_read_u8(bstream, &byte)) {
        return false;
    }
    *ull = result | PURE_6BYTE(byte);
    return true;
}

bool bscript_write_string(struct bstream *bstream, const char *s)
{
    uint32_t size;

    if (!s) {
        return bscript_write_u8(bstream, NULL_MARKER);
    }

    size = strlen((const char *)s);
    return
        bscript_write_u32(bstream, size) &&
        (!size ||
         bstream_write_raw_data(bstream, s, size));
}

bool bscript_write_data(struct bstream *bstream, const void *data, uint32_t size)
{
    if (!data || !size) {
        return bscript_write_u8(bstream, NULL_MARKER);
    }
    
    return
        bscript_write_u32(bstream, size) &&
        bstream_write_raw_data(bstream, data, size);
}

bool bscript_read_string(struct bstream *bstream,
                         /* inout */ void **data_buffer,
                         /* inout */ uint32_t *data_buffer_size,
                         char **string)
{
    uint32_t string_size;
    bool is_null_marker;

    if (!bscript_read_u32_or_null_marker(bstream, &string_size,
                                         &is_null_marker)) {
        return false;
    }
    if (is_null_marker) {
        *string = NULL;
        return true;
    }
    if (string_size + 1 > *data_buffer_size) {
        uint32_t new_buffer_size;
        void *new_data_buffer;
        new_buffer_size = 2 * *data_buffer_size;
        if (string_size + 1 > new_buffer_size) {
            new_buffer_size = string_size + 1;
        }
        new_data_buffer = g_try_malloc(new_buffer_size);
        if (!new_data_buffer) {
            return false;
        }
        g_free(*data_buffer);
        *data_buffer = new_data_buffer;
        *data_buffer_size = new_buffer_size;
    }
    if (string_size > 0) {
        if (!bstream_read_raw_data(bstream, *data_buffer, string_size)) {
            return false;
        }
    }
    *string = (char *)*data_buffer;
    (*string)[string_size] = 0;
    return true;
}

bool bscript_read_data(struct bstream *bstream,
                       /* inout */ void **data_buffer,
                       /* inout */ uint32_t *data_buffer_size,
                       uint32_t *data_size)
{
    bool is_null_marker;

    if (!bscript_read_u32_or_null_marker(bstream, data_size,
                                         &is_null_marker)) {
        return false;
    }
    if (is_null_marker) {
        *data_size = 0;
        return true;
    }
    if (*data_size > *data_buffer_size) {
        uint32_t new_buffer_size;
        void *new_data_buffer;
        new_buffer_size = 2 * *data_buffer_size;
        if (*data_size > new_buffer_size) {
            new_buffer_size = *data_size;
        }
        new_data_buffer = g_try_malloc(new_buffer_size);
        if (!new_data_buffer) {
            return false;
        }
        g_free(*data_buffer);
        *data_buffer = new_data_buffer;
        *data_buffer_size = new_buffer_size;
    }
    return bstream_read_raw_data(bstream, *data_buffer, *data_size);
}
