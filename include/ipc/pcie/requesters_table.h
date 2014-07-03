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

#include "hw/pci/pci.h"
#include "ipc/ipc_connection.h"
#include <glib.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct PCIeRequest {
    void *transaction;
    bool ready;
    bool waiting;
} PCIeRequest;

typedef struct PCIePendingRequests {
    PCIeRequest requests[256];
    uint8_t current_tag;
} PCIePendingRequests;

static inline uint16_t
pcie_requester_id(PCIDevice *pci_dev)
{
    return (pci_bus_num(pci_dev->bus) << 8) | (pci_dev->devfn & 0xFF);
}

GHashTable *create_pcie_requesters_table(void);
bool register_pcie_request(GHashTable *requesters_table,
                           uint16_t requester_id,
                           PCIeRequest **pointer_to_pending,
                           uint8_t *tag);

PCIeRequest *find_pcie_request(GHashTable *requesters_table,
                               uint16_t requester_id,
                               uint8_t tag);

static inline void pcie_request_ready(PCIeRequest *request,
                                      void *transaction) {
    request->transaction = transaction;
    request->ready = true;
}

static inline void pcie_request_done(PCIeRequest *request) {
    if (request->transaction) {
        free_data_ipc_packet(request->transaction);
        request->transaction = NULL;
    }
    request->waiting = false;
}

void wait_on_pcie_request(IPCConnection *connection, PCIeRequest *request);
