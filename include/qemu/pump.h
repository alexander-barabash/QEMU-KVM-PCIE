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

struct qemu_pump {
    int in;
    int out;
    bool do_mmap_in;
    bool do_mmap_out;
    uint64_t in_pointer_position;
    void *in_pointer;
    uint64_t in_segment_size;
    uint64_t out_pointer_position;
    void *out_pointer;
    uint64_t out_segment_size;
    char buf[1024];
    ssize_t buffer_shift;
    ssize_t buffer_size;
    uint64_t total_read;
    uint64_t total_written;
};

void init_pump(struct qemu_pump *pump,
               int in,
               bool do_mmap_in,
               int out,
               bool do_mmap_out);
int pump_data(struct qemu_pump *pump);

#endif /* __QEMU_PUMP_H__ */
