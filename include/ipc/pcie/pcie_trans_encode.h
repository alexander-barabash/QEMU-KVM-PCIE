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

#ifndef QEMU_PCIE_TRANS_ENCODE_H
#define QEMU_PCIE_TRANS_ENCODE_H

#include "ipc/pcie/pcie_trans.h"
#include "ipc/ipc_channel.h"

static inline bool
send_ipc_pcie_transaction_header(IPCChannel *channel,
                                 const uint8_t *trans_data)
{
    return write_ipc_channel_data(channel, trans_data,
                                  pcie_trans_get_header_size(trans_data));
}

static inline bool
send_ipc_pcie_read_failure(IPCChannel *channel,
                           const uint8_t *request,
                           uint8_t completer_id)
{
    uint8_t completion_data[16];
    pcie_trans_encode_read_failure_completion(request,
                                              completer_id,
                                              completion_data);                              
    return send_ipc_pcie_transaction_header(channel, completion_data);
}
                           
static inline bool
send_ipc_pcie_write_failure(IPCChannel *channel,
                            const uint8_t *request,
                            uint8_t completer_id)
{
    uint8_t completion_data[16];
    pcie_trans_encode_write_failure_completion(request,
                                               completer_id,
                                               completion_data);                              
    return send_ipc_pcie_transaction_header(channel, completion_data);
}
                           
static inline bool
send_ipc_pcie_write_completion(IPCChannel *channel,
                               const uint8_t *request,
                               uint8_t completer_id)
{
    uint8_t completion_data[16];
    pcie_trans_encode_write_completion(request,
                                       completer_id,
                                       completion_data);                              
    return send_ipc_pcie_transaction_header(channel, completion_data);
}
                           
static inline bool
send_ipc_pcie_read_completion(IPCChannel *channel,
                              const uint8_t *request,
                              uint8_t completer_id,
                              const uint8_t *result_data,
                              uint16_t result_size)
{
    uint8_t completion_data[16];
    pcie_trans_encode_read_completion(request,
                                      completer_id,
                                      completion_data);
    return
        send_ipc_pcie_transaction_header(channel, completion_data) &&
        write_ipc_channel_data(channel, result_data, result_size);
}
                           

#endif /* QEMU_PCIE_TRANS_ENCODE_H */
