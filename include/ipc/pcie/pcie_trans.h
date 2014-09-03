#ifndef QEMU_IPC_PCIE_TRANS_H
#define QEMU_IPC_PCIE_TRANS_H

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

typedef enum PCIE_Transaction_Type {
    PCIE_PAYLOAD_MARK = 0x40,
    PCIE_LONG_HEADER_MARK = 0x20,
    PCIE_MESSAGE_MARK = 0x10,
    PCIE_LOCKED_TRANSACTION_MARK = 0x01,

    PCIE_MEMORY_REQUEST = 0x00,
    PCIE_LOCKED_MEMORY_REQUEST = (PCIE_MEMORY_REQUEST |
                                  PCIE_LOCKED_TRANSACTION_MARK),
    PCIE_IO_REQUEST = 0x02,
    PCIE_CONFIG_TYPE0_REQUEST = 0x04,
    PCIE_CONFIG_TYPE1_REQUEST = (PCIE_CONFIG_TYPE0_REQUEST |
                                 PCIE_LOCKED_TRANSACTION_MARK),
    PCIE_COMPLETION = 0x0a,
    PCIE_LOCKED_COMPLETION = (PCIE_COMPLETION | PCIE_LOCKED_TRANSACTION_MARK),

    PCIE_FETCH_AND_ADD_REQUEST = 0x0c,
    PCIE_SWAP_REQUEST = 0x0d,
    PCIE_COMPARE_AND_SWAP_REQUEST = 0x0e,

    PCIE_MESSAGE_ROUTING_MASK = 0x07,
    PCIE_MESSAGE_MARK_MASK = PCIE_MESSAGE_MARK | 0x08,

    PCIE_REQUEST_TO_IGNORE = 0x03,
} PCIE_Transaction_Type;

typedef enum PCIE_Message_Routing {
    ROUTED_TO_ROOT_COMPLEX = 0x0,
    ROUTED_BY_ADDRESS = 0x1,
    ROUTED_BY_ID = 0x2,
    BROADCAST_FROM_ROOT = 0x3,
    LOCAL_ROUTING = 0x4,
    GATEHERED_AND_ROUTED_TO_ROOT_COMPLEX = 0x5,
    INVALID_MESSAGE_ROUTING = 0x8,
} PCIE_Message_Routing;

typedef enum PCIE_Message_Code {
    VENDOR_DEFINED_MESSAGE_TYPE0 = 0x7e,
    VENDOR_DEFINED_MESSAGE_TYPE1 = 0x7f,
} PCIE_Message_Code;

typedef enum PCIE_Completion_Status {
  SUCCESSFUL_COMPLETION = 0x0,
  UNSUPPORTED_REQUEST = 0x1,
  CONFIGURATION_REQUEST_RETRY = 0x2,
  COMPLETER_ABORT = 0x4,
} PCIE_Completion_Status;

static inline
void pcie_trans_clear(uint8_t *trans_data)
{
    memset(trans_data, 0, 16);
}

static inline
void pcie_trans_set_transaction_type(uint8_t *trans_data,
                                     uint8_t transaction_type)
{
    trans_data[0] =
        (uint8_t)((trans_data[0] &
                   (PCIE_PAYLOAD_MARK | PCIE_LONG_HEADER_MARK)) |
                  (transaction_type &
                   ~(PCIE_PAYLOAD_MARK | PCIE_LONG_HEADER_MARK)));
}

static inline
uint8_t pcie_trans_get_transaction_type(const uint8_t *trans_data)
{
    return (uint8_t)(trans_data[0] &
                     ~(PCIE_PAYLOAD_MARK | PCIE_LONG_HEADER_MARK));
}

static inline
bool pcie_trans_is_completion(const uint8_t *trans_data)
{
    return ((pcie_trans_get_transaction_type(trans_data) &
             ~PCIE_LOCKED_TRANSACTION_MARK) == PCIE_COMPLETION);
}

static inline
bool pcie_trans_is_request_to_ignore(const uint8_t *trans_data)
{
    return
        (pcie_trans_get_transaction_type(trans_data) == PCIE_REQUEST_TO_IGNORE);
}

static inline
bool pcie_trans_is_memory_request(const uint8_t *trans_data)
{
    return ((pcie_trans_get_transaction_type(trans_data) &
             ~PCIE_LOCKED_TRANSACTION_MARK) == PCIE_MEMORY_REQUEST);
}

static inline
bool pcie_trans_is_io_request(const uint8_t *trans_data)
{
    return
        (pcie_trans_get_transaction_type(trans_data) == PCIE_IO_REQUEST);
}

static inline
bool pcie_trans_is_config_request(const uint8_t *trans_data)
{
    return ((pcie_trans_get_transaction_type(trans_data) &
             ~PCIE_LOCKED_TRANSACTION_MARK) == PCIE_CONFIG_TYPE0_REQUEST);
}

static inline
bool pcie_trans_is_message_transaction(const uint8_t *trans_data)
{
    return
        (pcie_trans_get_transaction_type(trans_data) & PCIE_MESSAGE_MARK_MASK)
        == PCIE_MESSAGE_MARK;
}

static inline
bool pcie_trans_has_payload(const uint8_t *trans_data)
{
    return (trans_data[0] & PCIE_PAYLOAD_MARK) != 0;
}

static inline
bool pcie_trans_is_posted_request(const uint8_t *trans_data)
{
    return (pcie_trans_is_message_transaction(trans_data) ||
            (pcie_trans_is_memory_request(trans_data) &&
             pcie_trans_has_payload(trans_data)) ||
            pcie_trans_is_request_to_ignore(trans_data));
}

static inline
bool pcie_trans_is_locked(const uint8_t *trans_data) {
    return ((pcie_trans_get_transaction_type(trans_data) &
             PCIE_LOCKED_TRANSACTION_MARK) == PCIE_LOCKED_TRANSACTION_MARK);
}

static inline
bool pcie_trans_is_type0_config_request(const uint8_t *trans_data) {
    return (pcie_trans_is_config_request(trans_data) &&
            !pcie_trans_is_locked(trans_data));
}

static inline
bool pcie_trans_is_read_request(const uint8_t *trans_data) {
    return (!pcie_trans_has_payload(trans_data) &&
            (pcie_trans_is_memory_request(trans_data) ||
             pcie_trans_is_config_request(trans_data) ||
             pcie_trans_is_io_request(trans_data)));
}

static inline
bool pcie_trans_is_write_request(const uint8_t *trans_data) {
    return (pcie_trans_has_payload(trans_data) &&
            (pcie_trans_is_memory_request(trans_data) ||
             pcie_trans_is_config_request(trans_data) ||
             pcie_trans_is_io_request(trans_data)));
}

static inline
bool pcie_trans_get_long_header_mark(const uint8_t *trans_data)
{
    return (trans_data[0] & PCIE_LONG_HEADER_MARK) != 0;
}

static inline void
pcie_trans_set_long_header_mark(uint8_t *trans_data, bool is64bit)
{
    trans_data[0] = (uint8_t)((trans_data[0] & ~PCIE_LONG_HEADER_MARK) |
                              (is64bit? PCIE_LONG_HEADER_MARK: 0));
}

static inline
void pcie_trans_set_message_routing_type(uint8_t *trans_data,
                                         uint8_t routing_type)
{
    pcie_trans_set_transaction_type(trans_data,
                                    (uint8_t)(PCIE_MESSAGE_MARK |
                                              (PCIE_MESSAGE_ROUTING_MASK & routing_type)));
    pcie_trans_set_long_header_mark(trans_data, true);
}

static inline
uint8_t pcie_trans_get_message_routing_type(const uint8_t *trans_data)
{
    if(pcie_trans_is_message_transaction(trans_data)) {
        return (uint8_t)(pcie_trans_get_transaction_type(trans_data) &
                         PCIE_MESSAGE_ROUTING_MASK);
    } else {
      return INVALID_MESSAGE_ROUTING;
    }
}

static inline
void pcie_trans_set_message_code(uint8_t *trans_data, uint8_t msg_code)
{
    trans_data[7] = msg_code;
}

static inline
uint8_t pcie_trans_get_message_code(const uint8_t *trans_data)
{
    return trans_data[7];
}

static inline
void pcie_trans_set_vendor_defined_message_vendor_id(uint8_t *trans_data,
                                                     uint16_t vendor_id)
{
    trans_data[10] = (uint8_t)(vendor_id & 0xff);
    trans_data[11] = (uint8_t)(vendor_id >> 8);
}

static inline
uint16_t pcie_trans_get_vendor_defined_message_vendor_id(const uint8_t *trans_data)
{
    return (uint16_t)(trans_data[10] |
                      (uint16_t)(trans_data[11] << 8));
}

static inline
void pcie_trans_set_vendor_defined_message_vendor_bytes(uint8_t *trans_data,
                                                        uint8_t byte0,
                                                        uint8_t byte1,
                                                        uint8_t byte2,
                                                        uint8_t byte3)
{
    trans_data[12] = byte0;
    trans_data[13] = byte1;
    trans_data[14] = byte2;
    trans_data[15] = byte3;
}

static inline
void pcie_trans_get_vendor_defined_message_vendor_bytes(const uint8_t *trans_data,
                                                        uint8_t *byte0,
                                                        uint8_t *byte1,
                                                        uint8_t *byte2,
                                                        uint8_t *byte3)
{
    *byte0 = trans_data[12];
    *byte1 = trans_data[13];
    *byte2 = trans_data[14];
    *byte3 = trans_data[15];
}

static inline
void pcie_trans_set_vendor_defined_message_vendor_def(uint8_t *trans_data,
                                                      uint32_t vendor_def)
{
    pcie_trans_set_vendor_defined_message_vendor_bytes(trans_data,
                                                       (uint8_t)(vendor_def & 0xff),
                                                       (uint8_t)(vendor_def >> 8),
                                                       (uint8_t)(vendor_def >> 16),
                                                       (uint8_t)(vendor_def >> 24));
}

static inline
uint32_t pcie_trans_get_vendor_defined_message_vendor_def(const uint8_t *trans_data)
{
    uint8_t byte0, byte1, byte2, byte3;
    pcie_trans_get_vendor_defined_message_vendor_bytes(trans_data,
                                                       &byte0,
                                                       &byte1,
                                                       &byte2,
                                                       &byte3);
    return (uint32_t)(byte0 |
                      (uint32_t)(byte1 << 8) |
                      (uint32_t)(byte2 << 16) |
                      (uint32_t)(byte3 << 24));
}

static inline
void pcie_trans_set_payload_mark(uint8_t *trans_data, bool with_payload)
{
    trans_data[0] = (uint8_t)((trans_data[0] & ~PCIE_PAYLOAD_MARK) |
                              (with_payload? PCIE_PAYLOAD_MARK: 0));
}

static inline
bool pcie_trans_has_fourth_dw(const uint8_t *trans_data)
{
    return pcie_trans_get_long_header_mark(trans_data);
}

static inline
uint32_t pcie_trans_get_header_size(const uint8_t *trans_data)
{
    return (pcie_trans_has_fourth_dw(trans_data)? 4: 3) * 4;
}

static inline
void pcie_trans_set_data_size_in_dw(uint8_t *trans_data,
                                    uint32_t data_size_in_dw)
{
    trans_data[3] = (uint8_t)(data_size_in_dw & 0xFF);
    trans_data[2] = (uint8_t)((trans_data[2] & ~0x03) |
                              ((data_size_in_dw >> 8) & 0x03));
}

static inline
uint16_t pcie_trans_get_data_size_in_dw(const uint8_t *trans_data)
{
    return (uint16_t)(trans_data[3] | ((trans_data[2] & 0x03) << 8));
}

static inline uint16_t
pcie_trans_get_payload_or_response_size_in_dw(const uint8_t *trans_data)
{
    uint16_t result = pcie_trans_get_data_size_in_dw(trans_data);
    if(result == 0) {
      return 1024;
    } else {
      return result;
    }
}

static inline
uint16_t pcie_trans_get_payload_size_in_dw(const uint8_t *trans_data)
{
    if(!pcie_trans_has_payload(trans_data)) {
        return 0;
    }
    return pcie_trans_get_payload_or_response_size_in_dw(trans_data);
}

static inline
uint16_t pcie_trans_get_total_size_in_dw(const uint8_t *trans_data)
{
    return (uint16_t)((pcie_trans_has_fourth_dw(trans_data)? 4: 3) +
                      pcie_trans_get_payload_size_in_dw(trans_data));
}

static inline
uint16_t pcie_trans_get_total_size_in_bytes(const uint8_t *trans_data)
{
    return (uint16_t)(pcie_trans_get_total_size_in_dw(trans_data) * 4);
}

static inline
void pcie_trans_set_tc(uint8_t *trans_data, uint8_t tc)
{
    trans_data[1] = (uint8_t)((trans_data[1] & ~0x70) | ((tc << 4) & 0x70));
}

static inline
uint8_t pcie_trans_get_tc(const uint8_t *trans_data)
{
    return (uint8_t)((trans_data[1] >> 4) & 0x7);
}

static inline
void pcie_trans_set_at(uint8_t *trans_data, uint8_t at)
{
    trans_data[2] = (uint8_t)((trans_data[2] & ~0x0C) | ((at << 2) & 0x0C));
}

static inline
uint8_t pcie_trans_get_at(const uint8_t *trans_data)
{
    return (uint8_t)((trans_data[2] >> 2) & 0x3);
}

static inline
void pcie_trans_set_relaxed_ordering(uint8_t *trans_data,
                                     bool relaxed_ordering)
{
    trans_data[2] = (uint8_t)((trans_data[2] & ~0x20) |
                              ((relaxed_ordering? 1: 0) << 5));
}

static inline
bool pcie_trans_get_relaxed_ordering(const uint8_t *trans_data)
{
    return ((trans_data[2] & 0x20) != 0);
}

static inline
void pcie_trans_set_no_snoop(uint8_t *trans_data, bool no_snoop)
{
    trans_data[2] =
        (uint8_t)((trans_data[2] & ~0x10) | ((no_snoop? 1: 0) << 4));
}

static inline
bool pcie_trans_get_no_snoop(const uint8_t *trans_data)
{
    return ((trans_data[2] & 0x10) != 0);
}

static inline
void pcie_trans_set_id_based_ordering(uint8_t *trans_data,
                                      bool id_based_ordering)
{
    trans_data[1] = (uint8_t)((trans_data[1] & ~0x04) |
                              ((id_based_ordering? 1: 0) << 2));
}

static inline
bool pcie_trans_get_id_based_ordering(const uint8_t *trans_data)
{
    return ((trans_data[1] & 0x04) != 0);
}

static inline
void pcie_trans_set_routing_target_device(uint8_t *trans_data,
                                          uint8_t bus_num,
                                          uint8_t dev_num,
                                          uint8_t func_num)
{
    trans_data[8] = (uint8_t)(bus_num);
    trans_data[9] = (uint8_t)((dev_num << 3) | func_num);
}

static inline
void pcie_trans_get_routing_target_device(const uint8_t *trans_data,
                                          uint8_t *bus_num,
                                          uint8_t *dev_num,
                                          uint8_t *func_num)
{
    *bus_num = trans_data[8];
    *dev_num = (uint8_t)((trans_data[9] >> 3) & 0x1f);
    *func_num = (uint8_t)(trans_data[9] & 0x7);
}

static inline void pcie_trans_set_target_register(uint8_t *trans_data,
                                                  uint16_t reg_num)
{
    trans_data[10] = (uint8_t)((trans_data[10] & ~0x0F) |
                               ((reg_num >> 6) & 0x0F));
    trans_data[11] = (uint8_t)((trans_data[11] & 0x03) |
                               ((reg_num & 0x3F) << 2));
}

static inline
uint16_t pcie_trans_get_target_register(const uint8_t *trans_data)
{
    return (uint16_t)(((trans_data[10] & 0x0F) << 6) |
                      ((trans_data[11] >> 2) & 0x3F));
}

static inline
void pcie_trans_set_addr(uint8_t *trans_data, uint64_t addr)
{
    uint32_t limit = 0;
    limit--;
    if(addr > limit) {
        pcie_trans_set_long_header_mark(trans_data, true);
        trans_data[8] = (uint8_t)(addr >> 56);
        trans_data[9] = (uint8_t)(addr >> 48);
        trans_data[10] = (uint8_t)(addr >> 40);
        trans_data[11] = (uint8_t)(addr >> 32);
        trans_data[12] = (uint8_t)(addr >> 24);
        trans_data[13] = (uint8_t)(addr >> 16);
        trans_data[14] = (uint8_t)(addr >> 8);
        trans_data[15] = (uint8_t)((trans_data[15] & 0x03) | (addr & ~0x03));
    } else {
        pcie_trans_set_long_header_mark(trans_data, false);
        trans_data[8] = (uint8_t)(addr >> 24);
        trans_data[9] = (uint8_t)(addr >> 16);
        trans_data[10] = (uint8_t)(addr >> 8);
        trans_data[11] = (uint8_t)((trans_data[11] & 0x03) | (addr & ~0x03));
    }
}

static inline
uint64_t pcie_trans_get_addr(const uint8_t *trans_data) {
    if(pcie_trans_get_long_header_mark(trans_data)) {
        return
            ((uint64_t)(trans_data[8]) << 56) |
            ((uint64_t)(trans_data[9]) << 48) |
            ((uint64_t)(trans_data[10]) << 40) |
            ((uint64_t)(trans_data[11]) << 32) |
            ((uint64_t)(trans_data[12]) << 24) |
            ((uint64_t)(trans_data[13]) << 16) |
            ((uint64_t)(trans_data[14]) << 8) |
            ((uint64_t)(trans_data[15]) & ~0x03);
    } else {
        return
            ((uint64_t)(trans_data[8]) << 24) |
            ((uint64_t)(trans_data[9]) << 16) |
            ((uint64_t)(trans_data[10]) << 8) |
            ((uint64_t)(trans_data[11]) & ~0x03);
    }
}

static inline
void pcie_trans_set_request_requester_id(uint8_t *trans_data,
                                         uint16_t requester_id)
{
    trans_data[4] = (uint8_t)(requester_id >> 8);
    trans_data[5] = (uint8_t)(requester_id);
}

static inline
uint16_t pcie_trans_get_request_requester_id(const uint8_t *trans_data)
{
    return (uint16_t)(trans_data[5] | (trans_data[4] << 8));
}

static inline
void pcie_trans_set_request_tag(uint8_t *trans_data, uint8_t tag)
{
    trans_data[6] = tag;
}

static inline
uint8_t pcie_trans_get_request_tag(const uint8_t *trans_data)
{
    return trans_data[6];
}

static inline
void pcie_trans_set_completion_requester_id(uint8_t *trans_data,
                                            uint16_t requester_id)
{
    trans_data[9] = (uint8_t)(requester_id);
    trans_data[8] = (uint8_t)(requester_id >> 8);
}

static inline
uint16_t pcie_trans_get_completion_requester_id(const uint8_t *trans_data)
{
    return (uint16_t)(trans_data[9] | (trans_data[8] << 8));
}

static inline
void pcie_trans_set_completion_tag(uint8_t *trans_data, uint8_t tag)
{
    trans_data[10] = tag;
}

static inline
uint8_t pcie_trans_get_completion_tag(const uint8_t *trans_data)
{
    return trans_data[10];
}

static inline void pcie_trans_set_completer_id(uint8_t *trans_data,
                                               uint16_t completer_id)
{
    trans_data[5] = (uint8_t)(completer_id);
    trans_data[4] = (uint8_t)(completer_id >> 8);
}

static inline
uint16_t pcie_trans_get_completer_id(const uint8_t *trans_data)
{
    return (uint16_t)(trans_data[5] | (trans_data[4] << 8));
}

static inline
void pcie_trans_set_completion_status(uint8_t *trans_data,
                                      uint8_t completion_status)
{
    trans_data[6] = (uint8_t)((trans_data[6] & 0x1F) |
                              (completion_status << 5));
}

static inline
uint8_t pcie_trans_get_completion_status(const uint8_t *trans_data)
{
    return (uint8_t)((trans_data[6] >> 5) & 0x7);
}

static inline
void pcie_trans_set_completion_byte_count(uint8_t *trans_data,
                                          uint16_t byte_count)
{
    trans_data[6] = (uint8_t)((trans_data[6] & ~0x0F) |
                              ((byte_count >> 8) & 0x0F));
    trans_data[7] = (uint8_t)(byte_count);
}

static inline
uint16_t pcie_trans_get_completion_byte_count(const uint8_t *trans_data)
{
    return (uint16_t)(trans_data[7] | ((trans_data[6] & 0x0F) << 8));
}

static inline
void pcie_trans_set_byte_enable_bits(uint8_t *trans_data,
                                     const bool *bebits_first_dw,
                                     const bool *bebits_last_dw)
{
    trans_data[7] =
      (uint8_t)((bebits_first_dw[0]? 1: 0) |
                ((bebits_first_dw[1]? 1: 0) << 1) |
                ((bebits_first_dw[2]? 1: 0) << 2) |
                ((bebits_first_dw[3]? 1: 0) << 3) |
                ((bebits_last_dw[0]? 1: 0) << 4) |
                ((bebits_last_dw[1]? 1: 0) << 5) |
                ((bebits_last_dw[2]? 1: 0) << 6) |
                ((bebits_last_dw[3]? 1: 0) << 7));
}

static inline
void pcie_trans_get_byte_enable_bits(const uint8_t *trans_data,
                                     bool *bebits_first_dw,
                                     bool *bebits_last_dw)
{
    bebits_first_dw[0] = ((trans_data[7] & 0x1) != 0);
    bebits_first_dw[1] = (((trans_data[7] >> 1) & 0x1) != 0);
    bebits_first_dw[2] = (((trans_data[7] >> 2) & 0x1) != 0);
    bebits_first_dw[3] = (((trans_data[7] >> 3) & 0x1) != 0);
    bebits_last_dw[0] = (((trans_data[7] >> 4) & 0x1) != 0);
    bebits_last_dw[1] = (((trans_data[7] >> 5) & 0x1) != 0);
    bebits_last_dw[2] = (((trans_data[7] >> 6) & 0x1) != 0);
    bebits_last_dw[3] = (((trans_data[7] >> 7) & 0x1) != 0);
}

static inline
void pcie_trans_encode_completion(uint8_t *completion_data,
                                  uint16_t requester_id,
                                  uint8_t tag,
                                  uint16_t size_in_dw,
                                  bool locked,
                                  bool with_data,
                                  uint16_t completer_id,
                                  uint8_t completion_status)
{
    pcie_trans_clear(completion_data);
    pcie_trans_set_transaction_type(completion_data,
                                    locked?
                                    PCIE_LOCKED_COMPLETION:
                                    PCIE_COMPLETION);
    pcie_trans_set_payload_mark(completion_data, with_data);
    pcie_trans_set_completer_id(completion_data, completer_id);
    pcie_trans_set_completion_status(completion_data, completion_status);
    if (with_data) {
        pcie_trans_set_data_size_in_dw(completion_data, size_in_dw);
        pcie_trans_set_completion_byte_count(completion_data,
                                             size_in_dw * 4);
    }
    pcie_trans_set_completion_requester_id(completion_data, requester_id);
    pcie_trans_set_completion_tag(completion_data, tag);
}

static inline
void pcie_trans_encode_config_read_completion(uint8_t *completion_data,
                                              const uint8_t *request_data,
                                              uint16_t completer_id)
{
    pcie_trans_encode_completion(completion_data,
                                 pcie_trans_get_request_requester_id(request_data),
                                 pcie_trans_get_request_tag(request_data),
                                 pcie_trans_get_data_size_in_dw(request_data),
                                 /* locked = */ false,
                                 /* with_data = */ true,
                                 completer_id,
                                 SUCCESSFUL_COMPLETION);
}

static inline
void pcie_trans_encode_config_read_failure_completion(uint8_t *completion_data,
                                                      const uint8_t *request_data,
                                                      uint16_t completer_id)
{
    pcie_trans_encode_completion(completion_data,
                                 pcie_trans_get_request_requester_id(request_data),
                                 pcie_trans_get_request_tag(request_data),
                                 pcie_trans_get_data_size_in_dw(request_data),
                                 /* locked = */ false,
                                 /* with_data = */ false,
                                 completer_id,
                                 UNSUPPORTED_REQUEST);
}

static inline
void pcie_trans_encode_config_write_completion(uint8_t *completion_data,
                                               const uint8_t *request_data,
                                               uint16_t completer_id)
{
    pcie_trans_encode_completion(completion_data,
                                 pcie_trans_get_request_requester_id(request_data),
                                 pcie_trans_get_request_tag(request_data),
                                 pcie_trans_get_data_size_in_dw(request_data),
                                 /* locked = */ false,
                                 /* with_data = */ false,
                                 completer_id,
                                 SUCCESSFUL_COMPLETION);
}

static inline
void pcie_trans_encode_config_write_failure_completion(uint8_t *completion_data,
                                                       const uint8_t *request_data,
                                                       uint16_t completer_id)
{
    pcie_trans_encode_completion(completion_data,
                                 pcie_trans_get_request_requester_id(request_data),
                                 pcie_trans_get_request_tag(request_data),
                                 pcie_trans_get_data_size_in_dw(request_data),
                                 /* locked = */ false,
                                 /* with_data = */ false,
                                 completer_id,
                                 UNSUPPORTED_REQUEST);
}

static inline
void pcie_trans_encode_read_completion(uint8_t *completion_data,
                                       const uint8_t *request_data,
                                       uint16_t completer_id)
{
    pcie_trans_encode_completion(completion_data,
                                 pcie_trans_get_request_requester_id(request_data),
                                 pcie_trans_get_request_tag(request_data),
                                 pcie_trans_get_data_size_in_dw(request_data),
                                 /* TODO locked = */ false,
                                 /* with_data = */ true,
                                 completer_id,
                                 SUCCESSFUL_COMPLETION);
}

static inline
void pcie_trans_encode_read_failure_completion(uint8_t *completion_data,
                                               const uint8_t *request_data,
                                               uint16_t completer_id)
{
    pcie_trans_encode_completion(completion_data,
                                 pcie_trans_get_request_requester_id(request_data),
                                 pcie_trans_get_request_tag(request_data),
                                 pcie_trans_get_data_size_in_dw(request_data),
                                 /* TODO locked = */ false,
                                 /* with_data = */ false,
                                 completer_id,
                                 UNSUPPORTED_REQUEST);
}

static inline
void pcie_trans_encode_write_completion(uint8_t *completion_data,
                                        const uint8_t *request_data,
                                        uint16_t completer_id)
{
    pcie_trans_encode_completion(completion_data,
                                 pcie_trans_get_request_requester_id(request_data),
                                 pcie_trans_get_request_tag(request_data),
                                 pcie_trans_get_data_size_in_dw(request_data),
                                 /* TODO locked = */ false,
                                 /* with_data = */ false,
                                 completer_id,
                                 SUCCESSFUL_COMPLETION);
}

static inline
void pcie_trans_encode_write_failure_completion(uint8_t *completion_data,
                                                const uint8_t *request_data,
                                                uint16_t completer_id)
{
    pcie_trans_encode_completion(completion_data,
                                 pcie_trans_get_request_requester_id(request_data),
                                 pcie_trans_get_request_tag(request_data),
                                 pcie_trans_get_data_size_in_dw(request_data),
                                 /* TODO locked = */ false,
                                 /* with_data = */ false,
                                 completer_id,
                                 UNSUPPORTED_REQUEST);
}

static inline
bool pcie_trans_is_1_word_memory_trans(uint64_t addr,
                                       uint32_t size)
{
    if (size > 4) {
        return false;
    }
    if ((addr & 3) == 0) {
        return true;
    }
    if (size + (addr & 3) > 4) {
        return false;
    } else {
        return true;
    }
}

static inline
void pcie_trans_compute_disabled_bytes_for_1_word_trans(uint64_t addr,
                                                        unsigned size,
                                                        unsigned *leading_disabled_bytes,
                                                        unsigned *trailing_disabled_bytes)
{
    *leading_disabled_bytes = (unsigned)(addr & 3);
    if (size > 4 - *leading_disabled_bytes) {
        size = 4 - *leading_disabled_bytes;
        *trailing_disabled_bytes = 0;
    } else {
        *trailing_disabled_bytes = 4 - size - *leading_disabled_bytes;
    }
}

static inline
void pcie_trans_compute_disabled_bytes_for_multiword_trans(uint64_t addr,
                                                           unsigned size,
                                                           unsigned *leading_disabled_bytes,
                                                           unsigned *trailing_disabled_bytes)
{
    unsigned last_dword_overflow;
    *leading_disabled_bytes = (unsigned)(addr & 3);
    last_dword_overflow = (size + *leading_disabled_bytes) & 3;
    *trailing_disabled_bytes = last_dword_overflow ? 4 - last_dword_overflow: 0;
}

static inline
void pcie_trans_compute_disabled_bytes(uint64_t addr,
                                       unsigned size,
                                       unsigned *leading_disabled_bytes,
                                       unsigned *trailing_disabled_bytes)
{
    if (pcie_trans_is_1_word_memory_trans(addr, size)) {
        pcie_trans_compute_disabled_bytes_for_1_word_trans(addr,
                                                           size,
                                                           leading_disabled_bytes,
                                                           trailing_disabled_bytes);
    } else {
        pcie_trans_compute_disabled_bytes_for_multiword_trans(addr,
                                                              size,
                                                              leading_disabled_bytes,
                                                              trailing_disabled_bytes);
    }
}

static inline
void pcie_trans_compute_bebits_for_1_word_trans(uint64_t addr,
                                                unsigned size,
                                                unsigned *leading_disabled_bytes,
                                                unsigned *trailing_disabled_bytes,
                                                bool *bebits_first_dw,
                                                bool *bebits_last_dw)
{
    unsigned i, j;

    pcie_trans_compute_disabled_bytes_for_1_word_trans(addr, size,
                                                       leading_disabled_bytes,
                                                       trailing_disabled_bytes);

    for (i = 0; i < *leading_disabled_bytes; ++i) {
        bebits_first_dw[i] = false;
    }
    for (j = 0; j < size; ++j, ++i) {
        bebits_first_dw[i] = true;
    }
    for (; i < 4; ++i) {
        bebits_first_dw[i] = false;
    }
    for (i = 0; i < 4; ++i) {
        bebits_last_dw[i] = false;
    }
}

static inline
void pcie_trans_compute_bebits_for_multiword_trans(uint64_t addr,
                                                   unsigned size,
                                                   unsigned *leading_disabled_bytes,
                                                   unsigned *trailing_disabled_bytes,
                                                   bool *bebits_first_dw,
                                                   bool *bebits_last_dw)
{
    unsigned i;

    pcie_trans_compute_disabled_bytes_for_multiword_trans(addr, size,
                                                          leading_disabled_bytes,
                                                          trailing_disabled_bytes);

    for (i = 0; i < *leading_disabled_bytes; ++i) {
        bebits_first_dw[i] = false;
    }
    for (; i < 4; ++i) {
        bebits_first_dw[i] = true;
    }
    for (i = 0; i < 4 - *trailing_disabled_bytes; ++i) {
        bebits_last_dw[i] = true;
    }
    for (; i < 4; ++i) {
        bebits_last_dw[i] = false;
    }
}

static inline
void pcie_trans_compute_bebits(uint64_t addr,
                               unsigned size,
                               unsigned *leading_disabled_bytes,
                               unsigned *trailing_disabled_bytes,
                               bool *bebits_first_dw,
                               bool *bebits_last_dw)
{
    if (pcie_trans_is_1_word_memory_trans(addr, size)) {
        pcie_trans_compute_bebits_for_1_word_trans(addr,
                                                   size,
                                                   leading_disabled_bytes,
                                                   trailing_disabled_bytes,
                                                   bebits_first_dw,
                                                   bebits_last_dw);
    } else {
        pcie_trans_compute_bebits_for_multiword_trans(addr,
                                                      size,
                                                      leading_disabled_bytes,
                                                      trailing_disabled_bytes,
                                                      bebits_first_dw,
                                                      bebits_last_dw);
    }
}

static inline
void pcie_trans_encode_addressed_request(uint8_t trans_data[16],
                                         uint8_t transaction_type,
                                         bool with_data,
                                         uint16_t requester_id,
                                         uint8_t tag,
                                         uint64_t addr,
                                         uint32_t size_in_dw,
                                         bool *bebits_first_dw,
                                         bool *bebits_last_dw)
{
    pcie_trans_clear(trans_data);
    pcie_trans_set_transaction_type(trans_data, transaction_type);
    pcie_trans_set_payload_mark(trans_data, with_data);
    pcie_trans_set_addr(trans_data, addr);
    pcie_trans_set_byte_enable_bits(trans_data,
                                    bebits_first_dw, bebits_last_dw);
    pcie_trans_set_data_size_in_dw(trans_data, size_in_dw);
    pcie_trans_set_request_requester_id(trans_data, requester_id);
    pcie_trans_set_request_tag(trans_data, tag);
}

static inline
void pcie_trans_encode_memory_request(uint8_t trans_data[16],
                                      bool with_data,
                                      uint16_t requester_id,
                                      uint8_t tag,
                                      uint64_t addr,
                                      uint32_t size,
                                      unsigned *leading_disabled_bytes,
                                      unsigned *trailing_disabled_bytes)
{
    uint32_t size_in_dw;
    bool bebits_first_dw[4];
    bool bebits_last_dw[4];

    pcie_trans_compute_bebits(addr,
                              size,
                              leading_disabled_bytes,
                              trailing_disabled_bytes,
                              bebits_first_dw,
                              bebits_last_dw);
    size_in_dw =
        (size + *leading_disabled_bytes + *trailing_disabled_bytes) / 4;

    pcie_trans_encode_addressed_request(trans_data,
                                        PCIE_MEMORY_REQUEST,
                                        with_data,
                                        requester_id,
                                        tag,
                                        addr,
                                        size_in_dw,
                                        bebits_first_dw,
                                        bebits_last_dw);
}

static inline
void pcie_trans_encode_io_request(uint8_t trans_data[16],
                                  bool with_data,
                                  uint16_t requester_id,
                                  uint8_t tag,
                                  uint32_t addr,
                                  uint32_t size,
                                  unsigned *leading_disabled_bytes,
                                  unsigned *trailing_disabled_bytes)
{
    bool bebits_first_dw[4];
    bool bebits_last_dw[4];

    pcie_trans_compute_bebits_for_1_word_trans(addr,
                                               size,
                                               leading_disabled_bytes,
                                               trailing_disabled_bytes,
                                               bebits_first_dw,
                                               bebits_last_dw);

    pcie_trans_encode_addressed_request(trans_data,
                                        PCIE_IO_REQUEST,
                                        with_data,
                                        requester_id,
                                        tag,
                                        addr,
                                        1,
                                        bebits_first_dw,
                                        bebits_last_dw);
}

static inline
void pcie_trans_encode_msg_request_base(uint8_t trans_data[16],
                                        uint8_t message_routing_type,
                                        uint8_t message_code,
                                        bool with_data,
                                        uint16_t requester_id,
                                        uint8_t tag,
                                        uint32_t size)
{
    pcie_trans_clear(trans_data);
    pcie_trans_set_message_routing_type(trans_data, message_routing_type);
    pcie_trans_set_message_code(trans_data, message_code);
    pcie_trans_set_payload_mark(trans_data, with_data);
    pcie_trans_set_request_requester_id(trans_data, requester_id);
    pcie_trans_set_request_tag(trans_data, tag);
    pcie_trans_set_data_size_in_dw(trans_data, (size + 3) / 4);
}

static inline
void pcie_trans_encode_msg_routed_by_id(uint8_t trans_data[16],
                                        uint8_t message_routing_type,
                                        uint8_t message_code,
                                        bool with_data,
                                        uint16_t requester_id,
                                        uint8_t tag,
                                        uint8_t bus_num,
                                        uint8_t dev_num,
                                        uint8_t func_num,
                                        uint32_t size)
{
    pcie_trans_encode_msg_request_base(trans_data,
                                       ROUTED_BY_ID,
                                       message_code,
                                       with_data,
                                       requester_id,
                                       tag,
                                       size);
    pcie_trans_set_routing_target_device(trans_data,
                                         bus_num, dev_num, func_num);
}

static inline
void pcie_trans_encode_msg_routed_by_address(uint8_t trans_data[16],
                                             uint8_t message_routing_type,
                                             uint8_t message_code,
                                             bool with_data,
                                             uint16_t requester_id,
                                             uint8_t tag,
                                             uint64_t addr,
                                             uint32_t size)
{
    pcie_trans_encode_msg_request_base(trans_data,
                                       ROUTED_BY_ADDRESS,
                                       message_code,
                                       with_data,
                                       requester_id,
                                       tag,
                                       size);
    pcie_trans_set_addr(trans_data, addr);
}

static inline
void pcie_trans_encode_config_request(uint8_t trans_data[16],
                                      bool with_data,
                                      bool is_type1,
                                      uint16_t requester_id,
                                      uint8_t tag,
                                      uint8_t bus_num,
                                      uint8_t dev_num,
                                      uint8_t func_num,
                                      uint16_t reg_num,
                                      uint32_t size,
                                      unsigned *leading_disabled_bytes,
                                      unsigned *trailing_disabled_bytes)
{
    bool bebits_first_dw[4];
    bool bebits_last_dw[4];

    pcie_trans_compute_bebits_for_1_word_trans(reg_num,
                                               size,
                                               leading_disabled_bytes,
                                               trailing_disabled_bytes,
                                               bebits_first_dw,
                                               bebits_last_dw);
    pcie_trans_clear(trans_data);
    pcie_trans_set_transaction_type(trans_data,
                                    is_type1 ?
                                    PCIE_CONFIG_TYPE1_REQUEST :
                                    PCIE_CONFIG_TYPE0_REQUEST);
    pcie_trans_set_payload_mark(trans_data, with_data);
    pcie_trans_set_request_requester_id(trans_data, requester_id);
    pcie_trans_set_request_tag(trans_data, tag);
    pcie_trans_set_routing_target_device(trans_data,
                                         bus_num, dev_num, func_num);
    pcie_trans_set_target_register(trans_data, reg_num);
    pcie_trans_set_data_size_in_dw(trans_data, 1);
    pcie_trans_set_byte_enable_bits(trans_data, bebits_first_dw, bebits_last_dw);
}

static inline
void pcie_trans_encode_special_msg(uint8_t trans_data[16],
                                   uint16_t requester_id,
                                   uint8_t tag,
                                   uint8_t bus_num,
                                   uint8_t dev_num,
                                   uint8_t func_num,
                                   uint16_t external_device_id) {
    pcie_trans_encode_msg_request_base(trans_data,
                                       LOCAL_ROUTING,
                                       /* message_code = */ VENDOR_DEFINED_MESSAGE_TYPE0,
                                       /* with_data = */ false,
                                       requester_id,
                                       tag,
                                       /* size = */ 0);
    
    pcie_trans_set_vendor_defined_message_vendor_id(trans_data, /* vendor_id = */ 0);
    pcie_trans_set_vendor_defined_message_vendor_bytes(trans_data, 
                                                       (uint8_t)bus_num,
                                                       (uint8_t)((dev_num << 3) | func_num),
                                                       (uint8_t)(external_device_id & 0xFF),
                                                       (uint8_t)(external_device_id >> 8));
}

static inline
void pcie_trans_encode_memory_read_request(uint8_t trans_data[16],
                                           uint16_t requester_id,
                                           uint8_t tag,
                                           uint64_t addr,
                                           uint32_t size)
{
    unsigned leading_disabled_bytes;
    unsigned trailing_disabled_bytes;
    pcie_trans_encode_memory_request(trans_data,
                                     /* with_data = */ false,
                                     requester_id,
                                     tag,
                                     addr,
                                     size,
                                     &leading_disabled_bytes,
                                     &trailing_disabled_bytes);
}
                                            
static inline
void pcie_trans_encode_memory_write_request(uint8_t trans_data[16],
                                            uint16_t requester_id,
                                            uint8_t tag,
                                            uint64_t addr,
                                            uint32_t size,
                                            unsigned *leading_disabled_bytes,
                                            unsigned *trailing_disabled_bytes)
{
    pcie_trans_encode_memory_request(trans_data,
                                     /* with_data = */ true,
                                     requester_id,
                                     tag,
                                     addr,
                                     size,
                                     leading_disabled_bytes,
                                     trailing_disabled_bytes);
}
                                            
static inline
void pcie_trans_encode_io_read_request(uint8_t trans_data[16],
                                       uint16_t requester_id,
                                       uint8_t tag,
                                       uint32_t addr,
                                       uint32_t size)
{
    unsigned leading_disabled_bytes;
    unsigned trailing_disabled_bytes;
    pcie_trans_encode_io_request(trans_data,
                                 /* with_data = */ false,
                                 requester_id,
                                 tag,
                                 addr,
                                 size,
                                 &leading_disabled_bytes,
                                 &trailing_disabled_bytes);
}
                                            
static inline
void pcie_trans_encode_io_write_request(uint8_t trans_data[16],
                                        uint16_t requester_id,
                                        uint8_t tag,
                                        uint32_t addr,
                                        uint32_t size,
                                        unsigned *leading_disabled_bytes,
                                        unsigned *trailing_disabled_bytes)
{
    pcie_trans_encode_io_request(trans_data,
                                 /* with_data = */ true,
                                 requester_id,
                                 tag,
                                 addr,
                                 size,
                                 leading_disabled_bytes,
                                 trailing_disabled_bytes);
}
                                            
static inline
void pcie_trans_encode_config_read_request(uint8_t trans_data[16],
                                           bool is_type1,
                                           uint16_t requester_id,
                                           uint8_t tag,
                                           uint8_t bus_num,
                                           uint8_t dev_num,
                                           uint8_t func_num,
                                           uint16_t reg_num,
                                           uint32_t size)
{
    unsigned leading_disabled_bytes;
    unsigned trailing_disabled_bytes;
    pcie_trans_encode_config_request(trans_data,
                                     /* with_data = */ false,
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
}

static inline
void pcie_trans_encode_config_write_request(uint8_t trans_data[16],
                                            bool is_type1,
                                            uint16_t requester_id,
                                            uint8_t tag,
                                            uint8_t bus_num,
                                            uint8_t dev_num,
                                            uint8_t func_num,
                                            uint16_t reg_num,
                                            uint32_t size,
                                            unsigned *leading_disabled_bytes,
                                            unsigned *trailing_disabled_bytes)
{
    pcie_trans_encode_config_request(trans_data,
                                     /* with_data = */ true,
                                     is_type1,
                                     requester_id,
                                     tag,
                                     bus_num,
                                     dev_num,
                                     func_num,
                                     reg_num,
                                     size,
                                     leading_disabled_bytes,
                                     trailing_disabled_bytes);
}

#endif
