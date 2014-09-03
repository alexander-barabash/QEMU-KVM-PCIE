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
                           uint16_t completer_id)
{
    uint8_t completion_data[16];
    pcie_trans_encode_read_failure_completion(completion_data,
                                              request,
                                              completer_id);                              
    return send_ipc_pcie_transaction_header(channel, completion_data);
}
                           
static inline bool
send_ipc_pcie_write_failure(IPCChannel *channel,
                            const uint8_t *request,
                            uint16_t completer_id)
{
    uint8_t completion_data[16];
    pcie_trans_encode_write_failure_completion(completion_data,
                                               request,
                                               completer_id);                              
    return send_ipc_pcie_transaction_header(channel, completion_data);
}
                           
static inline bool
send_ipc_pcie_write_completion(IPCChannel *channel,
                               const uint8_t *request,
                               uint16_t completer_id)
{
    uint8_t completion_data[16];
    pcie_trans_encode_write_completion(completion_data,
                                       request,
                                       completer_id);                              
    return send_ipc_pcie_transaction_header(channel, completion_data);
}
                           
static inline bool
send_ipc_pcie_read_completion(IPCChannel *channel,
                              const uint8_t *request,
                              uint16_t completer_id,
                              const uint8_t *result_data,
                              uint16_t result_size,
                              unsigned result_shift)
{
    uint8_t completion_data[16];
    uint8_t padding[4];
    unsigned copy_to_padding;
    pcie_trans_encode_read_completion(completion_data,
                                      request,
                                      completer_id);
    if (!send_ipc_pcie_transaction_header(channel, completion_data)) {
        return false;
    }
    if (result_shift > 0) {
        memset(padding, 0, 4);
        if (result_shift + result_size < 4) {
            memcpy(padding + result_shift, result_data, result_size);
            result_size = 0;
        } else {
            copy_to_padding = 4 - result_shift;
            memcpy(padding + result_shift, result_data, copy_to_padding);
            result_data += copy_to_padding;
            result_shift -= copy_to_padding;
        }
        if (!write_ipc_channel_data(channel, padding, 4)) {
            return false;
        }
    }
    copy_to_padding = result_size & 3;
    result_size -= copy_to_padding;
    if (result_size > 0) {
        if (!write_ipc_channel_data(channel, result_data, result_size)) {
            return false;
        }
        result_data += result_size;
    }
    if (copy_to_padding > 0) {
        memset(padding, 0, 4);
        memcpy(padding, result_data, copy_to_padding);
        return write_ipc_channel_data(channel, padding, 4);
    } else {
        return true;
    }
}
                           
static inline bool
send_ipc_pcie_memory_read_request(IPCChannel *channel,
                                  uint16_t requester_id,
                                  uint8_t tag,
                                  uint64_t addr,
                                  uint32_t size)
{
    uint8_t trans_data[16];
    pcie_trans_encode_memory_read_request(trans_data,
                                          requester_id,
                                          tag,
                                          addr,
                                          size);
    return send_ipc_pcie_transaction_header(channel, trans_data);
}

static inline bool
send_ipc_pcie_io_read_request(IPCChannel *channel,
                              uint16_t requester_id,
                              uint8_t tag,
                              uint32_t addr,
                              uint32_t size)
{
    uint8_t trans_data[16];
    pcie_trans_encode_io_read_request(trans_data,
                                      requester_id,
                                      tag,
                                      addr,
                                      size);
    return send_ipc_pcie_transaction_header(channel, trans_data);
}

static inline bool
send_ipc_pcie_memory_write_request(IPCChannel *channel,
                                   uint16_t requester_id,
                                   uint8_t tag,
                                   uint64_t addr,
                                   uint32_t size,
                                   const uint8_t *data)
{
    uint8_t trans_data[16];
    unsigned leading_disabled_bytes;
    unsigned trailing_disabled_bytes;
    uint8_t padding[4];
    pcie_trans_encode_memory_write_request(trans_data,
                                           requester_id,
                                           tag,
                                           addr,
                                           size,
                                           &leading_disabled_bytes,
                                           &trailing_disabled_bytes);
    if (!send_ipc_pcie_transaction_header(channel, trans_data)) {
        return false;
    }
    if (pcie_trans_is_1_word_memory_trans(addr, size)) {
        if ((leading_disabled_bytes == 0) && (trailing_disabled_bytes == 0)) {
            return write_ipc_channel_data(channel, data, 4);
        } else {
            memset(padding, 0, 4);
            memcpy(padding + leading_disabled_bytes, data, size);
            return write_ipc_channel_data(channel, padding, 4);
        }
    } else {
        if (leading_disabled_bytes > 0) {
            unsigned bytes_in_first_dword = 4 - leading_disabled_bytes;
            memset(padding, 0, 4);
            memcpy(padding + leading_disabled_bytes, data,
                   bytes_in_first_dword);
            data += bytes_in_first_dword;
            size -= bytes_in_first_dword;
            if (!write_ipc_channel_data(channel, padding, 4)) {
                return false;
            }
        }
        if (trailing_disabled_bytes == 0) {
            return write_ipc_channel_data(channel, data, size);
        } else {
            if (size > 4) {
                unsigned middle_bytes = size - 4 + trailing_disabled_bytes;
                if (!write_ipc_channel_data(channel, data, middle_bytes)) {
                    return false;
                }
                data += middle_bytes;
                size -= middle_bytes;
            }
            memset(padding, 0, 4);
            memcpy(padding, data, size);
            return write_ipc_channel_data(channel, padding, 4);
        }
    }
}

static inline bool
send_ipc_pcie_io_write_request(IPCChannel *channel,
                               uint16_t requester_id,
                               uint8_t tag,
                               uint32_t addr,
                               uint32_t size,
                               const uint8_t *data)
{
    uint8_t trans_data[16];
    unsigned leading_disabled_bytes;
    unsigned trailing_disabled_bytes;
    pcie_trans_encode_io_write_request(trans_data,
                                       requester_id,
                                       tag,
                                       addr,
                                       size,
                                       &leading_disabled_bytes,
                                       &trailing_disabled_bytes);
    if (!send_ipc_pcie_transaction_header(channel, trans_data)) {
        return false;
    }
    if ((leading_disabled_bytes == 0) && (trailing_disabled_bytes == 0)) {
        return write_ipc_channel_data(channel, data, 4);
    } else {
        uint8_t padding[4];
        memset(padding, 0, 4);
        memcpy(padding + leading_disabled_bytes, data, size);
        return write_ipc_channel_data(channel, padding, 4);
    }
}

static inline bool
send_ipc_pcie_config_read_request(IPCChannel *channel,
                                  bool is_type1,
                                  uint16_t requester_id,
                                  uint8_t tag,
                                  uint8_t bus_num,
                                  uint8_t dev_num,
                                  uint8_t func_num,
                                  uint16_t reg_num,
                                  uint32_t size)
{
    uint8_t trans_data[16];
    pcie_trans_encode_config_read_request(trans_data,
                                          is_type1,
                                          requester_id,
                                          tag,
                                          bus_num,
                                          dev_num,
                                          func_num,
                                          reg_num,
                                          size);
    return send_ipc_pcie_transaction_header(channel, trans_data);
}

static inline bool
send_ipc_pcie_config_write_request(IPCChannel *channel,
                                   bool is_type1,
                                   uint16_t requester_id,
                                   uint8_t tag,
                                   uint8_t bus_num,
                                   uint8_t dev_num,
                                   uint8_t func_num,
                                   uint16_t reg_num,
                                   uint32_t size,
                                   const uint8_t *data)
{
    uint8_t trans_data[16];
    unsigned leading_disabled_bytes;
    unsigned trailing_disabled_bytes;
    pcie_trans_encode_config_write_request(trans_data,
                                           is_type1,
                                           requester_id,
                                           tag,
                                           bus_num,
                                           dev_num,
                                           func_num,
                                           reg_num,
                                           size,
                                           &leading_disabled_bytes,
                                           &trailing_disabled_bytes);

    if (!send_ipc_pcie_transaction_header(channel, trans_data)) {
        return false;
    }
    if ((leading_disabled_bytes == 0) && (trailing_disabled_bytes == 0)) {
        return write_ipc_channel_data(channel, data, 4);
    } else {
        uint8_t padding[4];
        memset(padding, 0, 4);
        memcpy(padding + leading_disabled_bytes, data, size);
        return write_ipc_channel_data(channel, padding, 4);
    }
}

static inline bool
send_ipc_pcie_special_msg(IPCChannel *channel,
                          uint16_t requester_id,
                          uint8_t tag,
                          uint8_t bus_num,
                          uint8_t dev_num,
                          uint8_t func_num,
                          uint16_t external_device_id)
{
    uint8_t trans_data[16];
    pcie_trans_encode_special_msg(trans_data,
                                  requester_id,
                                  tag,
                                  bus_num,
                                  dev_num,
                                  func_num,
                                  external_device_id);
    return send_ipc_pcie_transaction_header(channel, trans_data);
}


#endif /* QEMU_PCIE_TRANS_ENCODE_H */
