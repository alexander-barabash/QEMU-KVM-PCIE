/*
 * QEMU IPC for external PCI
 *
 * Alexander Barabash
 * Alexander_Barabash@mentor.com
 * Copyright (c) 2013 Mentor Graphics Corp.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_IPC_CHANNEL_H
#define QEMU_IPC_CHANNEL_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct IPCChannel IPCChannel;

struct IPCChannel {
    int fd;
};

bool setup_ipc_channel(IPCChannel *channel,
                       const char *socket_path, bool use_abstract_path);

bool read_ipc_channel_data(IPCChannel *channel,
                           uint8_t *buffer, size_t size);

bool write_ipc_channel_data(IPCChannel *channel,
                            const void *data, size_t size);

#endif /* QEMU_IPC_CHANNEL_H */
