#ifndef QEMU_IPC_DEBUG_H
#define QEMU_IPC_DEBUG_H

#include "qemu/error-report.h"

extern bool ipc_debug_enabled;

#ifndef _WIN32
#define IPC_DEBUG_NEWLINE "\n"
#else
#define IPC_DEBUG_NEWLINE "\r\n"
#endif

#if defined(IPC_DBGKEY)
#define IPC_DEBUG_ENUM(x) static bool IPC_DEBUG_##x
#define IPC_DEBUG_CONSTRUCT(key, x)                                     \
    IPC_DEBUG_ENUM(x);                                                  \
    static void __attribute__((constructor)) constructor_IPC_DEBUG_##x(void) \
    {                                                                   \
        const char *envvalue = getenv("IPC_DEBUG_" #key "_" #x);        \
        if (envvalue && *envvalue && (*envvalue != '0')) {              \
            IPC_DEBUG_##x = true;                                       \
        }                                                               \
    }
#define IF_DBGOUT(what, code) do {                      \
        if (IPC_DEBUG_##what && ipc_debug_enabled) {    \
            code;                                       \
        }                                               \
    } while (0)
#define DBGPRINT(fmt, ...)                                          \
    do {                                                            \
        error_printf(fmt , ## __VA_ARGS__);                         \
    } while(0)
#define DBGPRINT_IMPL2(key, fmt, ...) DBGPRINT(#key fmt, ## __VA_ARGS__)
#define DBGPRINT_IMPL(...) DBGPRINT_IMPL2(__VA_ARGS__)
#define	IPC_DBGOUT(key, what, fmt, ...)                                 \
    do {                                                                \
        if (IPC_DEBUG_##what && ipc_debug_enabled) {                    \
            error_printf(#key ": " fmt IPC_DEBUG_NEWLINE, ## __VA_ARGS__); \
        }                                                               \
    } while (0)
#else
#define IPC_DEBUG_ENUM(x)
#define IPC_DEBUG_CONSTRUCT(key, x)
#define IF_DBGOUT(what, code) do {} while (0)
#endif /* defined(IPC_DBGKEY) */

#define	DBGOUT(what, fmt, ...) \
    IF_DBGOUT(what, DBGPRINT_IMPL(IPC_DBGKEY, ": " fmt IPC_DEBUG_NEWLINE, ## __VA_ARGS__))

#define IPC_DEBUG_ON_IMPL(key, x) IPC_DEBUG_CONSTRUCT(key, x)
#define IPC_DEBUG_ON(x) IPC_DEBUG_ON_IMPL(IPC_DBGKEY, x)

#endif /* QEMU_IPC_DEBUG_H */
