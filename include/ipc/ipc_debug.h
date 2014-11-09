#ifndef QEMU_IPC_DEBUG_H
#define QEMU_IPC_DEBUG_H

//#define EXTERNAL_PCI_DEBUG

#if defined(EXTERNAL_PCI_DEBUG) && defined(IPC_DBGKEY)
#define IPC_DEBUG_ENUM(x, v) static bool IPC_DEBUG_##x = v
#define IF_DBGOUT(what, code) do {              \
        if (IPC_DEBUG_##what) {                 \
            code;                               \
        }                                       \
    } while (0)
#define DBGPRINT(fmt, ...)                                          \
    do {                                                            \
        fprintf(stderr, fmt , ## __VA_ARGS__);                      \
    } while(0)
#define DBGPRINT_IMPL2(key, fmt, ...) DBGPRINT(#key fmt, ## __VA_ARGS__)
#define DBGPRINT_IMPL(...) DBGPRINT_IMPL2(__VA_ARGS__)
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
    IF_DBGOUT(what, DBGPRINT_IMPL(IPC_DBGKEY, ": " fmt "\n", ## __VA_ARGS__))

#define IPC_DEBUG_ON(x) IPC_DEBUG_ENUM(x, true)
#define IPC_DEBUG_OFF(x) IPC_DEBUG_ENUM(x, false)

#endif /* QEMU_IPC_DEBUG_H */
