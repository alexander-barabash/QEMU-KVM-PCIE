#include "qemu/bstream.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAP_LENGTH 0x10000

static bool bstream_init_segment(struct bstream *bstream)
{
    bstream->segment_data.offset = bstream->commit_size;
    bstream->segment_data.length = MAP_LENGTH;
    if (!qemu_map_segment_data(&bstream->file_data,
                               &bstream->segment_data)) {
        return false;
    }
    bstream->p = bstream->addr = (char *)bstream->segment_data.pointer;
    bstream->end = bstream->addr + MAP_LENGTH;
    return true;
}

static void bstream_commit_buffer(struct bstream *bstream)
{
    if (bstream->addr) {
        qemu_unmap_segment_data(&bstream->segment_data);
        bstream->commit_size += MAP_LENGTH;
        bstream->addr = NULL;
        bstream->end = NULL;
        bstream->p = NULL;
    }
}

static bool bstream_next_output_buffer(struct bstream *bstream)
{
    bstream_commit_buffer(bstream);
    return bstream_init_segment(bstream);
}

static bool bstream_next_input_buffer(struct bstream *bstream)
{
    bstream_commit_buffer(bstream);
    return bstream_init_segment(bstream);
}

bool bstream_write_raw_data(struct bstream *bstream, const void *data, uint32_t size)
{
    while (size > 0) {
        uint32_t rem_size = bstream->end - bstream->p;
        if (rem_size > 0) {
            if (size < rem_size) {
                rem_size = size;
            }
            memcpy(bstream->p, data, rem_size);
            bstream->p += rem_size;
            data += rem_size;
            size -= rem_size;
        } else if (!bstream_next_output_buffer(bstream)) {
            return false;
        }
    }
    return true;
}

bool bstream_read_raw_data(struct bstream *bstream, void *data, uint32_t size)
{
    while (size > 0) {
        uint32_t rem_size = bstream->end - bstream->p;
        if (rem_size > 0) {
            if (size < rem_size) {
                rem_size = size;
            }
            memcpy(data, bstream->p, rem_size);
            bstream->p += rem_size;
            data += rem_size;
            size -= rem_size;
        } else if (!bstream_next_input_buffer(bstream)) {
            return false;
        }
    }
    return true;
}

struct bstream *bstream_init_for_output(const char *file, const char *msg)
{
    struct bstream *bstream = (struct bstream *)g_malloc0(sizeof(*bstream));
    qemu_init_mapped_file_data(&bstream->file_data, file);
    qemu_init_mapped_segment_data(&bstream->segment_data);
    bstream->file_data.writeonly = true;    
    if (!qemu_open_mapped_file(&bstream->file_data)) {
        perror(msg);
        bstream_close(bstream);
        bstream = NULL;
    }
    return bstream;
}


struct bstream *bstream_init_for_input(const char *file, const char *msg)
{
    struct bstream *bstream = (struct bstream *)g_malloc0(sizeof(*bstream));
    qemu_init_mapped_file_data(&bstream->file_data, file);
    qemu_init_mapped_segment_data(&bstream->segment_data);
    bstream->file_data.readonly = true;
    if (!qemu_open_mapped_file(&bstream->file_data)) {
        perror(msg);
        bstream_close(bstream);
        bstream = NULL;
    }
    return bstream;
}

void bstream_close(struct bstream *bstream)
{
    bstream_commit_buffer(bstream);
    qemu_unmap_segment_data(&bstream->segment_data);
    qemu_close_mapped_file(&bstream->file_data);
    g_free(bstream);
}

