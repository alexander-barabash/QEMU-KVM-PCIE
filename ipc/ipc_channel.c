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
                fprintf(stderr, "Failed to connect IPC socket %s.\n", socket_path);
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
    while (size > 0) {
        len = read(channel->fd, buffer, size);
        if (len == -1 && errno == EINTR) {
            continue;
        }
        if (len <= 0) {
            return false;
        }
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
    while (size > 0) {
        len = write(channel->fd, buffer, size);
        if (len == -1 && errno == EINTR) {
            continue;
        }
        if (len <= 0) {
            return false;
        }
        size -= len;
        buffer += len;
    }
    return true;    
}
