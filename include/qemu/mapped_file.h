/*
 * Utilities for memory mapped files.
 *
 * Copyright (C) 2015 Mentor Graphics Corp.
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

#ifndef QEMU_MAPPED_FILE_H
#define QEMU_MAPPED_FILE_H

#include "qemu/osdep.h"

typedef struct QemuMappedSegmentData {
    uint64_t length;
    uint64_t offset;
    void *pointer;
} QemuMappedSegmentData;

typedef struct QemuMappedFileData {
    char *filename;
    bool readonly;
    bool writeonly;
    QemuMappedFileHandleType handle;
} QemuMappedFileData;

void qemu_init_mapped_file_data(QemuMappedFileData *data, const char *filename);
bool qemu_open_mapped_file(QemuMappedFileData *data);
void qemu_close_mapped_file(QemuMappedFileData *data);

void qemu_init_mapped_segment_data(QemuMappedSegmentData *data);
bool qemu_map_segment_data(QemuMappedFileData *file_data,
                           QemuMappedSegmentData *data);
void qemu_unmap_segment_data(QemuMappedSegmentData *data);

#endif /* QEMU_MAPPED_FILE_H */
