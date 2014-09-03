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
#include "sysemu/sysemu.h"
#include <glib.h>
#include <string.h>

/* #define EXTERNAL_PCI_DEBUG */
#ifdef EXTERNAL_PCI_DEBUG
enum {
    DEBUG_GENERAL, DEBUG_REQUESTS, DEBUG_INIT,
};
#define DBGBIT(x)	(1<<DEBUG_##x)
static int debugflags = DBGBIT(GENERAL);

#define	DBGOUT(what, fmt, ...) do { \
    if (debugflags & DBGBIT(what)) \
        fprintf(stderr, "ipc_connection: " fmt "\n", ## __VA_ARGS__); \
    } while (0)
#else
#define	DBGOUT(what, fmt, ...) do {} while (0)
#endif

static gpointer ipc_input_thread(gpointer opaque)
{
    IPCConnection *connection = opaque;
    IPCSizer *sizer = connection->ipc_sizer;
    unsigned header_size = sizer->ipc_header_size;
    uint8_t *header = g_malloc0(header_size);

    while (true) {
        IPCPacket *packet;
        unsigned packet_size;

        DBGOUT(REQUESTS, "read_ipc_channel_data %d bytes\n", header_size);
        if (!read_ipc_channel_data(&connection->channel, header, header_size)) {
            DBGOUT(GENERAL, "read_ipc_channel_data failed\n");
            /* BROKEN */
            qemu_system_shutdown_request();
            return NULL;
        } else {
            DBGOUT(REQUESTS, "read_ipc_channel_data success\n");
        }
        packet_size = sizer->get_packet_size(header);
        packet = g_malloc0(sizeof(IPCPacket) + packet_size);
        memcpy(ipc_packet_data(packet), header, header_size);
        DBGOUT(REQUESTS, "read_ipc_channel_data for attached data %d bytes\n",
               packet_size - header_size);
        if (!read_ipc_channel_data(&connection->channel,
                                   ipc_packet_data(packet) + header_size,
                                   packet_size - header_size)) {
            DBGOUT(GENERAL, "read_ipc_channel_data failed\n");
            /* BROKEN */
            qemu_system_shutdown_request();
            return NULL;
        } else {
            DBGOUT(REQUESTS, "read_ipc_channel_data success\n");
        }
        g_async_queue_push(connection->incoming, packet);
        qemu_bh_schedule(connection->bh);
    }
    free(header);
    return NULL;
}

static void init_threads(void)
{
    static bool already_called;
    if (already_called) {
        return;
    }
    already_called = true;
    DBGOUT(INIT, "init_threads glib_major_version=%d glib_minor_version=%d",
           glib_major_version, glib_minor_version);
    if (glib_major_version != 2) {
        return;
    }
    if (glib_minor_version >= 32) {
        /* g_thread_init() has been deprecated since version 2.32. */
        return;
    }
#if !GLIB_CHECK_VERSION(2, 32, 0)
    {
        /*
         * Since version 2.24, calling g_thread_init() multiple times is allowed,
         * but nothing happens except for the first call.
         */
        /* g_thread_get_initialized() is supported since 2.20. */
        bool threads_already_initialized = g_thread_get_initialized();
        DBGOUT(INIT, "g_thread_get_initialized() returned %s.",
               threads_already_initialized? "true": "false");
        if (!threads_already_initialized) {
            DBGOUT(INIT, "init_threads calling g_thread_init");
            g_thread_init(NULL);
        }
    }
#endif
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
    connection->aio_context = qemu_get_aio_context();
    connection->bh = aio_bh_new(connection->aio_context, ipc_bh, connection);
}

void activate_ipc_connection(IPCConnection *connection)
{
#if !GLIB_CHECK_VERSION(2, 32, 0)
    g_thread_create_full(/* func = */ ipc_input_thread,
                         /* data = */ connection,
                         /* stack_size = */ 0,
                         /* joinable = */ false,
                         /* bound = */ TRUE,
                         /* priority = */ G_THREAD_PRIORITY_NORMAL,
                         /* error = */ NULL);
#else
    g_thread_new(/* name = */ NULL,
                 /* func = */ ipc_input_thread,
                 /* data = */ connection);
#endif
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
        if (strcmp(connection->kind, connection_kind) != 0) {
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

void wait_on_ipc_connection(IPCConnection *connection,
                            bool (*wait_function)(void *), void *user_data) {
    while (!wait_function(user_data)) {
        aio_poll(connection->aio_context, true);
    }
}
