#include "config-host.h"
#include "ipc/ipc_channel.h"

bool ipc_debug_enabled = false;

static void __attribute__((constructor)) ipc_debug_init(void)
{
    const char *IPC_DEBUG = getenv("IPC_DEBUG");
    if (IPC_DEBUG && (*IPC_DEBUG == '1')) {
        ipc_debug_enabled = true;
    }
}
