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
#include "ipc/pcie/requesters_table.h"
#include <linux/kvm.h>

#define EXTERNAL_PCI_DEBUG
#ifdef EXTERNAL_PCI_DEBUG
enum {
    DEBUG_GENERAL, DEBUG_REQUESTS,
};
#define DBGBIT(x)	(1<<DEBUG_##x)
static int debugflags = DBGBIT(GENERAL);

#define	DBGOUT(what, fmt, ...) do { \
    if (debugflags & DBGBIT(what)) \
        fprintf(stderr, "requesters_table: " fmt "\n", ## __VA_ARGS__); \
    } while (0)
#else
#define	DBGOUT(what, fmt, ...) do {} while (0)
#endif

GHashTable *create_pcie_requesters_table(void)
{
    return g_hash_table_new(g_direct_hash, g_direct_equal);
}

static inline PCIePendingRequests *
find_pcie_pending_requests(GHashTable *requesters_table, uint16_t requester_id)
{
    gpointer key = GUINT_TO_POINTER(requester_id);
    DBGOUT(REQUESTS, "find_pcie_pending_requests in table %lx for id %d\n",
           (long)requesters_table, requester_id);
    return g_hash_table_lookup(requesters_table, key);
}

static inline PCIePendingRequests *
get_pcie_pending_requests(GHashTable *requesters_table, uint16_t requester_id)
{
    PCIePendingRequests *requests =
        find_pcie_pending_requests(requesters_table, requester_id);
    if (requests == NULL) {
        DBGOUT(REQUESTS, "request not found for id %d\n", requester_id);
        requests = (PCIePendingRequests *)g_malloc0(sizeof(PCIePendingRequests));
        g_hash_table_replace(requesters_table,
                             GUINT_TO_POINTER(requester_id),
                             requests);
        DBGOUT(REQUESTS, "request allocated for id %d\n", requester_id);
    } else {
        DBGOUT(REQUESTS, "request found for id %d\n", requester_id);
    }
    return requests;
}

bool register_pcie_request(GHashTable *requesters_table,
                           uint16_t requester_id,
                           PCIeRequest **pointer_to_pending,
                           uint8_t *tag)
{
    PCIePendingRequests *pending =
        get_pcie_pending_requests(requesters_table, requester_id);
    unsigned i;
    for (i = 0; i < 256; ++i) {
        uint8_t index = (uint8_t)(i + pending->current_tag);
        PCIeRequest *request = &pending->requests[index];
        if (!request->waiting) {
            request->ready = false;
            request->waiting = true;
            *tag = index;
            *pointer_to_pending = request;
            pending->current_tag = index;
            return true;
        }
    }
    return false;
}

PCIeRequest *find_pcie_request(GHashTable *requesters_table,
                               uint16_t requester_id,
                               uint8_t tag)
{
    PCIePendingRequests *pending =
        find_pcie_pending_requests(requesters_table, requester_id);
    if (!pending) {
        return NULL;
    }
    return &pending->requests[tag];
}

static bool pcie_request_wait_function(void *user_data) {
    PCIeRequest *request = user_data;
    return request->ready;
}

struct kvm_userspace_preemption_data;
void (*pkvm_userspace_entry)(struct kvm_userspace_preemption_data *);
void (*pkvm_userspace_exit)(struct kvm_userspace_preemption_data *);
void (*pkvm_userspace_spend)(struct kvm_userspace_preemption_data *preemption_data, unsigned time);
void wait_on_pcie_request(IPCConnection *connection, PCIeRequest *request)
{
    int delay;
    char *PCIE_REQUEST_DELAY = getenv("PCIE_REQUEST_DELAY");
    if(PCIE_REQUEST_DELAY)
        delay = atoi(PCIE_REQUEST_DELAY);
    else
        delay = 64;
    struct kvm_userspace_preemption_data preemption_data;
    pkvm_userspace_entry(&preemption_data);
    pkvm_userspace_spend(&preemption_data, delay);
    wait_on_ipc_connection(connection, pcie_request_wait_function, request);
    pkvm_userspace_exit(&preemption_data);
}
