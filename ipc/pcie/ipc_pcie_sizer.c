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

static unsigned pcie_get_packet_size(const void *ipc_header)
{
    return pcie_trans_get_total_size_in_bytes(ipc_header);
}

static bool pcie_is_packet_completion(const void *ipc_packet)
{
    return pcie_trans_is_completion(ipc_packet);
}

static bool pcie_does_packet_require_completion(const void *ipc_packet)
{
    return (!pcie_is_packet_completion(ipc_packet) &&
            !pcie_trans_is_posted_request(ipc_packet));
}

IPCSizer *ipc_pcie_sizer(void)
{
    static IPCSizer pcie_sizer = {
        .ipc_header_size = 12,
        .get_packet_size = pcie_get_packet_size,
        .is_packet_completion = pcie_is_packet_completion,
        .does_packet_require_completion = pcie_does_packet_require_completion,
    };
    return &pcie_sizer;
}
