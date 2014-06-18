/*
 * Pump data between file descriptors.
 *
 * Copyright (C) 2014 Mentor Graphics Corp.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu-common.h"
#include "qemu/pump.h"
#include <sys/mman.h>

static int64_t get_file_size(int fd)
{
    return lseek64(fd, 0, SEEK_END);
}

static uint64_t align_position(uint64_t position)
{
    uint64_t seven = 7;
    return position & ~seven;
}

static ssize_t mapped_write(const void *ptr, size_t size, struct qemu_pump *pump)
{
    int64_t max_chunk_size = pump->out_pointer_position + pump->out_segment_size - pump->total_written;
    if (pump->out_pointer) {
        if (max_chunk_size <= 0) {
            munmap(pump->out_pointer, pump->out_segment_size);
            pump->out_pointer = NULL;
        }
    }
    if (!pump->out_pointer) {
        char zero = '\0';
        int64_t offset;
        pump->out_pointer_position = align_position(pump->total_written);
        pump->out_segment_size = 0x100000;
        offset = lseek64(pump->out,
                         pump->out_pointer_position + pump->out_segment_size - 1,
                         SEEK_SET);
        if (offset < 0) {
            return -1;
        }
        if (write(pump->out, &zero, 1) != 1) {
            return -1;
        }
        pump->out_pointer = mmap(0, pump->out_segment_size, PROT_WRITE,
                                 MAP_SHARED, pump->out, pump->out_pointer_position);
        if (pump->out_pointer == MAP_FAILED) {
            pump->out_pointer = NULL;
            return -1;
        }
        max_chunk_size = pump->out_pointer_position + pump->out_segment_size - pump->total_written;
    }
    if (max_chunk_size < 0) {
        return -1;
    }
    if (max_chunk_size < size) {
        size = (size_t)max_chunk_size;
    }
    memcpy(pump->out_pointer + pump->total_written - pump->out_pointer_position,
           ptr, size);
    return size;
}

static ssize_t mapped_read(void *ptr, size_t size, struct qemu_pump *pump)
{
    int64_t max_chunk_size = pump->in_pointer_position + pump->in_segment_size - pump->total_read;
    if (pump->in_pointer) {
        if (max_chunk_size <= 0) {
            munmap(pump->in_pointer, pump->in_segment_size);
            pump->in_pointer = NULL;
        }
    }
    if (!pump->in_pointer) {
        int64_t file_size = get_file_size(pump->in);
        uint64_t max_segment_size;
        if (file_size < 0) {
            return -1;
        }
        pump->in_pointer_position = align_position(pump->total_read);
        if (file_size <= pump->in_pointer_position) {
            return 0;
        }
        max_segment_size = file_size - pump->in_pointer_position;
        pump->in_segment_size = 0x100000;
        if (pump->in_segment_size > max_segment_size) {
            pump->in_segment_size = max_segment_size;
        }
        pump->in_pointer = mmap(0, pump->in_segment_size, PROT_READ,
                                MAP_SHARED, pump->in, pump->in_pointer_position);
        if (pump->in_pointer == MAP_FAILED) {
            pump->in_pointer = NULL;
            return -1;
        }
        max_chunk_size = pump->in_pointer_position + pump->in_segment_size - pump->total_read;
    }
    if (max_chunk_size < 0) {
        return -1;
    }
    if (max_chunk_size < size) {
        size = (size_t)max_chunk_size;
    }
    memcpy(ptr,
           pump->in_pointer + pump->total_read - pump->in_pointer_position,
           size);
    return size;
}

static inline void account_for_read_bytes(struct qemu_pump *pump, ssize_t read_bytes)
{
    if (read_bytes <= 0) {
        return;
    }
    pump->total_read += read_bytes;
}

static inline void account_for_written_bytes(struct qemu_pump *pump, ssize_t written_bytes)
{
    if (written_bytes <= 0) {
        return;
    }
    pump->total_written += written_bytes;
}

static inline void reset_pump_buffer(struct qemu_pump *pump)
{
    pump->buffer_shift = 0;
    pump->buffer_size = 0;
}

int pump_data(struct qemu_pump *pump)
{
    int in = pump->in;
    int out = pump->out;
    bool do_mmap_in = pump->do_mmap_in;
    bool do_mmap_out = pump->do_mmap_out;
    char *buf = pump->buf;
    int r;

    if ((in <= 0) || (out <= 0)) {
        return EINVAL;
    }

    while (true) {
        while (true) {
            ssize_t bytes_to_write = pump->buffer_size - pump->buffer_shift;
            ssize_t written_bytes;
            if (bytes_to_write == 0) {
                reset_pump_buffer(pump);
                break;
            }
            if (do_mmap_out) {
                written_bytes = mapped_write(buf + pump->buffer_shift, bytes_to_write, pump);
            } else {
                written_bytes = write(out, buf + pump->buffer_shift, bytes_to_write);
            }

            if (written_bytes == 0) {
                goto out;
            }
            if (written_bytes < 0) {
                if (errno == EINTR) {
                    continue;
                }
                r = errno;
                perror("Could not write stream");
                goto error;
            }
            account_for_written_bytes(pump, written_bytes);
            pump->buffer_shift += written_bytes;
        }
        while (true) {
            ssize_t read_bytes;

            if (do_mmap_in) {
                read_bytes = mapped_read(buf, 1024, pump);
            } else {
                read_bytes = read(in, buf, 1024);
            }

            if (read_bytes == 0) {
                goto out;
            }
            if (read_bytes < 0) {
                if (errno == EINTR) {
                    continue;
                }
                r = errno;
                perror("Could not read stream");
                goto error;
            }
            account_for_read_bytes(pump, read_bytes);
            pump->buffer_size = read_bytes;
            pump->buffer_shift = 0;
            break;
        }
    }

 out:
    return 0;

 error:
    close(out);
    close(in);
    pump->out = 0;
    pump->in = 0;
    return r;
}

void init_pump(struct qemu_pump *pump,
               int in,
               bool do_mmap_in,
               int out,
               bool do_mmap_out)
{
    memset(pump, 0, sizeof(*pump));
    pump->in = in;
    pump->out = out;
    pump->do_mmap_in = do_mmap_in;
    pump->do_mmap_out = do_mmap_out;
}
