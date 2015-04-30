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

#include "qemu-common.h"
#include "config-host.h"
#include "ipc/ipc_channel.h"
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#else
#include <winsock2.h>
#endif
#include <errno.h>

#define IPC_DBGKEY ipc_channel
#include "ipc/ipc_debug.h"
IPC_DEBUG_ON(CHANNEL_DATA);

static bool connect_ipc_channel_tcp(IPCChannel *channel,
                                    const char *host,
                                    uint16_t port)
{
    struct hostent *server;
    struct sockaddr_in serv_addr;

    server = gethostbyname(host);
    if (!server) {
        error_printf("Host \"%s\" not found." IPC_DEBUG_NEWLINE, host);
        return false;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr,
           server->h_addr, 
           server->h_length);

#ifdef SOCK_CLOEXEC
    channel->fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
    channel->fd = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (channel->fd != -1) {
#ifndef SOCK_CLOEXEC
#ifndef _WIN32
        qemu_set_cloexec(channel->fd);
#endif
#endif
        while (connect(channel->fd,
                       (struct sockaddr *)(&serv_addr),
                       sizeof(serv_addr)) != 0) {
            if (errno != EINTR) {
                error_printf("Failed to connect IPC socket %s:%d (error: %s)." IPC_DEBUG_NEWLINE,
                             host, port, strerror(errno));
                return false;
            }
        }
    } else {
        error_printf("Failed to create IPC socket.." IPC_DEBUG_NEWLINE);
        return false;
    }
    return true;
}

#ifndef _WIN32
static bool connect_ipc_channel_unix(IPCChannel *channel,
                                     const char *socket_path,
                                     bool use_abstract_path)
{
    struct sockaddr_un addr;
    socklen_t addr_size;
    char *addr_path = addr.sun_path;

    if (strlen(socket_path) > sizeof(addr.sun_path) - 2) {
        error_printf("Invalid socket path \"%s\"." IPC_DEBUG_NEWLINE, socket_path);
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
                error_printf("Failed to connect IPC socket %s (error: %s)." IPC_DEBUG_NEWLINE, socket_path, strerror(errno));
                return false;
            }
        }
    } else {
        error_printf("Failed to create IPC socket.." IPC_DEBUG_NEWLINE);
        return false;
    }
    return true;
}
#endif

bool setup_ipc_channel(IPCChannel *channel, IPCChannelOps *ops,
                       const char *socket_path, bool use_abstract_path)
{
    bool connected = false;
    const char *last_colon;
    const char *port_string;
    if (!socket_path || strlen(socket_path) == 0) {
        error_printf("Invalid null socket path." IPC_DEBUG_NEWLINE);
        return false;
    }
    last_colon = strrchr(socket_path, ':');
    port_string = last_colon + 1;
    if (last_colon && *port_string) {
        size_t host_len = last_colon - socket_path;
        char *host = g_malloc0(host_len + 1);
        unsigned long port;
        char *endptr = NULL;
        if (host_len) {
            memcpy(host, socket_path, host_len);
        }
        port = strtoul(port_string, &endptr, 0);
        if ((endptr && *endptr) || (port > 0xFFFF)) {
            g_free(host);
            error_printf("Invalid socket path \"%s\"." IPC_DEBUG_NEWLINE, socket_path);
            return false;
        }
        connected = connect_ipc_channel_tcp(channel, host, (uint16_t)port);
        g_free(host);        
    } else {
#ifndef _WIN32
        connected = connect_ipc_channel_unix(channel, socket_path, use_abstract_path);
#endif
    }
    if (connected) {
        channel->ops = ops;
        return true;
    } else {
        return false;
    }
}

#ifdef _WIN32
static LPSTR formatError(int e)
{
	LPSTR error_string = NULL;

	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                      FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS,
                      NULL, e,
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                      (LPTSTR)&error_string, 0, NULL);
        return error_string;
}
#endif

bool read_ipc_channel_data(IPCChannel *channel,
                           uint8_t *buffer, size_t size)
{
    ssize_t len;
    DBGOUT(CHANNEL_DATA, "read_ipc_channel_data size=%d", (unsigned)size);
    while (size > 0) {
#ifndef _WIN32
        len = read(channel->fd, buffer, size);
        if (len == -1 && errno == EINTR) {
            DBGOUT(CHANNEL_DATA, "read_ipc_channel_data restarted. errno=%d", errno);
            continue;
        }
        if (len <= 0) {
            DBGOUT(CHANNEL_DATA, "read_ipc_channel_data failed. errno=%d", errno);
            return false;
        }
#else
        len = recv(channel->fd, (char *)buffer, size, MSG_WAITALL);
        if ((len <= 0) || (len == SOCKET_ERROR)) {
            int last_error = WSAGetLastError();
            LPSTR error_string;
            if (last_error == WSAEINTR)  {
                DBGOUT(CHANNEL_DATA, "read_ipc_channel_data restarted.");
                continue;
            }
            error_string = formatError(last_error);
            if (error_string) {
                DBGOUT(CHANNEL_DATA,
                       "read_ipc_channel_data failed. last error=%s", error_string);
                LocalFree(error_string);
            } else {
                DBGOUT(CHANNEL_DATA,
                       "read_ipc_channel_data failed. last error=%d", last_error);
            }
            return false;
        }
#endif
        IF_DBGOUT(CHANNEL_DATA, {
                ssize_t ii;
                DBGPRINT("read_ipc_channel_data: ");
                for(ii = 0; ii < len; ++ii) {
                    DBGPRINT("%2.2X", buffer[ii]);
                }
                DBGPRINT(IPC_DEBUG_NEWLINE);
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
            DBGPRINT(IPC_DEBUG_NEWLINE);
        });
    while (size > 0) {
#ifndef _WIN32
        len = write(channel->fd, buffer, size);
        if (len == -1 && errno == EINTR) {
            continue;
        }
        if (len <= 0) {
            DBGOUT(CHANNEL_DATA, "write_ipc_channel_data failed. errno=%d", errno);
            return false;
        }
#else
        len = send(channel->fd, (const char *)buffer, size, 0);
        if ((len <= 0) || (len == SOCKET_ERROR)) {
            int last_error = WSAGetLastError();
            LPSTR error_string;
            if (last_error == WSAEINTR)  {
                DBGOUT(CHANNEL_DATA, "write_ipc_channel_data restarted.");
                continue;
            }
            error_string = formatError(last_error);
            if (error_string) {
                DBGOUT(CHANNEL_DATA,
                       "write_ipc_channel_data failed. last error=%s", error_string);
                LocalFree(error_string);
            } else {
                DBGOUT(CHANNEL_DATA,
                       "write_ipc_channel_data failed. last error=%d", last_error);
            }
            return false;
        }
#endif
        size -= len;
        buffer += len;
    }
    DBGOUT(CHANNEL_DATA, "write_ipc_channel_data success.");
    return true;    
}
