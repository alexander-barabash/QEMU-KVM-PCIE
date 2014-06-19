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
                written_bytes = qemu_mapped_write(buf + pump->buffer_shift, bytes_to_write, &pump->mapped_output);
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
            pump->buffer_shift += written_bytes;
        }
        while (true) {
            ssize_t read_bytes;

            if (do_mmap_in) {
                read_bytes = qemu_mapped_read(buf, 1024, &pump->mapped_input);
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

static ssize_t do_qemu_mapped_write(const void *ptr, size_t size,
                                    struct qemu_mapped_file *file)
{
    int64_t max_chunk_size = file->pointer_position + file->segment_size - file->total;
    if (file->pointer) {
        if (max_chunk_size <= 0) {
            munmap(file->pointer, file->segment_size);
            file->pointer = NULL;
        }
    }
    if (!file->pointer) {
        char zero = '\0';
        int64_t offset;
        file->pointer_position = align_position(file->total);
        file->segment_size = 0x100000;
        offset = lseek64(file->fd,
                         file->pointer_position + file->segment_size - 1,
                         SEEK_SET);
        if (offset < 0) {
            perror("Seek failed");
            fprintf(stderr, "Could not seek to position %lld\n",
                    (long long)file->pointer_position + file->segment_size - 1);
            return -1;
        }
        if (write(file->fd, &zero, 1) != 1) {
            return -1;
        }
        file->pointer = mmap(0, file->segment_size, PROT_WRITE,
                                 MAP_SHARED, file->fd, file->pointer_position);
        if (file->pointer == MAP_FAILED) {
            file->pointer = NULL;
            return -1;
        }
        max_chunk_size = file->pointer_position + file->segment_size - file->total;
    }
    if (max_chunk_size < 0) {
        return -1;
    }
    if (max_chunk_size < size) {
        size = (size_t)max_chunk_size;
    }
    memcpy(file->pointer + file->total - file->pointer_position,
           ptr, size);
    file->total += size;
    return size;
}

ssize_t qemu_mapped_write(const void *ptr, size_t size,
                          struct qemu_mapped_file *file)
{
    size_t copied = 0;
    do {
        ssize_t result = do_qemu_mapped_write(ptr, size, file);
        if (result < 0) {
            if (copied > 0) {
                return copied;
            } else {
                return result;
            }
        }
        if (result == size) {
            return copied + size;
        }
        size -= result;
        copied += result;
        ptr += result;
    } while (true);
}

static ssize_t do_qemu_mapped_read(void *ptr, size_t size,
                                   struct qemu_mapped_file *file)
{
    int64_t max_chunk_size = file->pointer_position + file->segment_size - file->total;
    if (file->pointer) {
        if (max_chunk_size <= 0) {
            munmap(file->pointer, file->segment_size);
            file->pointer = NULL;
        }
    }
    if (!file->pointer) {
        int64_t file_size = get_file_size(file->fd);
        uint64_t max_segment_size;
        if (file_size < 0) {
            return -1;
        }
        file->pointer_position = align_position(file->total);
        if (file_size <= file->pointer_position) {
            return 0;
        }
        max_segment_size = file_size - file->pointer_position;
        file->segment_size = 0x100000;
        if (file->segment_size > max_segment_size) {
            file->segment_size = max_segment_size;
        }
        file->pointer = mmap(0, file->segment_size, PROT_READ,
                                MAP_SHARED, file->fd, file->pointer_position);
        if (file->pointer == MAP_FAILED) {
            file->pointer = NULL;
            return -1;
        }
        max_chunk_size = file->pointer_position + file->segment_size - file->total;
    }
    if (max_chunk_size < 0) {
        return -1;
    }
    if (max_chunk_size < size) {
        size = (size_t)max_chunk_size;
    }
    memcpy(ptr,
           file->pointer + file->total - file->pointer_position,
           size);
    file->total += size;
    return size;
}

ssize_t qemu_mapped_read(void *ptr, size_t size, struct qemu_mapped_file *file)
{
    size_t copied = 0;
    do {
        ssize_t result = do_qemu_mapped_read(ptr, size, file);
        if (result < 0) {
            if (copied > 0) {
                return copied;
            } else {
                return result;
            }
        }
        if (result == size) {
            return copied + size;
        }
        size -= result;
        copied += result;
        ptr += result;
    } while (true);
}
