#include "qemu/bstream.h"

#include <glib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAP_LENGTH 0x10000

static bool bstream_init_output(struct bstream *bstream)
{
    void *p;
    const char *buf = "";

    p = mmap(NULL, MAP_LENGTH, PROT_WRITE, MAP_SHARED, bstream->fd,
             bstream->commit_size);
    if (p == MAP_FAILED) {
        return false;
    }
    bstream->p = bstream->addr = (char *)p;
    bstream->end = bstream->addr + MAP_LENGTH;
    pwrite(bstream->fd, buf, 1, bstream->commit_size + MAP_LENGTH - 1);
    memset(bstream->p, 0, MAP_LENGTH);
    return true;
}

static bool bstream_init_input(struct bstream *bstream)
{
    void *p;

    p = mmap(NULL, MAP_LENGTH, PROT_READ, MAP_PRIVATE, bstream->fd,
             bstream->commit_size);
    if (p == MAP_FAILED) {
        return false;
    }
    bstream->p = bstream->addr = (char *)p;
    bstream->end = bstream->addr + MAP_LENGTH;
    return true;
}

static void bstream_commit_buffer(struct bstream *bstream)
{
    if (bstream->addr) {
        munmap(bstream->addr, MAP_LENGTH);
        bstream->commit_size += MAP_LENGTH;
        bstream->addr = NULL;
        bstream->end = NULL;
        bstream->p = NULL;
    }
}

static bool bstream_next_output_buffer(struct bstream *bstream)
{
    bstream_commit_buffer(bstream);
    return bstream_init_output(bstream);
}

static bool bstream_next_input_buffer(struct bstream *bstream)
{
    bstream_commit_buffer(bstream);
    return bstream_init_input(bstream);
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
    bstream->fd = open(file,
                       O_RDWR |
                       O_CREAT | O_EXCL |
                       O_CLOEXEC | O_LARGEFILE,
                       0666);
    if (bstream->fd == -1) {
        perror(msg);
        bstream_close(bstream);
        bstream = NULL;
    }
    return bstream;
}


struct bstream *bstream_init_for_input(const char *file, const char *msg)
{
    struct bstream *bstream = (struct bstream *)g_malloc0(sizeof(*bstream));
    bstream->fd = open(file,
                       O_RDONLY |
                       O_CLOEXEC | O_LARGEFILE,
                       0666);
    if (bstream->fd == -1) {
        perror(msg);
        bstream_close(bstream);
        bstream = NULL;
    }
    return bstream;
}

void bstream_close(struct bstream *bstream)
{
    bstream_commit_buffer(bstream);
    if (bstream->fd != -1) {
        close(bstream->fd);
    }
    g_free(bstream);
}

