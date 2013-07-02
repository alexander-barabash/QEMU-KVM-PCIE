/*
 * Simple thread-safe FIFO for QEMU.
 *
 * Copyright (c) Mentor Graphics Corp.
 *
 * Author: Alexander Barabash <alexander.barabash@mentor.com>
 * Maintainer: Alexander Barabash <alexander_barabash@mentor.com>
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

#ifndef QEMU_SAFE_FIFO_H
#define QEMU_SAFE_FIFO_H

#include <glib.h>

typedef struct QemuSafeFifo QemuSafeFifo;
typedef struct QemuSafeFifoElement QemuSafeFifoElement;
typedef void (*QemuSafeFifoAction)(QemuSafeFifoElement *el, void *user_data);

struct QemuSafeFifoElement {
    QemuSafeFifoElement *next;
};

struct QemuSafeFifo {
    QemuSafeFifoElement * volatile first;
};

static inline
void qemu_safe_fifo_init(QemuSafeFifo *fifo)
{
    fifo->first = NULL;
}

static inline
QemuSafeFifoElement *qemu_safe_fifo_revert_list(QemuSafeFifoElement *l)
{
    QemuSafeFifoElement *p = l;
    QemuSafeFifoElement *q = p->next;
    p->next = 0;
    while(q) {
      QemuSafeFifoElement *n = q->next;
      q->next = p;
      p = q;
      q = n;
    }
    return p;
}

static inline
void qemu_safe_fifo_push_list(QemuSafeFifo *fifo, QemuSafeFifoElement *list)
{
    QemuSafeFifoElement *old_head;
    QemuSafeFifoElement *new_head = qemu_safe_fifo_revert_list(list);
    do {
        old_head = fifo->first;
        list->next = old_head;
    } while (!g_atomic_pointer_compare_and_exchange(&fifo->first,
                                                    old_head, new_head));
}

static inline
void qemu_safe_fifo_push_element(QemuSafeFifo *fifo, QemuSafeFifoElement *el)
{
    el->next = NULL;
    qemu_safe_fifo_push_list(fifo, el);
}

static inline
QemuSafeFifoElement *qemu_safe_fifo_pop_list(QemuSafeFifo *fifo)
{
    QemuSafeFifoElement *old_head;
    do {
        old_head = fifo->first;
        if (old_head == NULL) {
            return NULL;
        }
    } while (!g_atomic_pointer_compare_and_exchange(&fifo->first,
                                                    old_head, NULL));
    return qemu_safe_fifo_revert_list(old_head);
}

static inline
bool qemu_safe_fifo_has_data(QemuSafeFifo *fifo)
{
    return fifo->first != NULL;
}

static inline
void qemu_safe_fifo_apply_action_to_list(QemuSafeFifoElement *list,
                                         QemuSafeFifoAction action,
                                         void *user_data)
{
    while(list) {
      QemuSafeFifoElement *current = list;
      list = list->next;
      action(current, user_data);
    }
}

static inline
void qemu_safe_fifo_apply_action(QemuSafeFifo *fifo,
                                 QemuSafeFifoAction action,
                                 void *user_data)
{
    QemuSafeFifoElement *list;
    while ((list = qemu_safe_fifo_pop_list(fifo))) {
        qemu_safe_fifo_apply_action_to_list(list, action, user_data);
    }
}

#endif
