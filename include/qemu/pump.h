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

#ifndef __QEMU_PUMP_H__
#define __QEMU_PUMP_H__

struct qemu_mapped_file {
    int fd;
    uint64_t pointer_position;
    void *pointer;
    uint64_t segment_size;
    uint64_t total;
};

ssize_t qemu_mapped_write(const void *ptr, size_t size, struct qemu_mapped_file *file);
ssize_t qemu_mapped_read(void *ptr, size_t size, struct qemu_mapped_file *file);
static inline void qemu_init_mapped_file(struct qemu_mapped_file *file, int fd)
{
    file->fd = fd;
}

struct qemu_pump {
    struct qemu_mapped_file mapped_input;
    struct qemu_mapped_file mapped_output;
    int in;
    int out;
    bool do_mmap_in;
    bool do_mmap_out;
    char buf[1024];
    ssize_t buffer_shift;
    ssize_t buffer_size;
};

static inline void qemu_init_pump(struct qemu_pump *pump,
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
    if (do_mmap_in) {
        qemu_init_mapped_file(&pump->mapped_input, in);
    }
    if (do_mmap_out) {
        qemu_init_mapped_file(&pump->mapped_output, out);
    }
}

void init_pump(struct qemu_pump *pump,
               int in,
               bool do_mmap_in,
               int out,
               bool do_mmap_out);
int pump_data(struct qemu_pump *pump);

#endif /* __QEMU_PUMP_H__ */
