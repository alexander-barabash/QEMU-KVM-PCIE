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
#include "ipc/pcie/ipc_pcie_sizer.h"
#include "ipc/pcie/pcie_trans.h"

static const void *get_transaction(const void *ipc_header)
{
    return ipc_header + sizeof(uint64_t);
}

static unsigned pcie_get_packet_size(const void *ipc_header)
{
    return sizeof(uint64_t) +
        pcie_trans_get_total_size_in_bytes(get_transaction(ipc_header));
}

static bool pcie_is_packet_completion(const void *ipc_packet)
{
    return pcie_trans_is_completion(get_transaction(ipc_packet));
}

static bool pcie_does_packet_require_completion(const void *ipc_packet)
{
    const void *transaction = get_transaction(ipc_packet);
    return (!pcie_trans_is_completion(transaction) &&
            !pcie_trans_is_posted_request(transaction));
}

IPCSizer *ipc_pcie_sizer(void)
{
    static IPCSizer pcie_sizer = {
        .ipc_header_size = sizeof(uint64_t) + 12,
        .get_packet_size = pcie_get_packet_size,
        .is_packet_completion = pcie_is_packet_completion,
        .does_packet_require_completion = pcie_does_packet_require_completion,
    };
    return &pcie_sizer;
}
