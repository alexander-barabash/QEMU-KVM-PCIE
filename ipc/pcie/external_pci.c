/*
 * QEMU external PCI
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
#include "hw/pci/pci.h"
#include "net/net.h"
#include "qemu/osdep.h"
#include "qemu/range.h"
#include "qemu/error-report.h"
#include "ipc/pcie/downstream_pcie_connection.h"

#define EXTERNAL_PCI_DEBUG
#ifdef EXTERNAL_PCI_DEBUG
enum {
    DEBUG_GENERAL, DEBUG_INITIAL,
};
#define DBGBIT(x)	(1<<DEBUG_##x)
static int debugflags = 0; /*DBGBIT(GENERAL);*/

#define	DBGOUT(what, fmt, ...) do { \
    if (debugflags & DBGBIT(what)) \
        fprintf(stderr, "external_pci: " fmt "\n", ## __VA_ARGS__); \
    } while (0)
#else
#define	DBGOUT(what, fmt, ...) do {} while (0)
#endif

enum {
    DEVICE_BIG_ENDIAN_FLAG_NR,
    DEVICE_LITTLE_ENDIAN_FLAG_NR,
    USE_ABSTRACT_SOCKET_FLAG_NR,
};

enum {
    BIG_ENDIAN_FLAG_NR,
    LITTLE_ENDIAN_FLAG_NR,
    PREFETCHABLE_FLAG_NR,
    ROM_FLAG_NR,
    RAM_FLAG_NR,
    IO_FLAG_NR,
    MEM_64BIT_FLAG_NR,
};

typedef struct ExternalPCIState ExternalPCIState;
typedef struct BARInfo {
    char *name;
    uint32_t flags;
    uint32_t last_written_value;
    QemuMappedFileData file_data;
    MemoryRegion region;
    MemoryRegionOps ops;
    ExternalPCIState *dev;
    pcibus_t base_address;
    bool need_flush;
} BARInfo;

static inline
uint64_t bar_size(BARInfo *bar_info)
{
    return bar_info->file_data.length;
}

static inline
const char *bar_file(BARInfo *bar_info)
{
    const char *file = bar_info->file_data.filename;
    if (file && !*file) {
        return NULL;
    } else {
        return file;
    }
}

static inline void
bar_update_size(BARInfo *bar_info)
{
    const char *file = bar_file(bar_info);
    if (bar_size(bar_info) != 0) {
        return;
    }
    if (file == NULL) {
        return;
    }
    /* TODO: get the file's size. */
    /* TODO: Take care it is a power of two. */
}

static inline bool
bar_size_power_of_two(BARInfo *bar_info)
{
    uint64_t size = bar_size(bar_info);
    return (size != 0) && ((size & (size - 1)) == 0);
}

static inline bool
bar_is_io(BARInfo *bar_info)
{
    return ((bar_info->flags & (1 << IO_FLAG_NR)) != 0);
}

static inline bool
bar_is_64bit(BARInfo *bar_info)
{
    return ((bar_info->flags & (1 << MEM_64BIT_FLAG_NR)) != 0);
}

static inline bool
bar_is_ram(BARInfo *bar_info)
{
    return ((bar_info->flags & (1 << RAM_FLAG_NR)) != 0);
}

static inline bool
bar_is_rom(BARInfo *bar_info)
{
    return ((bar_info->flags & (1 << ROM_FLAG_NR)) != 0);
}

static inline bool
bar_is_ram_or_rom(BARInfo *bar_info)
{
    return bar_is_ram(bar_info) || bar_is_rom(bar_info);
}

static inline void
bar_set_rom(BARInfo *bar_info)
{
    bar_info->flags |= (1 << ROM_FLAG_NR);
}

static inline bool
bar_prefetchable(BARInfo *bar_info)
{
    return ((bar_info->flags & (1 << PREFETCHABLE_FLAG_NR)) != 0);
}

static inline bool
bar_big_endian(BARInfo *bar_info)
{
    return
        ((bar_info->flags & (1 << LITTLE_ENDIAN_FLAG_NR)) == 0) &&
        ((bar_info->flags & (1 << BIG_ENDIAN_FLAG_NR)) != 0);
}

static inline bool
bar_little_endian(BARInfo *bar_info)
{
    return
        ((bar_info->flags & (1 << LITTLE_ENDIAN_FLAG_NR)) != 0) &&
        ((bar_info->flags & (1 << BIG_ENDIAN_FLAG_NR)) == 0);
}

static inline bool
bar_native_endian(BARInfo *bar_info)
{
    return
        ((bar_info->flags & (1 << LITTLE_ENDIAN_FLAG_NR)) == 0) &&
        ((bar_info->flags & (1 << BIG_ENDIAN_FLAG_NR)) == 0);
}

static inline enum device_endian
bar_endianness(BARInfo *bar_info)
{
    if (bar_big_endian(bar_info)) {
        return DEVICE_BIG_ENDIAN;
    } else if (bar_little_endian(bar_info)) {
        return DEVICE_LITTLE_ENDIAN;
    } else {
        return DEVICE_NATIVE_ENDIAN;
    }
}

#define PCI_NUM_BARS (PCI_NUM_REGIONS - 1)

struct ExternalPCIState {
    PCIDevice dev;
    NICState *nic;
    NICConf conf;

    DownstreamPCIeConnection *ipc_connection;
    BARInfo bar_info[PCI_NUM_REGIONS];
    uint32_t last_written_pci_command;

    uint32_t flags;
    char *ipc_socket_path;
    uint16_t external_device_id;
};

static inline bool
device_big_endian(ExternalPCIState *d)
{
    return
        ((d->flags & (1 << DEVICE_LITTLE_ENDIAN_FLAG_NR)) == 0) &&
        ((d->flags & (1 << DEVICE_BIG_ENDIAN_FLAG_NR)) != 0);
}

static inline bool
device_little_endian(ExternalPCIState *d)
{
    return
        ((d->flags & (1 << DEVICE_LITTLE_ENDIAN_FLAG_NR)) != 0) &&
        ((d->flags & (1 << DEVICE_BIG_ENDIAN_FLAG_NR)) == 0);
}

static inline enum device_endian
device_endianness(ExternalPCIState *d)
{
    if (device_big_endian(d)) {
        return DEVICE_BIG_ENDIAN;
    } else if (device_little_endian(d)) {
        return DEVICE_LITTLE_ENDIAN;
    } else {
        return DEVICE_NATIVE_ENDIAN;
    }
}

static inline void
bar_update_endianness(BARInfo *bar_info, ExternalPCIState *d)
{
    if (bar_native_endian(bar_info)) {
        if (device_little_endian(d)) {
            bar_info->flags |= (1 << LITTLE_ENDIAN_FLAG_NR);
        } else if (device_big_endian(d)) {
            bar_info->flags |= (1 << BIG_ENDIAN_FLAG_NR);
        }
    }
}

static void
pci_external_uninit(PCIDevice *dev)
{
    ExternalPCIState *d = DO_UPCAST(ExternalPCIState, dev, dev);
    BARInfo *bar_info = d->bar_info;
    int i;

    for (i = 0; i < PCI_NUM_REGIONS; ++i) {
        memory_region_destroy(&bar_info[i].region);
        qemu_unmap_file_data(&bar_info[i].file_data);
    }

    qemu_del_nic(d->nic);
}

static uint32_t
external_pci_config_read(PCIDevice *dev, uint32_t address, int len)
{
    uint32_t val;
    uint32_t le32_val;
    ExternalPCIState *d = DO_UPCAST(ExternalPCIState, dev, dev);
    DBGOUT(GENERAL, "external_pci_config_read addr=0x%X len=%d",
           address, len);
    val = read_downstream_pcie_config(d->ipc_connection,
                                      dev, address, len);
    le32_val = cpu_to_le32(val);
    memcpy(dev->config + address, &le32_val, len);
    DBGOUT(GENERAL, "external_pci_config_read addr=0x%X len=%d val=0x%X",
           address, len, val);
    return val;
}

static void pci_update_region_mapping(PCIDevice *d, PCIIORegion *r,
                                      pcibus_t new_addr)
{
    /* This bar isn't changed */
    if (new_addr == r->addr) {
        return;
    }

    /* now do the real mapping */
    if (r->addr != PCI_BAR_UNMAPPED) {
        memory_region_del_subregion(r->address_space, r->memory);
    }
    r->addr = new_addr;
    if (r->addr != PCI_BAR_UNMAPPED) {
        DBGOUT(GENERAL, "pci_update_region_mapping in %s: address=%llx\n",
               d->name, (unsigned long long)r->addr);
        memory_region_add_subregion_overlap(r->address_space,
                                            r->addr, r->memory, 1);
    } else {
        DBGOUT(GENERAL, "pci_update_region_mapping in %s: region disabled\n",
               d->name);
    }
}

static void update_last_written_value(uint32_t *last_written_value,
                                      uint32_t addr, uint32_t val, int l)
{
    switch (l) {
    default:
        break;
    case 4:
        *last_written_value = val;
        break;
    case 2:
    case 1:
        {
            uint32_t sh = ((addr & (4 - l)) * 8);
            uint32_t val_mask = (1 << (l * 8)) - 1;
            uint32_t old_value_mask = ~(val_mask << sh);
            val &= val_mask;
            *last_written_value &= old_value_mask;
            *last_written_value |= (val << sh);
        }
        break;
    }
}

static void pci_bar_update_last_written_value(BARInfo *bar_info, uint32_t addr,
                                              uint32_t val, int l)
{
    update_last_written_value(&bar_info->last_written_value, addr, val, l);
}

static void pci_update_bar_mapping(PCIDevice *dev, uint16_t pci_command,
                                   int index)
{
    ExternalPCIState *d = DO_UPCAST(ExternalPCIState, dev, dev);
    PCIIORegion *r = &dev->io_regions[index];
    BARInfo *bar_info = &d->bar_info[index];
    pcibus_t base_address;

    /* this region isn't registered */
    if (!r->size) {
        return;
    }

    base_address = bar_info->last_written_value;
    if (bar_is_rom(bar_info)) {
        if ((pci_command & PCI_COMMAND_MEMORY) != 0) {
            if ((base_address & PCI_ROM_ADDRESS_ENABLE) == 0) {
                base_address = PCI_BAR_UNMAPPED;
            } else {
                base_address &= PCI_ROM_ADDRESS_MASK;
            }
        } else {
            base_address = PCI_BAR_UNMAPPED;
        }
    } else if (bar_is_io(bar_info)) {
        if ((pci_command & PCI_COMMAND_IO) != 0) {
            base_address &= PCI_BASE_ADDRESS_IO_MASK;
        } else {
            base_address = PCI_BAR_UNMAPPED;
        }
    } else {
        if ((pci_command & PCI_COMMAND_MEMORY) != 0) {
            base_address &= PCI_BASE_ADDRESS_MEM_MASK;
            if (bar_is_64bit(bar_info)) {
                BARInfo *bar_info2 = &d->bar_info[index + 1];
                pcibus_t base_address2 = bar_info2->last_written_value;
                base_address |= base_address2 << 32;
            }
        } else {
            base_address = PCI_BAR_UNMAPPED;
        }
    }
    if (base_address != PCI_BAR_UNMAPPED) {
        base_address &= ~(bar_size(bar_info) - 1);
        DBGOUT(GENERAL, "pci_update_bar_mapping on %s.%d: "
               "address=%llx is_rom=%d is_io=%d is_64bit=%d\n",
               dev->name, index, (unsigned long long)base_address,
               bar_is_rom(bar_info), bar_is_io(bar_info), bar_is_64bit(bar_info));
    } else {
        DBGOUT(GENERAL, "pci_update_bar_mapping on %s.%d: "
               "address=UNMAPPED is_rom=%d is_io=%d is_64bit=%d\n",
               dev->name, index,
               bar_is_rom(bar_info), bar_is_io(bar_info), bar_is_64bit(bar_info));
    }
    d->bar_info[index].base_address = base_address;
    pci_update_region_mapping(dev, r, base_address);
}

static void pci_update_all_bar_mappings(PCIDevice *d, uint16_t pci_command)
{
    int i;

    for(i = 0; i < PCI_NUM_REGIONS; i++) {
        pci_update_bar_mapping(d, pci_command, i);
    }
}

static void
external_pci_config_write(PCIDevice *dev, uint32_t addr, uint32_t val,
                          int l)
{
    ExternalPCIState *d = DO_UPCAST(ExternalPCIState, dev, dev);
    bool covers_pci_command = range_covers_byte(addr, l, PCI_COMMAND);
#if 0
    bool was_irq_disabled = 0;
    if (covers_pci_command) {
        was_irq_disabled =
            (d->last_written_pci_command & PCI_COMMAND_INTX_DISABLE) != 0;
    }
#endif

    DBGOUT(GENERAL, "external_pci_config_write addr=0x%X len=%d val=0x%x",
           addr, l, val);

    write_downstream_pcie_config(d->ipc_connection,
                                 dev, addr, val, l);

    if (covers_pci_command) {
        update_last_written_value(&d->last_written_pci_command, addr, val, l);
        pci_update_all_bar_mappings(dev, d->last_written_pci_command);
#if 0
        pci_update_irq_disabled(d, was_irq_disabled);
#endif
        memory_region_set_enabled(&dev->bus_master_enable_region,
                                  d->last_written_pci_command & PCI_COMMAND_MASTER);
        memory_region_set_enabled(&dev->bus_master_io_enable_region,
                                  d->last_written_pci_command & PCI_COMMAND_MASTER);
    } else if (addr >= PCI_BASE_ADDRESS_0) {
        int index = (addr - PCI_BASE_ADDRESS_0) / 4;
        if (index == (PCI_ROM_ADDRESS - PCI_BASE_ADDRESS_0) / 4) {
            index = PCI_ROM_SLOT;
        }
        if (index < PCI_NUM_REGIONS) {
            pci_bar_update_last_written_value(&d->bar_info[index],
                                              addr, val, l);
            pci_update_bar_mapping(dev, d->last_written_pci_command,
                                   index);
        }
    }

    /* TODO: handle MSI. */
    /* TODO: handle MSIX. */
}

static inline uint64_t
external_pci_read_direct(void *opaque, hwaddr addr, unsigned size)
{
    uintptr_t base = (uintptr_t)opaque;
    void *p = (void *)(base + (uintptr_t)addr);
    union {
        uint8_t byte;
        uint16_t word;
        uint32_t dword;
        uint64_t qword;
    } *pointer = p;

    DBGOUT(GENERAL, "external_pci_read_direct addr=0x%llX size=%d",
           (unsigned long long)addr, size);

    switch (size) {
    case 1:
        return pointer->byte;
    case 2:
        return pointer->word;
    case 4:
        return pointer->dword;
    case 8:
        return pointer->qword;
    default:
        abort();
    }

    return 0;
}

static void
external_pci_write_direct(void *opaque, hwaddr addr, uint64_t val,
                          unsigned size)
{
    uintptr_t base = (uintptr_t)opaque;
    void *p = (void *)(base + addr);
    union {
        uint8_t byte;
        uint16_t word;
        uint32_t dword;
        uint64_t qword;
    } *pointer = p;
    
    DBGOUT(GENERAL, "external_pci_write_direct addr=0x%llX val=0x%llX size=%d",
           (unsigned long long)addr, (unsigned long long)val, size);

    switch (size) {
    case 1:
        pointer->byte = val;
        break;
    case 2:
        pointer->word = val;
        break;
    case 4:
        pointer->dword = val;
        break;
    case 8:
        pointer->qword = val;
        break;
    default:
        abort();
    }
}

static inline uint64_t
external_pci_read_memory(void *opaque, hwaddr addr, unsigned size)
{
    BARInfo *bar_info = opaque;

    DBGOUT(GENERAL, "external_pci_read_memory addr=0x%llX size=%d",
           (unsigned long long)addr, size);

    if (bar_info->need_flush) {
        bar_info->need_flush = false;
        return read_downstream_pcie_memory(bar_info->dev->ipc_connection,
                                           &bar_info->dev->dev,
                                           bar_info->base_address + addr,
                                           size);
    } else {
        return external_pci_read_direct(bar_info->file_data.pointer, addr, size);
    }
}

static void
external_pci_write_memory(void *opaque, hwaddr addr, uint64_t val,
                          unsigned size)
{
    BARInfo *bar_info = opaque;
    
    DBGOUT(GENERAL, "external_pci_write_memory addr=0x%llX val=0x%llX size=%d",
           (unsigned long long)addr, (unsigned long long)val, size);

    switch (size) {
    case 1:
        break;
    case 2:
        break;
    case 4:
        break;
    case 8:
        break;
    default:
        abort();
    }
    write_downstream_pcie_memory(bar_info->dev->ipc_connection,
                                 &bar_info->dev->dev,
                                 bar_info->base_address + addr,
                                 val, size);
    bar_info->need_flush = true;
}

static uint64_t
external_pci_read_mmio(void *opaque, hwaddr addr, unsigned size)
{
    BARInfo *bar_info = opaque;
    uint64_t val;

    DBGOUT(GENERAL, "external_pci_read_mmio addr=0x%llX size=%d",
           (unsigned long long)addr, size);

    val = read_downstream_pcie_memory(bar_info->dev->ipc_connection,
                                      &bar_info->dev->dev,
                                      bar_info->base_address + addr,
                                      size);
    DBGOUT(GENERAL, "external_pci_read_mmio addr=0x%llX size=%d val=0x%llX",
           (unsigned long long)addr, size, (unsigned long long)val);
    return val;
}

static void
external_pci_write_mmio(void *opaque, hwaddr addr, uint64_t val,
                        unsigned size)
{
    BARInfo *bar_info = opaque;
    
    DBGOUT(GENERAL, "external_pci_write_mmio addr=0x%llX size=%d val=0x%llX",
           (unsigned long long)addr, size, (unsigned long long)val);

    write_downstream_pcie_memory(bar_info->dev->ipc_connection,
                                 &bar_info->dev->dev,
                                 bar_info->base_address + addr,
                                 val, size);
}

static uint64_t
external_pci_read_io(void *opaque, hwaddr addr, unsigned size)
{
    BARInfo *bar_info = opaque;
    uint64_t val;

    DBGOUT(GENERAL, "external_pci_read_io addr=0x%llX size=%d",
           (unsigned long long)addr, size);

    val = read_downstream_pcie_io(bar_info->dev->ipc_connection,
                                  &bar_info->dev->dev,
                                  bar_info->base_address + addr,
                                  size);
    DBGOUT(GENERAL, "external_pci_read_io addr=0x%llX size=%d val=0x%llX",
           (unsigned long long)addr, size, (unsigned long long)val);
    return val;
}

static void
external_pci_write_io(void *opaque, hwaddr addr, uint64_t val,
                        unsigned size)
{
    BARInfo *bar_info = opaque;
    
    DBGOUT(GENERAL, "external_pci_write_io addr=0x%llX size=%d val=0x%llX",
           (unsigned long long)addr, size, (unsigned long long)val);

    write_downstream_pcie_io(bar_info->dev->ipc_connection,
                             &bar_info->dev->dev,
                             bar_info->base_address + addr,
                             val, size);
}

static int pci_external_init(PCIDevice *pci_dev)
{
    ExternalPCIState *d = DO_UPCAST(ExternalPCIState, dev, pci_dev);
    MemoryRegion *address_space_memory = pci_address_space(pci_dev);
    MemoryRegion *address_space_io = pci_address_space_io(pci_dev);
    BARInfo *bar_info = d->bar_info;
    PCIIORegion *io_region = pci_dev->io_regions;
    bool upper_bar = false;
    int ii;

    d->last_written_pci_command = 0;
    DBGOUT(INITIAL, "Establishing IPC connection on path %s (flag=%d)\n",
           d->ipc_socket_path, (d->flags & (1 << USE_ABSTRACT_SOCKET_FLAG_NR)));
    d->ipc_connection =
        init_pcie_downstream_ipc(d->ipc_socket_path,
                                 d->flags & (1 << USE_ABSTRACT_SOCKET_FLAG_NR),
                                 pci_dev);
    if (d->ipc_connection == NULL) {
        error_report("Failed to establish IPC connection on path %s (flag=%d)\n",
                     d->ipc_socket_path, (d->flags & (1 << USE_ABSTRACT_SOCKET_FLAG_NR)));
        return -1;
    } else {
        DBGOUT(INITIAL, "Established IPC connection on path %s (flag=%d)\n",
               d->ipc_socket_path, (d->flags & (1 << USE_ABSTRACT_SOCKET_FLAG_NR)));
    }

    send_special_downstream_pcie_msg(d->ipc_connection,
                                     pci_dev,
                                     d->external_device_id);
    DBGOUT(INITIAL, "Sent special PCI message for device %s %d:%d:%d (ID=%d)\n",
           pci_dev->name,
           (unsigned)pci_bus_num(pci_dev->bus),
           (unsigned)PCI_SLOT(pci_dev->devfn),
           (unsigned)PCI_FUNC(pci_dev->devfn),
           d->external_device_id);

    for (ii = 0; ii < PCI_NUM_REGIONS; ++ii, ++bar_info, ++io_region) {
        bar_info->dev = d;
        bar_info->last_written_value = 0;
        bar_update_size(bar_info);
        if (upper_bar) {
            upper_bar = false;
            if (bar_info->flags != 0) {
                error_report("Attempt to use PCI bar %d twice for device %s\n",
                             ii, pci_dev->name);
                return -1;
            }
            if (bar_size(bar_info) != 0) {
                error_report("Attempt to use PCI bar %d twice for device %s\n",
                             ii, pci_dev->name);
                return -1;
            }
            continue;
        } else if (bar_size(bar_info) == 0) {
            if (bar_info->flags == 0) {
                continue;
            } else {
                error_report("Size of PCI bar %d unspecified for device %s\n",
                             ii, pci_dev->name);
                return -1;
            }
        }
        if (ii == PCI_ROM_SLOT) {
            bar_set_rom(bar_info);
        }

        if (!bar_size_power_of_two(bar_info)) {
            error_report("Size of PCI bar %d (%" PRIu64 ")"
                         " is not a power of two"
                         " for device %s\n",
                         ii, bar_size(bar_info), pci_dev->name);
            return -1;
        }

        bar_update_endianness(bar_info, d);

        if ((bar_info->name == NULL) || (*bar_info->name == '\0')) {
            if (ii == PCI_ROM_SLOT) {
                bar_info->name = g_strdup_printf("%s-rom", pci_dev->name);
            } else {
                bar_info->name = g_strdup_printf("%s-bar%d", pci_dev->name, ii);
            }
        }

        if (bar_is_rom(bar_info)) {
            if (bar_is_ram(bar_info)) {
                error_report("ROM PCI bar specified as RAM for device %s\n",
                             pci_dev->name);
                return -1;
            }
            if (bar_is_64bit(bar_info)) {
                error_report("ROM PCI bar specified as 64-bit"
                             " for device %s\n",
                             pci_dev->name);
                return -1;
            }
            if(!bar_file(bar_info)) {
                error_report("File for ROM PCI bar"
                             " for device %s unspecified\n",
                             pci_dev->name);
                return -1;
            }
            if(!qemu_map_file_data(&bar_info->file_data)) {
                error_report("Cannot map file \"%s\" for ROM PCI bar"
                             " for device %s\n",
                             bar_file(bar_info), pci_dev->name);
                    return -1;
            }
            io_region->type = PCI_BASE_ADDRESS_SPACE_MEMORY;
            io_region->type |= PCI_BASE_ADDRESS_MEM_TYPE_32;
            io_region->type |= PCI_BASE_ADDRESS_MEM_PREFETCH;
            io_region->address_space = address_space_memory;
        } else if (bar_is_io(bar_info)) {
            if (bar_is_ram(bar_info)) {
                error_report("I/O PCI bar %d specified as RAM for device %s\n",
                             ii, pci_dev->name);
                return -1;
            }
            if (bar_prefetchable(bar_info)) {
                error_report("I/O PCI bar %d specified as prefetchable"
                             " for device %s\n",
                             ii, pci_dev->name);
                return -1;
            }
            if (bar_is_64bit(bar_info)) {
                error_report("I/O PCI bar %d specified as 64-bit"
                             " for device %s\n",
                             ii, pci_dev->name);
                return -1;
            }
            io_region->type = PCI_BASE_ADDRESS_SPACE_IO;
            io_region->address_space = address_space_io;
        } else {
            io_region->type = PCI_BASE_ADDRESS_SPACE_MEMORY;
            if (bar_is_64bit(bar_info)) {
                if (ii + 1 >= PCI_NUM_BARS) {
                    error_report("Last PCI bar %d cannot be 64-bit"
                                 " for device %s\n",
                                 ii, pci_dev->name);
                    return -1;
                }
                upper_bar = true;
                io_region->type |= PCI_BASE_ADDRESS_MEM_TYPE_64;
            } else {
                if (bar_size(bar_info) > 0xFFFFFFFFu) {
                    error_report("Size of 32-bit PCI bar %d (%" PRIu64 ")"
                                 " cannot be 64-bit for device %s\n",
                                 ii, bar_size(bar_info), pci_dev->name);
                    return -1;
                }
                io_region->type |= PCI_BASE_ADDRESS_MEM_TYPE_32;
            }

            if (bar_prefetchable(bar_info) || bar_is_ram(bar_info)) {
                if(bar_file(bar_info) &&
                   !qemu_map_file_data(&bar_info->file_data)) {
                    error_report("Cannot map file \"%s\" for PCI bar %d"
                                 " for device %s\n",
                                 bar_file(bar_info), ii, pci_dev->name);
                    return -1;
                }
                io_region->type |= PCI_BASE_ADDRESS_MEM_PREFETCH;
            }
            io_region->address_space = address_space_memory;
        }

        io_region->addr = PCI_BAR_UNMAPPED;
        io_region->size = bar_size(bar_info);
        io_region->memory = &bar_info->region;

        if (bar_is_ram_or_rom(bar_info) &&
            !is_wrong_endian(bar_endianness(bar_info))) {
            memory_region_init_ram_ptr(&bar_info->region,
                                       OBJECT(pci_dev),
                                       bar_info->name, io_region->size,
                                       bar_info->file_data.pointer);
        } else {
            void *opaque = bar_info;
            MemoryRegionOps *ops = &bar_info->ops;
            memset(ops, 0, sizeof(MemoryRegionOps));

            ops->endianness = bar_endianness(bar_info);
            
            ops->valid.min_access_size = 1;
            if (bar_is_io(bar_info)) {
                ops->valid.max_access_size = 4;
            } else if(bar_is_ram_or_rom(bar_info) ||
                      bar_prefetchable(bar_info)) {
                ops->valid.max_access_size = 8;
            }

            if (bar_is_io(bar_info)) {
                ops->read = external_pci_read_io;
                ops->write = external_pci_write_io;
            } else if (bar_is_ram_or_rom(bar_info) && bar_file(bar_info)) {
                opaque = bar_info->file_data.pointer;
                ops->read = external_pci_read_direct;
                ops->write = external_pci_write_direct;
            } else if (bar_prefetchable(bar_info) && bar_file(bar_info)) {
                ops->read = external_pci_read_memory;
                ops->write = external_pci_write_memory;
                /*
                 * For some parts of the memory we may wish
                 * to call memory_region_add_coalescing().
                 */
            } else {
                ops->read = external_pci_read_mmio;
                ops->write = external_pci_write_mmio;
            }

            memory_region_init_io(&bar_info->region, OBJECT(pci_dev), ops,
                                  opaque, bar_info->name, io_region->size);
        }

        if (bar_is_ram_or_rom(bar_info)) {
            memory_region_set_coalescing(&bar_info->region);
        }
    }

    return 0;
}

static void qdev_external_pci_reset(DeviceState *dev)
{
}

#define DEFINE_PCI_BAR_PROPS(_name, index)                              \
    DEFINE_PROP_STRING(_name "_name" #index, ExternalPCIState,          \
                       bar_info[index].name),                           \
        DEFINE_PROP_BIT(_name "_ram" #index, ExternalPCIState,          \
                        bar_info[index].flags, RAM_FLAG_NR, false),     \
        DEFINE_PROP_BIT(_name "_prefetchable" #index, ExternalPCIState, \
                        bar_info[index].flags, PREFETCHABLE_FLAG_NR, false), \
        DEFINE_PROP_BIT(_name "_io" #index, ExternalPCIState,           \
                        bar_info[index].flags, IO_FLAG_NR, false),      \
        DEFINE_PROP_BIT(_name "_64bit" #index, ExternalPCIState,        \
                        bar_info[index].flags, MEM_64BIT_FLAG_NR, false), \
        DEFINE_PROP_BIT(_name "_big_endian" #index, ExternalPCIState,   \
                        bar_info[index].flags, BIG_ENDIAN_FLAG_NR,      \
                        false),                                         \
        DEFINE_PROP_BIT(_name "_little_endian" #index, ExternalPCIState, \
                        bar_info[index].flags, LITTLE_ENDIAN_FLAG_NR,   \
                        false),                                         \
        DEFINE_PROP_UINT64(_name "_size" #index, ExternalPCIState,      \
                           bar_info[index].file_data.length, 0),        \
        DEFINE_PROP_STRING(_name "_file" #index, ExternalPCIState,      \
                           bar_info[index].file_data.filename),         \
        DEFINE_PROP_UINT64(_name "_file_offset" #index, ExternalPCIState, \
                           bar_info[index].file_data.offset, 0)

static Property external_pci_properties[] = {
    DEFINE_NIC_PROPERTIES(ExternalPCIState, conf),

    DEFINE_PROP_BIT("pci_express", ExternalPCIState, dev.cap_present,
                    QEMU_PCI_CAP_EXPRESS_BITNR, true),

    DEFINE_PROP_BIT("big_endian", ExternalPCIState,
                    flags, DEVICE_BIG_ENDIAN_FLAG_NR,
                    false),
    DEFINE_PROP_BIT("little_endian", ExternalPCIState,
                    flags, DEVICE_LITTLE_ENDIAN_FLAG_NR,
                    false),

    DEFINE_PCI_BAR_PROPS("pci_bar", 0),
    DEFINE_PCI_BAR_PROPS("pci_bar", 1),
    DEFINE_PCI_BAR_PROPS("pci_bar", 2),
    DEFINE_PCI_BAR_PROPS("pci_bar", 3),
    DEFINE_PCI_BAR_PROPS("pci_bar", 4),
    DEFINE_PCI_BAR_PROPS("pci_bar", 5),
    DEFINE_PCI_BAR_PROPS("pci_bar", 6),

    DEFINE_PROP_STRING("ipc_socket_path", ExternalPCIState, ipc_socket_path),
    DEFINE_PROP_UINT16("external_device_id", ExternalPCIState,
                       external_device_id, 0),
    DEFINE_PROP_BIT("ipc_use_unix_socket", ExternalPCIState,
                    flags, USE_ABSTRACT_SOCKET_FLAG_NR,
                    false),


    DEFINE_PROP_END_OF_LIST(),
};

static void external_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = pci_external_init;
    k->config_read = external_pci_config_read;
    k->config_write = external_pci_config_write;
    k->exit = pci_external_uninit;
    dc->desc = "External PCIe endpoint";
    dc->reset = qdev_external_pci_reset;
#ifdef E1000_IMPL
    dc->vmsd = &vmstate_e1000;
#endif /* E1000_IMPL */
    dc->props = external_pci_properties;
}

static const TypeInfo external_pci_info = {
    .name          = "external_pci",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(ExternalPCIState),
    .class_init    = external_pci_class_init,
};

static void external_pci_register_types(void)
{
    type_register_static(&external_pci_info);
}

type_init(external_pci_register_types)
