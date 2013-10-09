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
#include "ipc/ipc_channel.h"
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#define EXTERNAL_PCI_DEBUG
#ifdef EXTERNAL_PCI_DEBUG
enum {
    DEBUG_GENERAL, DEBUG_CHANNEL_DATA,
};
#define DBGBIT(x)	(1<<DEBUG_##x)
static int debugflags = DBGBIT(GENERAL);

#define IF_DBGOUT(what, code) do {              \
        if (debugflags & DBGBIT(what)) {        \
            code;                               \
        }                                       \
    } while (0)

#define DBGPRINT(fmt, ...)                                          \
    do {                                                            \
        fprintf(stderr, fmt , ## __VA_ARGS__);                      \
    } while(0)
#else
#define IF_DBGOUT(what, code) do {} while (0)
#endif
#define	DBGOUT(what, fmt, ...) \
    IF_DBGOUT(what, DBGPRINT("ipc_channel: " fmt "\n", ## __VA_ARGS__))

bool setup_ipc_channel(IPCChannel *channel,
                       const char *socket_path, bool use_abstract_path)
{
    struct sockaddr_un addr;
    socklen_t addr_size;
    char *addr_path = addr.sun_path;

    if (!socket_path || strlen(socket_path) == 0) {
        fprintf(stderr, "Invalid null socket path.\n");
        return false;
    }
    if (strlen(socket_path) > sizeof(addr.sun_path) - 2) {
        fprintf(stderr, "Invalid socket path \"%s\".\n", socket_path);
        return false;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    if (use_abstract_path) {
        addr_path++;
        addr_size =
            (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 +
                        strlen(socket_path));
    } else {
        addr_size = (socklen_t)sizeof(addr);
    }
    strcpy(addr_path, socket_path);

#ifdef SOCK_CLOEXEC
    channel->fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
    channel->fd = socket(AF_UNIX, SOCK_STREAM, 0);
#endif
    if (channel->fd != -1) {
#ifndef SOCK_CLOEXEC
        qemu_set_cloexec(channel->fd);
#endif
        while (connect(channel->fd,
                       (struct sockaddr *)(&addr),
                       addr_size) != 0) {
            if (errno != EINTR) {
                fprintf(stderr, "Failed to connect IPC socket %s (error: %s).\n", socket_path, strerror(errno));
                return false;
            }
        }
    } else {
        fprintf(stderr, "Failed to create IPC socket..\n");
        return false;
    }
    return true;
}

bool read_ipc_channel_data(IPCChannel *channel,
                           uint8_t *buffer, size_t size)
{
    ssize_t len;
    DBGOUT(CHANNEL_DATA, "read_ipc_channel_data size=%d", (unsigned)size);
    while (size > 0) {
        len = read(channel->fd, buffer, size);
        if (len == -1 && errno == EINTR) {
            continue;
        }
        if (len <= 0) {
            DBGOUT(CHANNEL_DATA, "read_ipc_channel_data failed. errno=%d", errno);
            return false;
        }
        IF_DBGOUT(CHANNEL_DATA, {
                ssize_t ii;
                DBGPRINT("read_ipc_channel_data: ");
                for(ii = 0; ii < len; ++ii) {
                    DBGPRINT("%2.2X", buffer[ii]);
                }
                DBGPRINT("\n");
            });
        size -= len;
        buffer += len;
    }
    return true;
}

bool write_ipc_channel_data(IPCChannel *channel,
                            const void *data, size_t size)
{
    const uint8_t *buffer = data;
    ssize_t len;
    IF_DBGOUT(CHANNEL_DATA, {
            DBGPRINT("write_ipc_channel_data: ");
            for(len = 0; (size_t)len < size; ++len) {
                DBGPRINT("%2.2X", ((uint8_t *)data)[len]);
            }
            DBGPRINT("\n");
        });
    while (size > 0) {
        len = write(channel->fd, buffer, size);
        if (len == -1 && errno == EINTR) {
            continue;
        }
        if (len <= 0) {
            DBGOUT(CHANNEL_DATA, "write_ipc_channel_data failed. errno=%d", errno);
            return false;
        }
        size -= len;
        buffer += len;
    }
    DBGOUT(CHANNEL_DATA, "write_ipc_channel_data success.");
    return true;    
}
