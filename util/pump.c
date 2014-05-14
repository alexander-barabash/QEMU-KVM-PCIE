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

static inline void account_for_read_bytes(struct qemu_pump *pump, ssize_t read_bytes)
{
    if (read_bytes <= 0) {
        return;
    }
    pump->total_read += read_bytes;
    if (((pump->total_read - read_bytes) / 100000) != (pump->total_read / 100000)) {
        fprintf(stderr, "Read %lld KBytes\n", (long long)((pump->total_read / 100000) * 100));
    }
}

static inline void account_for_written_bytes(struct qemu_pump *pump, ssize_t written_bytes)
{
    if (written_bytes <= 0) {
        return;
    }
    pump->total_written += written_bytes;
    if (((pump->total_written - written_bytes) / 100000) != (pump->total_written / 100000)) {
        fprintf(stderr, "Written %lld KBytes\n", (long long)((pump->total_written / 100000) * 100));
    }
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
            written_bytes = write(out, buf + pump->buffer_shift, bytes_to_write);
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
            ssize_t read_bytes = read(in, buf, 1024);
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
