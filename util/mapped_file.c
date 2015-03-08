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
#include "qemu/mapped_file.h"
#include <glib.h>
#include <stdlib.h>
#include <string.h>

void qemu_init_mapped_file_data(QemuMappedFileData *data, const char *filename)
{
    memset(data, 0, sizeof(*data));
    data->filename = g_strdup(filename);
}

void qemu_init_mapped_segment_data(QemuMappedSegmentData *data)
{
    memset(data, 0, sizeof(*data));
}

bool qemu_open_mapped_file(QemuMappedFileData *data)
{
    qemu_close_mapped_file(data);

    if ((data->filename == NULL) || (*data->filename == '\0')) {
        return false;
    }

    data->handle =
        qemu_open_mapped_file_handle(data->filename, !data->writeonly, !data->readonly);
    return qemu_file_data_handle_valid(data->handle);
}

bool qemu_map_segment_data(QemuMappedFileData *file_data,
                           QemuMappedSegmentData *data)
{
    QemuMappedFileHandleType handle = file_data->handle;
    bool readable = !file_data->writeonly;
    bool writable = !file_data->readonly;
    uint64_t length = data->length;
    uint64_t offset = data->offset;
    void *pointer;
    pointer = qemu_map_file_data(handle,
                                 readable, writable,
                                 length, offset);
    if (!qemu_mapped_file_data_pointer_valid(pointer)) {
        return false;
    }
    if (writable &&
        !qemu_extend_mapped_segment(handle, pointer, offset, length, readable)) {
        qemu_unmap_data_segment(pointer, length);
        return false;
    }
    data->pointer = pointer;
    return true;
}

void qemu_unmap_segment_data(QemuMappedSegmentData *data)
{
    qemu_unmap_data_segment(data->pointer, data->length);
    data->pointer = NULL;   
}

void qemu_close_mapped_file(QemuMappedFileData *data)
{
    qemu_close_mapped_file_handle(data->handle);
    data->handle = 0;
}
