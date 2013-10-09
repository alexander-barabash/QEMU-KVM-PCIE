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

#ifndef QEMU_PCIE_TRANS_DECODED_H
#define QEMU_PCIE_TRANS_DECODED_H

#include "ipc/pcie/pcie_trans.h"

typedef struct PCIE_RequestDecoded {
    const uint8_t *transaction;
    unsigned header_size;
    const uint8_t *payload_data;
    uint16_t size_in_dw;
    const uint8_t *actual_payload;
    uint16_t actual_size;

    bool is_memory;
    bool is_io;
    bool is_config;
    bool is_type0;
    bool is_msg;
    bool has_payload;

    uint64_t addr;
    bool bebits_first_dw[4];
    bool bebits_last_dw[4];
} PCIE_RequestDecoded;

typedef struct PCIE_CompletionDecoded {
    const uint8_t *transaction;
    unsigned header_size;
    const uint8_t *payload_data;
    uint16_t size_in_dw;

    uint16_t requester_id;
    uint8_t tag;
    uint8_t status;
    uint16_t byte_count;
    bool has_payload;

    bool bebits_first_dw[4];
    bool bebits_last_dw[4];
} PCIE_CompletionDecoded;

static inline void decode_request(const uint8_t *transaction,
                                  PCIE_RequestDecoded *decoded) {
    int i, j;

    decoded->transaction = transaction;
    decoded->header_size = pcie_trans_has_fourth_dw(transaction)? 16: 12;
    decoded->payload_data = transaction + decoded->header_size;
    decoded->size_in_dw = pcie_trans_get_data_size_in_dw(transaction);
    decoded->has_payload = pcie_trans_has_payload(transaction);

    decoded->is_io = pcie_trans_is_io_request(transaction);
    decoded->is_config = pcie_trans_is_config_request(transaction);
    decoded->is_type0 = pcie_trans_is_type0_config_request(transaction);
    decoded->is_msg = pcie_trans_is_message_transaction(transaction);
    decoded->is_memory = pcie_trans_is_memory_request(transaction);

    if (decoded->is_memory || decoded->is_io) {
        decoded->addr = pcie_trans_get_addr(transaction);
    } else if (decoded->is_config) {
        decoded->addr = pcie_trans_get_target_register(transaction);
    }

    if (!decoded->is_msg) {
        if (decoded->size_in_dw == 0) {
            decoded->actual_size = 0;
        } else {
            pcie_trans_get_byte_enable_bits(transaction,
                                            decoded->bebits_first_dw,
                                            decoded->bebits_last_dw);
            for (i = 0; i < 4; ++i) {
                if (decoded->bebits_first_dw[i]) {
                    break;
                }
            }
            if (i < 4) {
                if (decoded->size_in_dw == 1) {
                    for (j = 3; j >= 0; --j) {
                        if (decoded->bebits_first_dw[j]) {
                            break;
                        }
                    }
                } else {
                    for (j = 3; j >= 0; --j) {
                        if (decoded->bebits_last_dw[j]) {
                            break;
                        }
                    }
                }
                decoded->addr += i;
                decoded->actual_size =
                    (decoded->size_in_dw - 1) * 4 + j - i + 1;
                decoded->actual_payload = decoded->payload_data + i;
            } else {
                decoded->actual_size = 0;
            }
        }
    }
}

static inline uint32_t
get_completion_transaction_id(PCIE_CompletionDecoded *decoded)
{
    uint32_t transaction_id = decoded->requester_id;
    transaction_id <<= 8;
    transaction_id |= decoded->tag;
    return transaction_id;
}

static inline void decode_completion(const uint8_t *transaction,
                                     PCIE_CompletionDecoded *decoded) {
    decoded->transaction = transaction;
    decoded->header_size = pcie_trans_has_fourth_dw(transaction)? 16: 12;
    decoded->payload_data = transaction + decoded->header_size;
    decoded->size_in_dw = pcie_trans_get_data_size_in_dw(transaction);
    decoded->has_payload = pcie_trans_has_payload(transaction);

    decoded->requester_id = pcie_trans_get_completion_requester_id(transaction);
    decoded->tag = pcie_trans_get_completion_tag(transaction);
    decoded->status = pcie_trans_get_completion_status(transaction);
    decoded->byte_count = pcie_trans_get_completion_byte_count(transaction);

    pcie_trans_get_byte_enable_bits(transaction,
                                    decoded->bebits_first_dw,
                                    decoded->bebits_last_dw);
}



#endif /* QEMU_PCIE_TRANS_DECODED_H */
