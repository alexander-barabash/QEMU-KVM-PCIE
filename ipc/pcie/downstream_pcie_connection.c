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

#define IPC_DBGKEY downstream_pcie_connection
#include "ipc/ipc_debug.h"
IPC_DEBUG_ON(REQUESTS);

static void ipc_timer_callback(void *opaque);

static uint64_t quantum = 1000 * 1000; /* Default: 1 milli second */

static void __attribute__((constructor)) init_ipc_quantum(void)
{
    const char *quantum_string = getenv("QEMU_IPC_QUANTUM");
    char *end;
    if (!quantum_string) {
        return;
    }
    quantum = strtoll(quantum_string, &end, 0);
    if (*end) {
        fprintf(stderr, "Invalid value for QEMU_IPC_QUANTUM: %s\n",
                quantum_string);
        exit(1);
    }   
}

static inline uint64_t get_current_time_ns(DownstreamPCIeConnection *connection)
{
    IPCChannel *channel = &connection->connection.channel;
    return channel->ops->get_current_time_ns(channel);
}

static void ipc_timer_callback(void *opaque)
{
    DownstreamPCIeConnection *connection = opaque;
    DBGOUT(REQUESTS, "ipc_timer_callback at local time %"PRId64"\n",
           get_current_time_ns(connection));
    send_downstream_time_pcie_msg(connection, connection->pci_dev);
}

static void *ipc_trans_data(void *transaction)
{
    return transaction - sizeof(uint64_t);
}

static inline void free_data_ipc_trans(void *transaction)
{
    free_data_ipc_packet(ipc_trans_data(transaction));
}

static inline void *ipc_packet_trans(void *packet)
{
    return ipc_packet_data(packet) + sizeof(uint64_t);
}

static inline uint64_t ipc_packet_time(void *packet)
{
    uint64_t time;
    memcpy(&time, ipc_packet_data(packet), sizeof(uint64_t));
    return time;
}

static inline uint64_t ipc_trans_time(void *transaction)
{
    uint64_t time;
    memcpy(&time, ipc_trans_data(transaction), sizeof(uint64_t));
    return time;
}

static inline void pcie_request_done(PCIeRequest *request) {
    if (request->transaction) {
        free_data_ipc_trans(request->transaction);
        request->transaction = NULL;
    }
    request->waiting = false;
}

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
    if (connection->connection.shutdown) {
        return 0;
    }
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
    if (connection->connection.shutdown) {
        return;
    }
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
    if (connection->connection.shutdown) {
        return 0;
    }
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
    if (connection->connection.shutdown) {
        return;
    }
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
    if (connection->connection.shutdown) {
        return 0;
    }
    decode_completion(request->transaction, &decoded);
    if (size > 0) {
        memcpy(data.bytes, decoded.payload_data + (addr & 3), size);
    }
    pcie_request_done(request);
    DBGOUT(REQUESTS, "read_downstream_pcie_config %d bytes @ 0x%X = 0x%X", size, addr, data.val);
    return data.val;
}

void send_downstream_time_pcie_msg(DownstreamPCIeConnection *connection,
                                   PCIDevice *pci_dev)
{
    PCIeRequest *request;
    uint16_t requester_id = pcie_requester_id(pci_dev);
    uint8_t tag;
    DBGOUT(REQUESTS, "in send_downstream_time_pcie_msg for device %s at %"PRId64, pci_dev->name,
           get_current_time_ns(connection));
    if (!register_pcie_request(connection->requesters_table,
                               requester_id,
                               &request,
                               &tag)) {
        error_report("Cannot allocate PCIe request for device %s\n", pci_dev->name);
        return;
    } else {
        DBGOUT(REQUESTS, "Allocated PCIe request for device %s", pci_dev->name);
    }
    request->is_time_request = true;
    if (!send_ipc_pcie_time_msg(&connection->connection.channel,
                                requester_id,
                                tag)) {
        error_report("Cannot send time packet for device %s\n", pci_dev->name);
    }
    wait_on_pcie_request(&connection->connection, request);
    if (connection->connection.shutdown) {
        return;
    }
    DBGOUT(REQUESTS, "send_downstream_time_pcie_msg: returned_time %"PRId64"",
           ipc_trans_time(request->transaction));
    pcie_request_done(request);
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

static void handle_completion(void *transaction, DownstreamPCIeConnection *connection)
{
    PCIE_CompletionDecoded decoded;
    PCIeRequest *request;

    DBGOUT(REQUESTS, "handle_completion at time %"PRId64". Local time %"PRId64"\n",
           ipc_trans_time(transaction), get_current_time_ns(connection));

    decode_completion(transaction, &decoded);
    request = find_pcie_request(connection->requesters_table,
                                decoded.requester_id,
                                decoded.tag);
    if ((request == NULL) || !request->waiting) {
        /* Nobody waits for this. */
        free_data_ipc_trans(transaction);
    } else {
        pcie_request_ready(request, transaction);
        /* NOTE: transaction is now owned by the requester. */
    }
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

static void handle_request(void *transaction, DownstreamPCIeConnection *connection)
{
    PCIE_RequestDecoded decoded;

    DBGOUT(REQUESTS, "handle_request at time %"PRId64". Local time %"PRId64"\n",
           ipc_trans_time(transaction), get_current_time_ns(connection));

    decode_request(transaction, &decoded);
    if (decoded.is_memory) {
        handle_memory_request(&decoded, connection);
    } else if (decoded.is_io) {
        handle_io_request(&decoded, connection);
    } else if (decoded.is_config) {
        handle_config_request(&decoded, connection);
    } else {
        handle_msg_request(&decoded, connection);
    }
    free_data_ipc_trans(transaction);
}

static void handle_packet(IPCPacket *packet, IPCConnection *ipc_connection)
{
    uint8_t *transaction = ipc_packet_trans(packet);
    DownstreamPCIeConnection *connection =
        downstream_by_ipc_connection(ipc_connection);
    IPCChannel *channel = &ipc_connection->channel;
    uint64_t transaction_time =
        ipc_connection->ops->get_ipc_packet_time(ipc_connection, packet);

    if (transaction_time + 1 != 0) {
        channel->ops->rearm_timer(channel, transaction_time);
    }

    if (!ipc_connection->waiting) {
        // need wait
    }

    if (pcie_trans_is_completion(transaction)) {
        handle_completion(transaction, connection);
    } else {
        handle_request(transaction, connection);
    }
}

static uint64_t
get_ipc_packet_time(IPCConnection *connection, IPCPacket *packet)
{
    uint8_t *transaction = ipc_packet_trans(packet);
    return ipc_trans_time(transaction);
}

static uint64_t
ipc_connection_get_current_time_ns(IPCConnection *connection)
{
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static struct IPCConnectionOps ipc_connection_ops = {
    .get_current_time_ns = ipc_connection_get_current_time_ns,
    .get_ipc_packet_time = get_ipc_packet_time,
};

static DownstreamPCIeConnection *create_connection(void)
{
    DownstreamPCIeConnection *connection =
        (DownstreamPCIeConnection *)g_malloc0(sizeof(DownstreamPCIeConnection));
    connection->connection.ops = &ipc_connection_ops;
    return connection;
}

static const char *ipc_connection_kind = "pcie_downstream";

static void init_connection(DownstreamPCIeConnection *connection, PCIDevice *pci_dev)
{
    init_ipc_connection(&connection->connection,
                        ipc_connection_kind,
                        ipc_pcie_sizer(),
                        handle_packet);

    connection->pci_dev = pci_dev;
    connection->pci_bus_num = pci_bus_num(pci_dev->bus);
    connection->dma_as = pci_get_address_space(pci_dev);
    connection->io_as = pci_get_io_address_space(pci_dev);
    connection->requesters_table = create_pcie_requesters_table();

    activate_ipc_connection(&connection->connection);
}

static uint64_t ipc_channel_get_current_time_ns(IPCChannel *channel)
{
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void ipc_channel_rearm_timer(IPCChannel *channel, uint64_t transaction_time)
{
    IPCConnection *ipc_connection = ipc_channel_connection(channel);
    DownstreamPCIeConnection *connection =
        downstream_by_ipc_connection(ipc_connection);
    uint64_t local_time = channel->ops->get_current_time_ns(channel);
    uint64_t target_time = transaction_time + quantum;

    if (connection->timer == NULL) {
        connection->timer =
            timer_new_ns(QEMU_CLOCK_VIRTUAL, ipc_timer_callback, connection);
    }

    if (local_time >= target_time) {
        target_time = local_time + quantum;
    }
    timer_mod_ns(connection->timer, target_time);
}

static IPCChannelOps ipc_channel_ops = {
    .get_current_time_ns = ipc_channel_get_current_time_ns,
    .rearm_timer = ipc_channel_rearm_timer,
};

static DownstreamPCIeConnection *get_connection(const char *socket_path,
                                                bool use_abstract_path,
                                                PCIDevice *pci_dev)
{
    bool error;
    IPCConnection *ipc_connection = find_ipc_connection(ipc_connection_kind,
                                                        socket_path,
                                                        use_abstract_path,
                                                        &error);
    DownstreamPCIeConnection *connection =
        downstream_by_ipc_connection(ipc_connection);
    if (connection == NULL) {
        if (error) {
            return NULL;
        }
        connection = create_connection();
        if (setup_ipc_channel(&connection->connection.channel, &ipc_channel_ops,
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
