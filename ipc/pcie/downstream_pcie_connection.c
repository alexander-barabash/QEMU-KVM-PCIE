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

#include "ipc/pcie/downstream_pcie_connection.h"
#include "ipc/ipc_connection.h"
#include "ipc/ipc_channel.h"
#include "ipc/pcie/ipc_pcie_sizer.h"
#include "ipc/pcie/pcie_trans.h"
#include "ipc/pcie/pcie_trans_decoded.h"
#include "ipc/pcie/pcie_trans_encode.h"
#include <glib.h>

struct DownstreamPCIeConnection {
    IPCConnection connection;
    uint32_t pci_bus_num;
    AddressSpace *dma_as;
    AddressSpace *io_as;
};

static uint8_t root_completer_id(DownstreamPCIeConnection *ipc_data)
{
    /* TODO: maybe find out the completer's devfn somehow. */
    uint32_t devfn = 0;
    return (ipc_data->pci_bus_num << 8) | (devfn & 0xFF);
}

static bool handle_completion(void *transaction, DownstreamPCIeConnection *ipc_data)
{
    PCIE_CompletionDecoded decoded;
    decode_completion(transaction, &decoded);
    /* TODO */
    free(ipc_packet_from_data(transaction));
    return true;
}

static bool handle_memory_write(PCIE_RequestDecoded *decoded,
                                DownstreamPCIeConnection *ipc_data)
{
    /* TODO: use byte enables. */
    dma_memory_write(ipc_data->dma_as, decoded->addr, decoded->payload_data,
                     decoded->size_in_dw * 4);
    return true;
    /* No completion needed here. */
}

static bool handle_read(PCIE_RequestDecoded *decoded,
                        AddressSpace *as,
                        DownstreamPCIeConnection *ipc_data)
{
    IPCChannel *channel = &ipc_data->connection.channel;
    uint8_t completer_id = root_completer_id(ipc_data);
    uint16_t read_size = decoded->size_in_dw * 4;
    uint16_t result_size = (read_size > 0) ? read_size : 1;
    uint8_t *result_data = alloca(result_size);
    if (read_size == 0) {
        result_data[0] = 0;
        return send_ipc_pcie_read_completion(channel, decoded->transaction,
                                             completer_id,
                                             result_data, result_size);
    } else if (!dma_memory_read(as, decoded->addr, result_data,
                                result_size)) {
        /* TODO: use byte enables. */
        /* failure is signaled by dma_memory_read by returning true */
        return send_ipc_pcie_read_completion(channel, decoded->transaction,
                                             completer_id,
                                             result_data, result_size);
    } else {
        return send_ipc_pcie_read_failure(channel, decoded->transaction,
                                          completer_id);
    }
}

static bool handle_memory_read(PCIE_RequestDecoded *decoded,
                               DownstreamPCIeConnection *ipc_data)
{
    return handle_read(decoded, ipc_data->dma_as, ipc_data);
}

static bool handle_memory_request(PCIE_RequestDecoded *decoded,
                                  DownstreamPCIeConnection *ipc_data)
{
    if (decoded->has_payload) {
        return handle_memory_write(decoded, ipc_data);
    } else {
        return handle_memory_read(decoded, ipc_data);
    }
}

static bool handle_io_write(PCIE_RequestDecoded *decoded,
                            DownstreamPCIeConnection *ipc_data)
{
    IPCChannel *channel = &ipc_data->connection.channel;
    /* TODO: use byte enables. */
    /* failure is signaled by dma_memory_write by returning true */
    if (!dma_memory_write(ipc_data->io_as,
                          decoded->addr,
                          decoded->payload_data,
                          decoded->size_in_dw * 4)) {
        return send_ipc_pcie_write_completion(channel, decoded->transaction,
                                              root_completer_id(ipc_data));
    } else {
        return send_ipc_pcie_write_failure(channel, decoded->transaction,
                                           root_completer_id(ipc_data));
    }
}

static bool handle_io_read(PCIE_RequestDecoded *decoded,
                           DownstreamPCIeConnection *ipc_data)
{
    return handle_read(decoded, ipc_data->io_as, ipc_data);
}

static bool handle_io_request(PCIE_RequestDecoded *decoded,
                              DownstreamPCIeConnection *ipc_data)
{
    if (decoded->has_payload) {
        return handle_io_write(decoded, ipc_data);
    } else {
        return handle_io_read(decoded, ipc_data);
    }
}

static bool handle_config_request(PCIE_RequestDecoded *decoded,
                                  DownstreamPCIeConnection *ipc_data)
{
    IPCChannel *channel = &ipc_data->connection.channel;
    /* config requests upstream are not allowed. */
    if (decoded->has_payload) {
        return send_ipc_pcie_write_failure(channel, decoded->transaction,
                                           root_completer_id(ipc_data));
    } else {
        return send_ipc_pcie_read_failure(channel, decoded->transaction,
                                          root_completer_id(ipc_data));
    }
}

static bool handle_msg_request(PCIE_RequestDecoded *decoded,
                               DownstreamPCIeConnection *ipc_data)
{
    /* TODO */
    return true;
}

static bool handle_request(void *transaction, DownstreamPCIeConnection *ipc_data)
{
    PCIE_RequestDecoded decoded;
    decode_request(transaction, &decoded);

    if (decoded.is_memory) {
        return handle_memory_request(&decoded, ipc_data);
    } else if (decoded.is_io) {
        return handle_io_request(&decoded, ipc_data);
    } else if (decoded.is_config) {
        return handle_config_request(&decoded, ipc_data);
    } else {
        return handle_msg_request(&decoded, ipc_data);
    }
}

static void handle_packet(IPCPacket *packet, IPCConnection *connection)
{
    uint8_t *transaction = ipc_packet_data(packet);
    DownstreamPCIeConnection *ipc_data = (DownstreamPCIeConnection *)connection;
    if (pcie_trans_is_completion(transaction)) {
        handle_completion(transaction, ipc_data);
    } else {
        handle_request(transaction, ipc_data);
        free(packet);
    }
}

static DownstreamPCIeConnection *create_ipc_data(void)
{
    return (DownstreamPCIeConnection *)g_malloc0(sizeof(DownstreamPCIeConnection));
}

static const char *ipc_connection_kind = "pcie_downstream";

static void init_ipc_data(DownstreamPCIeConnection *ipc_data, PCIDevice *pci_dev)
{
    init_ipc_connection(&ipc_data->connection,
                        ipc_connection_kind,
                        ipc_pcie_sizer(),
                        handle_packet);

    ipc_data->pci_bus_num = pci_bus_num(pci_dev->bus);
    ipc_data->dma_as = pci_get_address_space(pci_dev);
    ipc_data->io_as = pci_get_io_address_space(pci_dev);

    activate_ipc_connection(&ipc_data->connection);
}

static DownstreamPCIeConnection *get_ipc_data(const char *socket_path,
                                 bool use_abstract_path,
                                 PCIDevice *pci_dev)
{
    bool error;
    IPCConnection *connection = find_ipc_connection(ipc_connection_kind,
                                                    socket_path,
                                                    use_abstract_path,
                                                    &error);
    DownstreamPCIeConnection *ipc_data = (DownstreamPCIeConnection *)connection;
    if (connection == NULL) {
        if (error) {
            return NULL;
        }
        ipc_data = create_ipc_data();
        if (setup_ipc_channel(&ipc_data->connection.channel,
                              socket_path, use_abstract_path)) {
            init_ipc_data(ipc_data, pci_dev);
            register_ipc_connection(socket_path, use_abstract_path,
                                    &ipc_data->connection);
        } else {
            free(ipc_data);
            return NULL;
        }
    } else if ((ipc_data->pci_bus_num != pci_bus_num(pci_dev->bus)) ||
               (ipc_data->dma_as != pci_get_address_space(pci_dev)) ||
               (ipc_data->io_as != pci_get_io_address_space(pci_dev))) {
        fprintf(stderr, "IPC channel %s already in use.\n", socket_path);
        return NULL;
    }
    return ipc_data;
}

DownstreamPCIeConnection *init_pcie_downstream_ipc(const char *socket_path,
                                      bool use_abstract_path,
                                      PCIDevice *pci_dev)
{
    DownstreamPCIeConnection *ipc_data =
        get_ipc_data(socket_path, use_abstract_path, pci_dev);
    if (ipc_data == NULL) {
        fprintf(stderr, "Failed to open IPC channel %s.\n", socket_path);
    }
    return ipc_data;
}
