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

#include "ipc/ipc_connection.h"
#include "hw/pci/pci.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct DownstreamPCIeConnection DownstreamPCIeConnection;

struct DownstreamPCIeConnection {
    IPCConnection connection;
    uint32_t pci_bus_num;
    AddressSpace *dma_as;
    AddressSpace *io_as;
    GHashTable *requesters_table;
};

DownstreamPCIeConnection *init_pcie_downstream_ipc(const char *socket_path,
                                                   bool use_abstract_path,
                                                   PCIDevice *pci_dev);

void write_downstream_pcie_memory(DownstreamPCIeConnection *connection,
                                  PCIDevice *pci_dev,
                                  hwaddr addr, uint64_t val, unsigned size);

uint64_t read_downstream_pcie_memory(DownstreamPCIeConnection *connection,
                                     PCIDevice *pci_dev,
                                     hwaddr addr, unsigned size);

void write_downstream_pcie_io(DownstreamPCIeConnection *connection,
                              PCIDevice *pci_dev,
                              hwaddr addr, uint32_t val, unsigned size);

uint32_t read_downstream_pcie_io(DownstreamPCIeConnection *connection,
                                 PCIDevice *pci_dev,
                                 hwaddr addr, unsigned size);

void write_downstream_pcie_config(DownstreamPCIeConnection *connection,
                                  PCIDevice *pci_dev,
                                  uint32_t addr, uint32_t val, unsigned size);

uint32_t read_downstream_pcie_config(DownstreamPCIeConnection *connection,
                                     PCIDevice *pci_dev,
                                     uint32_t addr, unsigned size);

void send_special_downstream_pcie_msg(DownstreamPCIeConnection *connection,
                                      PCIDevice *pci_dev,
                                      uint16_t external_device_id);
