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

#ifndef QEMU_IPC_CONNECTION_H
#define QEMU_IPC_CONNECTION_H

#include "ipc/ipc_channel.h"
#include "ipc/ipc_sizer.h"
#include "qemu/typedefs.h"
#include "block/aio.h"
#include <glib.h>

typedef struct IPCConnection IPCConnection;
typedef struct IPCPacket IPCPacket;

typedef void (*IPCPacketHandler)(IPCPacket *packet, IPCConnection *connection);

struct IPCConnection {
    IPCChannel channel;
    char *kind;
    GAsyncQueue *incoming;
    IPCSizer *ipc_sizer;
    AioContext *aio_context;
    QEMUBH *bh;
    IPCPacketHandler packet_handler;
};

struct IPCPacket {
    unsigned packet_size;
};

static inline uint8_t *ipc_packet_data(IPCPacket *packet)
{
    return (uint8_t *)(packet + 1);
}

static inline IPCPacket *ipc_packet_from_data(uint8_t *data)
{
    return ((IPCPacket *)data) - 1;
}

static inline void free_data_ipc_packet(uint8_t *data)
{
    free(ipc_packet_from_data(data));
}

void init_ipc_connection(IPCConnection *connection,
                         const char *connection_kind,
                         IPCSizer *ipc_sizer,
                         IPCPacketHandler packet_handler);
void activate_ipc_connection(IPCConnection *connection);

IPCConnection *find_ipc_connection(const char *connection_kind,
                                   const char *socket_path,
                                   bool use_abstract_path,
                                   bool *error);

void register_ipc_connection(const char *socket_path,
                             bool use_abstract_path,
                             IPCConnection *connection);

void wait_on_ipc_connection(IPCConnection *connection,
                            bool (*wait_function)(void *), void *user_data);

#endif /* QEMU_IPC_CONNECTION_H */
