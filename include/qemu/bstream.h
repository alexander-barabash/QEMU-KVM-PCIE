#ifndef __QEMU_BSTREAM_H__
#define __QEMU_BSTREAM_H__

#include "qemu/mapped_file.h"
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

struct bstream {
    QemuMappedFileData file_data;
    QemuMappedSegmentData segment_data;
    char *addr;
    char *end;
    char *p;
    uint64_t commit_size;
};

extern bool bstream_write_raw_data(struct bstream *bstream, const void *data, uint32_t size);
extern bool bstream_read_raw_data(struct bstream *bstream, void *data, uint32_t size);

extern struct bstream *bstream_init_for_output(const char *file, const char *msg);
extern struct bstream *bstream_init_for_input(const char *file, const char *msg);
extern void bstream_close(struct bstream *bstream);

#endif
