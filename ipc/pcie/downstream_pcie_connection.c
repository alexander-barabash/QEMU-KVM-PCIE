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
#include "ipc/pcie/downstream_pcie_connection.h"
#include "ipc/pcie/requesters_table.h"
#include "ipc/ipc_channel.h"
#include "ipc/pcie/ipc_pcie_sizer.h"
#include "ipc/pcie/pcie_trans.h"
#include "ipc/pcie/pcie_trans_decoded.h"
#include "ipc/pcie/pcie_trans_encode.h"
#include "qemu/error-report.h"
#include <glib.h>

/* #define EXTERNAL_PCI_DEBUG */
#ifdef EXTERNAL_PCI_DEBUG
enum {
    DEBUG_GENERAL, DEBUG_REQUESTS,
};
#define DBGBIT(x)	(1<<DEBUG_##x)
static int debugflags = DBGBIT(GENERAL);

#define	DBGOUT(what, fmt, ...) do { \
    if (debugflags & DBGBIT(what)) \
        fprintf(stderr, "downstream_pcie_connection: " fmt "\n", ## __VA_ARGS__); \
    } while (0)
#else
#define	DBGOUT(what, fmt, ...) do {} while (0)
#endif

void write_downstream_pcie_memory(DownstreamPCIeConnection *connection,
                                  PCIDevice *pci_dev,
                                  hwaddr addr, uint64_t val, unsigned size)
{
    PCIeRequest *request;
    uint16_t requester_id = pcie_requester_id(pci_dev);
    uint8_t tag;
    union {
        uint8_t bytes[8];
        uint64_t val;
    } data;
    data.val = val;
    DBGOUT(REQUESTS, "write_downstream_pcie_memory %d bytes @ 0x%llX := 0x%llX", size,
           (unsigned long long)addr, (unsigned long long)data.val);
    if (!register_pcie_request(connection->requesters_table,
                               requester_id,
                               &request,
                               &tag)) {
        error_report("Cannot allocate PCIe request for device %s\n", pci_dev->name);
        return;
    }
    send_ipc_pcie_memory_write_request(&connection->connection.channel,
                                       requester_id,
                                       tag,
                                       addr,
                                       size,
                                       data.bytes);
    pcie_request_done(request);
    DBGOUT(REQUESTS, "write_downstream_pcie_memory %d bytes @ 0x%llX done", size, (unsigned long long)addr);
}

uint64_t read_downstream_pcie_memory(DownstreamPCIeConnection *connection,
                                     PCIDevice *pci_dev,
                                     hwaddr addr, unsigned size)
{
    PCIeRequest *request;
    uint16_t requester_id = pcie_requester_id(pci_dev);
    uint8_t tag;
    PCIE_CompletionDecoded decoded;
    union {
        uint8_t bytes[8];
        uint64_t val;
    } data;
    data.val = 0;
    DBGOUT(REQUESTS, "read_downstream_pcie_memory %d bytes @ 0x%llX", size, (unsigned long long)addr);
    if (!register_pcie_request(connection->requesters_table,
                               requester_id,
                               &request,
                               &tag)) {
        error_report("Cannot allocate PCIe request for device %s\n", pci_dev->name);
        return 0;
    }
    send_ipc_pcie_memory_read_request(&connection->connection.channel,
                                      requester_id,
                                      tag,
                                      addr,
                                      size);
    wait_on_pcie_request(&connection->connection, request);
    decode_completion(request->transaction, &decoded);
    if (size > 0) {
        memcpy(data.bytes, decoded.payload_data + (addr & 3), size);
    }
    pcie_request_done(request);
    DBGOUT(REQUESTS, "read_downstream_pcie_memory %d bytes @ 0x%llX = 0x%llX", size,
           (unsigned long long)addr, (unsigned long long)data.val);
    return data.val;
}

void write_downstream_pcie_io(DownstreamPCIeConnection *connection,
                              PCIDevice *pci_dev,
                              hwaddr addr, uint32_t val, unsigned size)
{
    PCIeRequest *request;
    uint16_t requester_id = pcie_requester_id(pci_dev);
    uint8_t tag;
    union {
        uint8_t bytes[4];
        uint32_t val;
    } data;
    data.val = val;
    DBGOUT(REQUESTS, "write_downstream_pcie_io %d bytes @ 0x%llX := 0x%X", size,
           (unsigned long long)addr, data.val);
    if (!register_pcie_request(connection->requesters_table,
                               requester_id,
                               &request,
                               &tag)) {
        error_report("Cannot allocate PCIe request for device %s\n", pci_dev->name);
        return;
    }
    send_ipc_pcie_io_write_request(&connection->connection.channel,
                                   requester_id,
                                   tag,
                                   addr,
                                   size,
                                   data.bytes);
    wait_on_pcie_request(&connection->connection, request);
    pcie_request_done(request);
    DBGOUT(REQUESTS, "write_downstream_pcie_io %d bytes @ 0x%llX done", size, (unsigned long long)addr);
}

uint32_t read_downstream_pcie_io(DownstreamPCIeConnection *connection,
                                 PCIDevice *pci_dev,
                                 hwaddr addr, unsigned size)
{
    PCIeRequest *request;
    uint16_t requester_id = pcie_requester_id(pci_dev);
    uint8_t tag;
    PCIE_CompletionDecoded decoded;
    union {
        uint8_t bytes[4];
        uint32_t val;
    } data;
    data.val = 0;
    DBGOUT(REQUESTS, "read_downstream_pcie_io %d bytes @ 0x%llX", size, (unsigned long long)addr);
    if (!register_pcie_request(connection->requesters_table,
                               requester_id,
                               &request,
                               &tag)) {
        error_report("Cannot allocate PCIe request for device %s\n", pci_dev->name);
        return 0;
    }
    send_ipc_pcie_memory_read_request(&connection->connection.channel,
                                      requester_id,
                                      tag,
                                      addr,
                                      size);
    wait_on_pcie_request(&connection->connection, request);
    decode_completion(request->transaction, &decoded);
    if (size > 0) {
        memcpy(data.bytes, decoded.payload_data + (addr & 3), size);
    }
    pcie_request_done(request);
    DBGOUT(REQUESTS, "read_downstream_pcie_io %d bytes @ 0x%llX = 0x%X", size,
           (unsigned long long)addr, data.val);
    return data.val;
}

void write_downstream_pcie_config(DownstreamPCIeConnection *connection,
                                  PCIDevice *pci_dev,
                                  uint32_t addr, uint32_t val, unsigned size)
{
    PCIeRequest *request;
    uint16_t requester_id = pcie_requester_id(pci_dev);
    uint8_t tag;
    union {
        uint8_t bytes[4];
        uint32_t val;
    } data;
    data.val = val;
    DBGOUT(REQUESTS, "write_downstream_pcie_config %d bytes @ 0x%X := 0x%X", size, addr, val);
    if (!register_pcie_request(connection->requesters_table,
                               requester_id,
                               &request,
                               &tag)) {
        error_report("Cannot allocate PCIe request for device %s\n", pci_dev->name);
        return;
    }
    send_ipc_pcie_config_write_request(&connection->connection.channel,
                                       /* is_type1 */ true,
                                       requester_id,
                                       tag,
                                       pci_bus_num(pci_dev->bus),
                                       PCI_SLOT(pci_dev->devfn),
                                       PCI_FUNC(pci_dev->devfn),
                                       addr,
                                       size,
                                       data.bytes);
    wait_on_pcie_request(&connection->connection, request);
    pcie_request_done(request);
    DBGOUT(REQUESTS, "write_downstream_pcie_config %d bytes @ 0x%X done.", size, addr);
}

uint32_t read_downstream_pcie_config(DownstreamPCIeConnection *connection,
                                     PCIDevice *pci_dev,
                                     uint32_t addr, unsigned size)
{
    PCIeRequest *request;
    uint16_t requester_id = pcie_requester_id(pci_dev);
    uint8_t tag;
    PCIE_CompletionDecoded decoded;
    union {
        uint8_t bytes[4];
        uint32_t val;
    } data;
    data.val = 0;
    DBGOUT(REQUESTS, "read_downstream_pcie_config %d bytes @ 0x%X", size, addr);
    if (!register_pcie_request(connection->requesters_table,
                               requester_id,
                               &request,
                               &tag)) {
        error_report("Cannot allocate PCIe request for device %s\n", pci_dev->name);
        return 0;
    }
    send_ipc_pcie_config_read_request(&connection->connection.channel,
                                      /* is_type1 */ true,
                                      requester_id,
                                      tag,
                                      pci_bus_num(pci_dev->bus),
                                      PCI_SLOT(pci_dev->devfn),
                                      PCI_FUNC(pci_dev->devfn),
                                      addr,
                                      size);
    wait_on_pcie_request(&connection->connection, request);
    decode_completion(request->transaction, &decoded);
    if (size > 0) {
        memcpy(data.bytes, decoded.payload_data + (addr & 3), size);
    }
    pcie_request_done(request);
    DBGOUT(REQUESTS, "read_downstream_pcie_config %d bytes @ 0x%X = 0x%X", size, addr, data.val);
    return data.val;
}

void send_special_downstream_pcie_msg(DownstreamPCIeConnection *connection,
                                      PCIDevice *pci_dev,
                                      uint16_t external_device_id)
{
    PCIeRequest *request;
    uint16_t requester_id = pcie_requester_id(pci_dev);
    uint8_t tag;
    DBGOUT(REQUESTS, "in send_special_downstream_pcie_msg for device %s", pci_dev->name);
    if (!register_pcie_request(connection->requesters_table,
                               requester_id,
                               &request,
                               &tag)) {
        error_report("Cannot allocate PCIe request for device %s\n", pci_dev->name);
        return;
    } else {
        DBGOUT(REQUESTS, "Allocated PCIe request for device %s", pci_dev->name);
    }
    if (!send_ipc_pcie_special_msg(&connection->connection.channel,
                                   requester_id,
                                   tag,
                                   pci_bus_num(pci_dev->bus),
                                   PCI_SLOT(pci_dev->devfn),
                                   PCI_FUNC(pci_dev->devfn),
                                   external_device_id)) {
        error_report("Cannot send connection packet for device %s\n", pci_dev->name);
    }
    pcie_request_done(request);
}

static uint16_t root_completer_id(DownstreamPCIeConnection *connection)
{
    /* TODO: maybe find out the completer's devfn somehow. */
    uint32_t devfn = 0;
    return (connection->pci_bus_num << 8) | (devfn & 0xFF);
}

static bool handle_completion(void *transaction, DownstreamPCIeConnection *connection)
{
    PCIE_CompletionDecoded decoded;
    PCIeRequest *request;
    decode_completion(transaction, &decoded);
    request = find_pcie_request(connection->requesters_table,
                                decoded.requester_id,
                                decoded.tag);
    if ((request == NULL) || !request->waiting) {
        /* Nobody waits for this. */
        free_data_ipc_packet(request->transaction);
    } else {
        pcie_request_ready(request, transaction);
        /* NOTE: transaction is now owned by the requester. */
    }
    return true;
}

static bool handle_memory_write(PCIE_RequestDecoded *decoded,
                                DownstreamPCIeConnection *connection)
{
    if (decoded->actual_size > 0) {
        dma_memory_write(connection->dma_as,
                         decoded->addr,
                         decoded->actual_payload,
                         decoded->actual_size);
    }
    return true;
    /* No completion needed here. */
}

static bool handle_read(PCIE_RequestDecoded *decoded,
                        AddressSpace *as,
                        DownstreamPCIeConnection *connection)
{
    IPCChannel *channel = &connection->connection.channel;
    uint16_t completer_id = root_completer_id(connection);
    unsigned result_shift = decoded->addr & 3;
    uint16_t read_size = decoded->actual_size;
    uint8_t *result_data = (read_size > 0) ? alloca(read_size) : NULL;
    if (read_size == 0) {
        return send_ipc_pcie_read_completion(channel, decoded->transaction,
                                             completer_id,
                                             NULL, 0, 0);
    } else if (!dma_memory_read(as, decoded->addr, result_data,
                                read_size)) {
        /* failure is signaled by dma_memory_read by returning true */
        return send_ipc_pcie_read_completion(channel, decoded->transaction,
                                             completer_id,
                                             result_data, read_size,
                                             result_shift);
    } else {
        return send_ipc_pcie_read_failure(channel, decoded->transaction,
                                          completer_id);
    }
}

static bool handle_memory_read(PCIE_RequestDecoded *decoded,
                               DownstreamPCIeConnection *connection)
{
    return handle_read(decoded, connection->dma_as, connection);
}

static bool handle_memory_request(PCIE_RequestDecoded *decoded,
                                  DownstreamPCIeConnection *connection)
{
    if (decoded->has_payload) {
        return handle_memory_write(decoded, connection);
    } else {
        return handle_memory_read(decoded, connection);
    }
}

static bool handle_io_write(PCIE_RequestDecoded *decoded,
                            DownstreamPCIeConnection *connection)
{
    IPCChannel *channel = &connection->connection.channel;
    /* failure is signaled by dma_memory_write by returning true */
    if ((decoded->actual_size > 0) &&
        !dma_memory_write(connection->io_as,
                          decoded->addr,
                          decoded->actual_payload,
                          decoded->actual_size)) {
        return send_ipc_pcie_write_completion(channel, decoded->transaction,
                                              root_completer_id(connection));
    } else {
        return send_ipc_pcie_write_failure(channel, decoded->transaction,
                                           root_completer_id(connection));
    }
}

static bool handle_io_read(PCIE_RequestDecoded *decoded,
                           DownstreamPCIeConnection *connection)
{
    return handle_read(decoded, connection->io_as, connection);
}

static bool handle_io_request(PCIE_RequestDecoded *decoded,
                              DownstreamPCIeConnection *connection)
{
    if (decoded->has_payload) {
        return handle_io_write(decoded, connection);
    } else {
        return handle_io_read(decoded, connection);
    }
}

static bool handle_config_request(PCIE_RequestDecoded *decoded,
                                  DownstreamPCIeConnection *connection)
{
    IPCChannel *channel = &connection->connection.channel;
    /* config requests upstream are not allowed. */
    if (decoded->has_payload) {
        return send_ipc_pcie_write_failure(channel, decoded->transaction,
                                           root_completer_id(connection));
    } else {
        return send_ipc_pcie_read_failure(channel, decoded->transaction,
                                          root_completer_id(connection));
    }
}

static bool handle_msg_request(PCIE_RequestDecoded *decoded,
                               DownstreamPCIeConnection *connection)
{
    /* TODO */
    return true;
}

static bool handle_request(void *transaction, DownstreamPCIeConnection *connection)
{
    PCIE_RequestDecoded decoded;
    decode_request(transaction, &decoded);

    if (decoded.is_memory) {
        return handle_memory_request(&decoded, connection);
    } else if (decoded.is_io) {
        return handle_io_request(&decoded, connection);
    } else if (decoded.is_config) {
        return handle_config_request(&decoded, connection);
    } else {
        return handle_msg_request(&decoded, connection);
    }
}

static void handle_packet(IPCPacket *packet, IPCConnection *base_connection)
{
    uint8_t *transaction = ipc_packet_data(packet);
    DownstreamPCIeConnection *connection = (DownstreamPCIeConnection *)base_connection;
    if (pcie_trans_is_completion(transaction)) {
        handle_completion(transaction, connection);
    } else {
        handle_request(transaction, connection);
        free(packet);
    }
}

static DownstreamPCIeConnection *create_connection(void)
{
    return (DownstreamPCIeConnection *)g_malloc0(sizeof(DownstreamPCIeConnection));
}

static const char *ipc_connection_kind = "pcie_downstream";

static void init_connection(DownstreamPCIeConnection *connection, PCIDevice *pci_dev)
{
    init_ipc_connection(&connection->connection,
                        ipc_connection_kind,
                        ipc_pcie_sizer(),
                        handle_packet);

    connection->pci_bus_num = pci_bus_num(pci_dev->bus);
    connection->dma_as = pci_get_address_space(pci_dev);
    connection->io_as = pci_get_io_address_space(pci_dev);
    connection->requesters_table = create_pcie_requesters_table();

    activate_ipc_connection(&connection->connection);
}

static DownstreamPCIeConnection *get_connection(const char *socket_path,
                                                bool use_abstract_path,
                                                PCIDevice *pci_dev)
{
    bool error;
    IPCConnection *base_connection = find_ipc_connection(ipc_connection_kind,
                                                         socket_path,
                                                         use_abstract_path,
                                                         &error);
    DownstreamPCIeConnection *connection = (DownstreamPCIeConnection *)base_connection;
    if (connection == NULL) {
        if (error) {
            return NULL;
        }
        connection = create_connection();
        if (setup_ipc_channel(&connection->connection.channel,
                              socket_path, use_abstract_path)) {
            init_connection(connection, pci_dev);
            register_ipc_connection(socket_path, use_abstract_path,
                                    &connection->connection);
        } else {
            free(connection);
            return NULL;
        }
    } else if ((connection->pci_bus_num != pci_bus_num(pci_dev->bus)) ||
               (connection->dma_as != pci_get_address_space(pci_dev)) ||
               (connection->io_as != pci_get_io_address_space(pci_dev))) {
        fprintf(stderr, "IPC channel %s already in use.\n", socket_path);
        return NULL;
    }
    return connection;
}

DownstreamPCIeConnection *init_pcie_downstream_ipc(const char *socket_path,
                                      bool use_abstract_path,
                                      PCIDevice *pci_dev)
{
    DownstreamPCIeConnection *connection =
        get_connection(socket_path, use_abstract_path, pci_dev);
    if (connection == NULL) {
        fprintf(stderr, "Failed to open IPC channel %s.\n", socket_path);
    }
    return connection;
}
