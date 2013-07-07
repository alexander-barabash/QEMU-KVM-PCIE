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

#include "config-host.h"
#include "ipc/ipc_connection.h"
#include "qemu/main-loop.h"
#include "block/aio.h"
#include <glib.h>
#include <string.h>

static gpointer ipc_input_thread(gpointer opaque)
{
    IPCConnection *connection = opaque;
    IPCSizer *sizer = connection->ipc_sizer;
    unsigned header_size = sizer->ipc_header_size;
    uint8_t *header = g_malloc0(header_size);

    while (true) {
        IPCPacket *packet;
        unsigned packet_size;

        if (!read_ipc_channel_data(&connection->channel, header, header_size)) {
            /* BROKEN */
            exit(0);
            break;
        }
        packet_size = sizer->get_packet_size(header);
        packet = g_malloc0(sizeof(IPCPacket) + packet_size);
        memcpy(ipc_packet_data(packet), header, header_size);
        if (!read_ipc_channel_data(&connection->channel,
                                   ipc_packet_data(packet) + header_size,
                                   packet_size - header_size)) {
            /* BROKEN */
            exit(0);
            break;
        }
        g_async_queue_push(connection->incoming, packet);
        qemu_bh_schedule(connection->bh);
    }
    free(header);
    return NULL;
}

static inline void init_threads(void)
{
    if (!g_thread_supported()) {
#if !GLIB_CHECK_VERSION(2, 31, 0)
        g_thread_init(NULL);
#else
        fprintf(stderr, "glib threading failed to initialize.\n");
        exit(1);
#endif
    }
}

static void ipc_bh(void *opaque)
{
    IPCConnection *connection = opaque;
    IPCPacket *packet = g_async_queue_try_pop(connection->incoming);
    if (packet) {
        connection->packet_handler(packet, connection);
        qemu_bh_schedule(connection->bh);        
    }
}

void init_ipc_connection(IPCConnection *connection,
                         const char *connection_kind,
                         IPCSizer *ipc_sizer,
                         IPCPacketHandler packet_handler)
{
    init_threads();
    connection->kind = g_strdup(connection_kind);
    connection->incoming = g_async_queue_new();
    connection->ipc_sizer = ipc_sizer;
    connection->packet_handler = packet_handler;
    connection->bh = qemu_bh_new(ipc_bh, connection);
}

void activate_ipc_connection(IPCConnection *connection)
{
    g_thread_create(ipc_input_thread, connection, false, NULL);
}

static GHashTable *get_ipc_connection_table(bool use_abstract_path)
{
    static GHashTable *tables[2];
    int index = use_abstract_path ? 1 : 0;
    if (tables[index] == NULL) {
        tables[index] = g_hash_table_new (g_str_hash, g_str_equal);
    }
    return tables[index];
}

IPCConnection *find_ipc_connection(const char *connection_kind,
                                   const char *socket_path,
                                   bool use_abstract_path,
                                   bool *error)
{
    GHashTable *table = get_ipc_connection_table(use_abstract_path);
    IPCConnection *connection;
    if (!socket_path) {
        *error = true;
        return NULL;
    }
    connection = g_hash_table_lookup(table, socket_path);
    if (connection != NULL) {
        if (g_strcmp0(connection->kind, connection_kind) != 0) {
            *error = true;
            return NULL;
        }
    }
    *error = false;
    return connection;
}

void register_ipc_connection(const char *socket_path,
                             bool use_abstract_path,
                             IPCConnection *connection)
{
    GHashTable *table = get_ipc_connection_table(use_abstract_path);
    g_hash_table_replace(table, g_strdup(socket_path), connection);
}
