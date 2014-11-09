#ifndef QEMU_IPC_DEBUG_H
#define QEMU_IPC_DEBUG_H

#define EXTERNAL_PCI_DEBUG

#if defined(EXTERNAL_PCI_DEBUG) && defined(IPC_DBGKEY)
#define IPC_DEBUG_ENUM(x, v) static bool IPC_DEBUG_##x = v
#define IF_DBGOUT(what, code) do {              \
        if (IPC_DEBUG_##what) {                 \
            code;                               \
        }                                       \
    } while (0)
#define IPC_DBGPRINT(key, fmt, ...)                                 \
    do {                                                            \
        fprintf(stderr, #key ": " fmt , ## __VA_ARGS__);            \
    } while(0)
#define IPC_DBGPRINT_IMPL(...) IPC_DBGPRINT(__VA_ARGS__)
#define DBGPRINT(fmt, ...) IPC_DBGPRINT_IMPL(IPC_DBGKEY, fmt, ## __VA_ARGS__)
#define	IPC_DBGOUT(key, what, fmt, ...)                                 \
    do {                                                                \
        if (IPC_DEBUG_##what) {                                         \
            fprintf(stderr, #key ": " fmt "\n", ## __VA_ARGS__);        \
        }                                                               \
    } while (0)
#else
#define IPC_DEBUG_ENUM(x, v)
#define IF_DBGOUT(what, code) do {} while (0)
#endif /* defined(EXTERNAL_PCI_DEBUG) && defined(IPC_DBGKEY) */

#define	DBGOUT(what, fmt, ...) \
    IF_DBGOUT(what, DBGPRINT(fmt, ## __VA_ARGS__))

#define IPC_DEBUG_ON(x) IPC_DEBUG_ENUM(x, true)
#define IPC_DEBUG_OFF(x) IPC_DEBUG_ENUM(x, false)

#endif /* QEMU_IPC_DEBUG_H */
