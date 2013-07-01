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


#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie_trans.h"
#include "net/net.h"
#include "net/checksum.h"
#include "hw/loader.h"
#include "sysemu/sysemu.h"
#include "sysemu/dma.h"
#include "qemu/osdep.h"
#include "qemu/range.h"

#ifdef E1000_IMPL
#include "e1000_regs.h"
#endif

#define EXTERNAL_PCI_DEBUG
#ifdef EXTERNAL_PCI_DEBUG
enum {
    DEBUG_GENERAL,	DEBUG_IO,	DEBUG_MMIO,	DEBUG_INTERRUPT,
    DEBUG_RX,		DEBUG_TX,	DEBUG_MDIC,	DEBUG_EEPROM,
    DEBUG_UNKNOWN,	DEBUG_TXSUM,	DEBUG_TXERR,	DEBUG_RXERR,
    DEBUG_RXFILTER,     DEBUG_PHY,      DEBUG_NOTYET,
};
#define DBGBIT(x)	(1<<DEBUG_##x)
static int debugflags = DBGBIT(TXERR) | DBGBIT(GENERAL);

#define	DBGOUT(what, fmt, ...) do { \
    if (debugflags & DBGBIT(what)) \
        fprintf(stderr, "external_pci: " fmt "\n", ## __VA_ARGS__); \
    } while (0)
#else
#define	DBGOUT(what, fmt, ...) do {} while (0)
#endif

#ifdef E1000_IMPL
#define IOPORT_SIZE       0x40
#define PNPMMIO_SIZE      0x20000
#define MIN_BUF_SIZE      60 /* Min. octets in an ethernet frame sans FCS */

/* this is the size past which hardware will drop packets when setting LPE=0 */
#define MAXIMUM_ETHERNET_VLAN_SIZE 1522
/* this is the size past which hardware will drop packets when setting LPE=1 */
#define MAXIMUM_ETHERNET_LPE_SIZE 16384

/*
 * HW models:
 *  E1000_DEV_ID_82540EM works with Windows and Linux
 *  E1000_DEV_ID_82573L OK with windoze and Linux 2.6.22,
 *	appears to perform better than 82540EM, but breaks with Linux 2.6.18
 *  E1000_DEV_ID_82544GC_COPPER appears to work; not well tested
 *  Others never tested
 */
enum { E1000_DEVID = E1000_DEV_ID_82540EM };

/*
 * May need to specify additional MAC-to-PHY entries --
 * Intel's Windows driver refuses to initialize unless they match
 */
enum {
    PHY_ID2_INIT = E1000_DEVID == E1000_DEV_ID_82573L ?		0xcc2 :
                   E1000_DEVID == E1000_DEV_ID_82544GC_COPPER ?	0xc30 :
                   /* default to E1000_DEV_ID_82540EM */	0xc20
};
#endif /* E1000_IMPL */

enum {
    DEVICE_BIG_ENDIAN_FLAG_NR,
    DEVICE_LITTLE_ENDIAN_FLAG_NR,
};

enum {
    BIG_ENDIAN_FLAG_NR,
    LITTLE_ENDIAN_FLAG_NR,
    PREFETCHABLE_FLAG_NR,
    RAM_FLAG_NR,
    IO_FLAG_NR,
    MEM_64BIT_FLAG_NR,
};

typedef struct BARInfo {
    char *name;
    uint32_t flags;
    QemuMappedFileData file_data;
    MemoryRegion region;
    MemoryRegionOps ops;
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
    /* TODO: Take care it is a powr of two. */
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

typedef struct ExternalPCIState_st {
    PCIDevice dev;
    NICState *nic;
    NICConf conf;

    BARInfo bar_info[PCI_NUM_BARS];

    uint32_t flags;

    uint32_t mac_reg[0x8000];
    uint16_t phy_reg[0x20];
    uint16_t eeprom_data[64];

    uint32_t rxbuf_size;
    uint32_t rxbuf_min_shift;
    struct e1000_tx {
        unsigned char header[256];
        unsigned char vlan_header[4];
        /* Fields vlan and data must not be reordered or separated. */
        unsigned char vlan[4];
        unsigned char data[0x10000];
        uint16_t size;
        unsigned char sum_needed;
        unsigned char vlan_needed;
        uint8_t ipcss;
        uint8_t ipcso;
        uint16_t ipcse;
        uint8_t tucss;
        uint8_t tucso;
        uint16_t tucse;
        uint8_t hdr_len;
        uint16_t mss;
        uint32_t paylen;
        uint16_t tso_frames;
        char tse;
        int8_t ip;
        int8_t tcp;
        char cptse;     // current packet tse bit
    } tx;

    struct {
        uint32_t val_in;	// shifted in from guest driver
        uint16_t bitnum_in;
        uint16_t bitnum_out;
        uint16_t reading;
        uint32_t old_eecd;
    } eecd_state;

    QEMUTimer *autoneg_timer;

/* Compatibility flags for migration to/from qemu 1.3.0 and older */
#define E1000_FLAG_AUTONEG_BIT 0
#define E1000_FLAG_AUTONEG (1 << E1000_FLAG_AUTONEG_BIT)
    uint32_t compat_flags;
} ExternalPCIState;

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

#ifdef E1000_IMPL
#define	defreg(x)	x = (E1000_##x>>2)
enum {
    defreg(CTRL),	defreg(EECD),	defreg(EERD),	defreg(GPRC),
    defreg(GPTC),	defreg(ICR),	defreg(ICS),	defreg(IMC),
    defreg(IMS),	defreg(LEDCTL),	defreg(MANC),	defreg(MDIC),
    defreg(MPC),	defreg(PBA),	defreg(RCTL),	defreg(RDBAH),
    defreg(RDBAL),	defreg(RDH),	defreg(RDLEN),	defreg(RDT),
    defreg(STATUS),	defreg(SWSM),	defreg(TCTL),	defreg(TDBAH),
    defreg(TDBAL),	defreg(TDH),	defreg(TDLEN),	defreg(TDT),
    defreg(TORH),	defreg(TORL),	defreg(TOTH),	defreg(TOTL),
    defreg(TPR),	defreg(TPT),	defreg(TXDCTL),	defreg(WUFC),
    defreg(RA),		defreg(MTA),	defreg(CRCERRS),defreg(VFTA),
    defreg(VET),
};

static void
e1000_link_down(E1000State *s)
{
    s->mac_reg[STATUS] &= ~E1000_STATUS_LU;
    s->phy_reg[PHY_STATUS] &= ~MII_SR_LINK_STATUS;
}

static void
e1000_link_up(E1000State *s)
{
    s->mac_reg[STATUS] |= E1000_STATUS_LU;
    s->phy_reg[PHY_STATUS] |= MII_SR_LINK_STATUS;
}

static void
set_phy_ctrl(E1000State *s, int index, uint16_t val)
{
    /*
     * QEMU 1.3 does not support link auto-negotiation emulation, so if we
     * migrate during auto negotiation, after migration the link will be
     * down.
     */
    if (!(s->compat_flags & E1000_FLAG_AUTONEG)) {
        return;
    }
    if ((val & MII_CR_AUTO_NEG_EN) && (val & MII_CR_RESTART_AUTO_NEG)) {
        e1000_link_down(s);
        s->phy_reg[PHY_STATUS] &= ~MII_SR_AUTONEG_COMPLETE;
        DBGOUT(PHY, "Start link auto negotiation\n");
        qemu_mod_timer(s->autoneg_timer, qemu_get_clock_ms(vm_clock) + 500);
    }
}

static void
e1000_autoneg_timer(void *opaque)
{
    E1000State *s = opaque;
    if (!qemu_get_queue(s->nic)->link_down) {
        e1000_link_up(s);
    }
    s->phy_reg[PHY_STATUS] |= MII_SR_AUTONEG_COMPLETE;
    DBGOUT(PHY, "Auto negotiation is completed\n");
}

static void (*phyreg_writeops[])(E1000State *, int, uint16_t) = {
    [PHY_CTRL] = set_phy_ctrl,
};

enum { NPHYWRITEOPS = ARRAY_SIZE(phyreg_writeops) };

enum { PHY_R = 1, PHY_W = 2, PHY_RW = PHY_R | PHY_W };
static const char phy_regcap[0x20] = {
    [PHY_STATUS] = PHY_R,	[M88E1000_EXT_PHY_SPEC_CTRL] = PHY_RW,
    [PHY_ID1] = PHY_R,		[M88E1000_PHY_SPEC_CTRL] = PHY_RW,
    [PHY_CTRL] = PHY_RW,	[PHY_1000T_CTRL] = PHY_RW,
    [PHY_LP_ABILITY] = PHY_R,	[PHY_1000T_STATUS] = PHY_R,
    [PHY_AUTONEG_ADV] = PHY_RW,	[M88E1000_RX_ERR_CNTR] = PHY_R,
    [PHY_ID2] = PHY_R,		[M88E1000_PHY_SPEC_STATUS] = PHY_R
};

static const uint16_t phy_reg_init[] = {
    [PHY_CTRL] = 0x1140,
    [PHY_STATUS] = 0x794d, /* link initially up with not completed autoneg */
    [PHY_ID1] = 0x141,				[PHY_ID2] = PHY_ID2_INIT,
    [PHY_1000T_CTRL] = 0x0e00,			[M88E1000_PHY_SPEC_CTRL] = 0x360,
    [M88E1000_EXT_PHY_SPEC_CTRL] = 0x0d60,	[PHY_AUTONEG_ADV] = 0xde1,
    [PHY_LP_ABILITY] = 0x1e0,			[PHY_1000T_STATUS] = 0x3c00,
    [M88E1000_PHY_SPEC_STATUS] = 0xac00,
};

static const uint32_t mac_reg_init[] = {
    [PBA] =     0x00100030,
    [LEDCTL] =  0x602,
    [CTRL] =    E1000_CTRL_SWDPIN2 | E1000_CTRL_SWDPIN0 |
                E1000_CTRL_SPD_1000 | E1000_CTRL_SLU,
    [STATUS] =  0x80000000 | E1000_STATUS_GIO_MASTER_ENABLE |
                E1000_STATUS_ASDV | E1000_STATUS_MTXCKOK |
                E1000_STATUS_SPEED_1000 | E1000_STATUS_FD |
                E1000_STATUS_LU,
    [MANC] =    E1000_MANC_EN_MNG2HOST | E1000_MANC_RCV_TCO_EN |
                E1000_MANC_ARP_EN | E1000_MANC_0298_EN |
                E1000_MANC_RMCP_EN,
};

static void
set_interrupt_cause(E1000State *s, int index, uint32_t val)
{
    if (val && (E1000_DEVID >= E1000_DEV_ID_82547EI_MOBILE)) {
        /* Only for 8257x */
        val |= E1000_ICR_INT_ASSERTED;
    }
    s->mac_reg[ICR] = val;

    /*
     * Make sure ICR and ICS registers have the same value.
     * The spec says that the ICS register is write-only.  However in practice,
     * on real hardware ICS is readable, and for reads it has the same value as
     * ICR (except that ICS does not have the clear on read behaviour of ICR).
     *
     * The VxWorks PRO/1000 driver uses this behaviour.
     */
    s->mac_reg[ICS] = val;

    qemu_set_irq(s->dev.irq[0], (s->mac_reg[IMS] & s->mac_reg[ICR]) != 0);
}

static void
set_ics(E1000State *s, int index, uint32_t val)
{
    DBGOUT(INTERRUPT, "set_ics %x, ICR %x, IMR %x\n", val, s->mac_reg[ICR],
        s->mac_reg[IMS]);
    set_interrupt_cause(s, 0, val | s->mac_reg[ICR]);
}

static int
rxbufsize(uint32_t v)
{
    v &= E1000_RCTL_BSEX | E1000_RCTL_SZ_16384 | E1000_RCTL_SZ_8192 |
         E1000_RCTL_SZ_4096 | E1000_RCTL_SZ_2048 | E1000_RCTL_SZ_1024 |
         E1000_RCTL_SZ_512 | E1000_RCTL_SZ_256;
    switch (v) {
    case E1000_RCTL_BSEX | E1000_RCTL_SZ_16384:
        return 16384;
    case E1000_RCTL_BSEX | E1000_RCTL_SZ_8192:
        return 8192;
    case E1000_RCTL_BSEX | E1000_RCTL_SZ_4096:
        return 4096;
    case E1000_RCTL_SZ_1024:
        return 1024;
    case E1000_RCTL_SZ_512:
        return 512;
    case E1000_RCTL_SZ_256:
        return 256;
    }
    return 2048;
}

static void e1000_reset(void *opaque)
{
    E1000State *d = opaque;
    uint8_t *macaddr = d->conf.macaddr.a;
    int i;

    qemu_del_timer(d->autoneg_timer);
    memset(d->phy_reg, 0, sizeof d->phy_reg);
    memmove(d->phy_reg, phy_reg_init, sizeof phy_reg_init);
    memset(d->mac_reg, 0, sizeof d->mac_reg);
    memmove(d->mac_reg, mac_reg_init, sizeof mac_reg_init);
    d->rxbuf_min_shift = 1;
    memset(&d->tx, 0, sizeof d->tx);

    if (qemu_get_queue(d->nic)->link_down) {
        e1000_link_down(d);
    }

    /* Some guests expect pre-initialized RAH/RAL (AddrValid flag + MACaddr) */
    d->mac_reg[RA] = 0;
    d->mac_reg[RA + 1] = E1000_RAH_AV;
    for (i = 0; i < 4; i++) {
        d->mac_reg[RA] |= macaddr[i] << (8 * i);
        d->mac_reg[RA + 1] |= (i < 2) ? macaddr[i + 4] << (8 * i) : 0;
    }
}

static void
set_ctrl(E1000State *s, int index, uint32_t val)
{
    /* RST is self clearing */
    s->mac_reg[CTRL] = val & ~E1000_CTRL_RST;
}

static void
set_rx_control(E1000State *s, int index, uint32_t val)
{
    s->mac_reg[RCTL] = val;
    s->rxbuf_size = rxbufsize(val);
    s->rxbuf_min_shift = ((val / E1000_RCTL_RDMTS_QUAT) & 3) + 1;
    DBGOUT(RX, "RCTL: %d, mac_reg[RCTL] = 0x%x\n", s->mac_reg[RDT],
           s->mac_reg[RCTL]);
    qemu_flush_queued_packets(qemu_get_queue(s->nic));
}

static void
set_mdic(E1000State *s, int index, uint32_t val)
{
    uint32_t data = val & E1000_MDIC_DATA_MASK;
    uint32_t addr = ((val & E1000_MDIC_REG_MASK) >> E1000_MDIC_REG_SHIFT);

    if ((val & E1000_MDIC_PHY_MASK) >> E1000_MDIC_PHY_SHIFT != 1) // phy #
        val = s->mac_reg[MDIC] | E1000_MDIC_ERROR;
    else if (val & E1000_MDIC_OP_READ) {
        DBGOUT(MDIC, "MDIC read reg 0x%x\n", addr);
        if (!(phy_regcap[addr] & PHY_R)) {
            DBGOUT(MDIC, "MDIC read reg %x unhandled\n", addr);
            val |= E1000_MDIC_ERROR;
        } else
            val = (val ^ data) | s->phy_reg[addr];
    } else if (val & E1000_MDIC_OP_WRITE) {
        DBGOUT(MDIC, "MDIC write reg 0x%x, value 0x%x\n", addr, data);
        if (!(phy_regcap[addr] & PHY_W)) {
            DBGOUT(MDIC, "MDIC write reg %x unhandled\n", addr);
            val |= E1000_MDIC_ERROR;
        } else {
            if (addr < NPHYWRITEOPS && phyreg_writeops[addr]) {
                phyreg_writeops[addr](s, index, data);
            }
            s->phy_reg[addr] = data;
        }
    }
    s->mac_reg[MDIC] = val | E1000_MDIC_READY;

    if (val & E1000_MDIC_INT_EN) {
        set_ics(s, 0, E1000_ICR_MDAC);
    }
}

static uint32_t
get_eecd(E1000State *s, int index)
{
    uint32_t ret = E1000_EECD_PRES|E1000_EECD_GNT | s->eecd_state.old_eecd;

    DBGOUT(EEPROM, "reading eeprom bit %d (reading %d)\n",
           s->eecd_state.bitnum_out, s->eecd_state.reading);
    if (!s->eecd_state.reading ||
        ((s->eeprom_data[(s->eecd_state.bitnum_out >> 4) & 0x3f] >>
          ((s->eecd_state.bitnum_out & 0xf) ^ 0xf))) & 1)
        ret |= E1000_EECD_DO;
    return ret;
}

static void
set_eecd(E1000State *s, int index, uint32_t val)
{
    uint32_t oldval = s->eecd_state.old_eecd;

    s->eecd_state.old_eecd = val & (E1000_EECD_SK | E1000_EECD_CS |
            E1000_EECD_DI|E1000_EECD_FWE_MASK|E1000_EECD_REQ);
    if (!(E1000_EECD_CS & val))			// CS inactive; nothing to do
	return;
    if (E1000_EECD_CS & (val ^ oldval)) {	// CS rise edge; reset state
	s->eecd_state.val_in = 0;
	s->eecd_state.bitnum_in = 0;
	s->eecd_state.bitnum_out = 0;
	s->eecd_state.reading = 0;
    }
    if (!(E1000_EECD_SK & (val ^ oldval)))	// no clock edge
        return;
    if (!(E1000_EECD_SK & val)) {		// falling edge
        s->eecd_state.bitnum_out++;
        return;
    }
    s->eecd_state.val_in <<= 1;
    if (val & E1000_EECD_DI)
        s->eecd_state.val_in |= 1;
    if (++s->eecd_state.bitnum_in == 9 && !s->eecd_state.reading) {
        s->eecd_state.bitnum_out = ((s->eecd_state.val_in & 0x3f)<<4)-1;
        s->eecd_state.reading = (((s->eecd_state.val_in >> 6) & 7) ==
            EEPROM_READ_OPCODE_MICROWIRE);
    }
    DBGOUT(EEPROM, "eeprom bitnum in %d out %d, reading %d\n",
           s->eecd_state.bitnum_in, s->eecd_state.bitnum_out,
           s->eecd_state.reading);
}

static uint32_t
flash_eerd_read(E1000State *s, int x)
{
    unsigned int index, r = s->mac_reg[EERD] & ~E1000_EEPROM_RW_REG_START;

    if ((s->mac_reg[EERD] & E1000_EEPROM_RW_REG_START) == 0)
        return (s->mac_reg[EERD]);

    if ((index = r >> E1000_EEPROM_RW_ADDR_SHIFT) > EEPROM_CHECKSUM_REG)
        return (E1000_EEPROM_RW_REG_DONE | r);

    return ((s->eeprom_data[index] << E1000_EEPROM_RW_REG_DATA) |
           E1000_EEPROM_RW_REG_DONE | r);
}

static void
putsum(uint8_t *data, uint32_t n, uint32_t sloc, uint32_t css, uint32_t cse)
{
    uint32_t sum;

    if (cse && cse < n)
        n = cse + 1;
    if (sloc < n-1) {
        sum = net_checksum_add(n-css, data+css);
        cpu_to_be16wu((uint16_t *)(data + sloc),
                      net_checksum_finish(sum));
    }
}

static inline int
vlan_enabled(E1000State *s)
{
    return ((s->mac_reg[CTRL] & E1000_CTRL_VME) != 0);
}

static inline int
vlan_rx_filter_enabled(E1000State *s)
{
    return ((s->mac_reg[RCTL] & E1000_RCTL_VFE) != 0);
}

static inline int
is_vlan_packet(E1000State *s, const uint8_t *buf)
{
    return (be16_to_cpup((uint16_t *)(buf + 12)) ==
                le16_to_cpup((uint16_t *)(s->mac_reg + VET)));
}

static inline int
is_vlan_txd(uint32_t txd_lower)
{
    return ((txd_lower & E1000_TXD_CMD_VLE) != 0);
}

/* FCS aka Ethernet CRC-32. We don't get it from backends and can't
 * fill it in, just pad descriptor length by 4 bytes unless guest
 * told us to strip it off the packet. */
static inline int
fcs_len(E1000State *s)
{
    return (s->mac_reg[RCTL] & E1000_RCTL_SECRC) ? 0 : 4;
}

static void
e1000_send_packet(E1000State *s, const uint8_t *buf, int size)
{
    NetClientState *nc = qemu_get_queue(s->nic);
    if (s->phy_reg[PHY_CTRL] & MII_CR_LOOPBACK) {
        nc->info->receive(nc, buf, size);
    } else {
        qemu_send_packet(nc, buf, size);
    }
}

static void
xmit_seg(E1000State *s)
{
    uint16_t len, *sp;
    unsigned int frames = s->tx.tso_frames, css, sofar, n;
    struct e1000_tx *tp = &s->tx;

    if (tp->tse && tp->cptse) {
        css = tp->ipcss;
        DBGOUT(TXSUM, "frames %d size %d ipcss %d\n",
               frames, tp->size, css);
        if (tp->ip) {		// IPv4
            cpu_to_be16wu((uint16_t *)(tp->data+css+2),
                          tp->size - css);
            cpu_to_be16wu((uint16_t *)(tp->data+css+4),
                          be16_to_cpup((uint16_t *)(tp->data+css+4))+frames);
        } else			// IPv6
            cpu_to_be16wu((uint16_t *)(tp->data+css+4),
                          tp->size - css);
        css = tp->tucss;
        len = tp->size - css;
        DBGOUT(TXSUM, "tcp %d tucss %d len %d\n", tp->tcp, css, len);
        if (tp->tcp) {
            sofar = frames * tp->mss;
            cpu_to_be32wu((uint32_t *)(tp->data+css+4),	// seq
                be32_to_cpupu((uint32_t *)(tp->data+css+4))+sofar);
            if (tp->paylen - sofar > tp->mss)
                tp->data[css + 13] &= ~9;		// PSH, FIN
        } else	// UDP
            cpu_to_be16wu((uint16_t *)(tp->data+css+4), len);
        if (tp->sum_needed & E1000_TXD_POPTS_TXSM) {
            unsigned int phsum;
            // add pseudo-header length before checksum calculation
            sp = (uint16_t *)(tp->data + tp->tucso);
            phsum = be16_to_cpup(sp) + len;
            phsum = (phsum >> 16) + (phsum & 0xffff);
            cpu_to_be16wu(sp, phsum);
        }
        tp->tso_frames++;
    }

    if (tp->sum_needed & E1000_TXD_POPTS_TXSM)
        putsum(tp->data, tp->size, tp->tucso, tp->tucss, tp->tucse);
    if (tp->sum_needed & E1000_TXD_POPTS_IXSM)
        putsum(tp->data, tp->size, tp->ipcso, tp->ipcss, tp->ipcse);
    if (tp->vlan_needed) {
        memmove(tp->vlan, tp->data, 4);
        memmove(tp->data, tp->data + 4, 8);
        memcpy(tp->data + 8, tp->vlan_header, 4);
        e1000_send_packet(s, tp->vlan, tp->size + 4);
    } else
        e1000_send_packet(s, tp->data, tp->size);
    s->mac_reg[TPT]++;
    s->mac_reg[GPTC]++;
    n = s->mac_reg[TOTL];
    if ((s->mac_reg[TOTL] += s->tx.size) < n)
        s->mac_reg[TOTH]++;
}

static void
process_tx_desc(E1000State *s, struct e1000_tx_desc *dp)
{
    uint32_t txd_lower = le32_to_cpu(dp->lower.data);
    uint32_t dtype = txd_lower & (E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D);
    unsigned int split_size = txd_lower & 0xffff, bytes, sz, op;
    unsigned int msh = 0xfffff, hdr = 0;
    uint64_t addr;
    struct e1000_context_desc *xp = (struct e1000_context_desc *)dp;
    struct e1000_tx *tp = &s->tx;

    if (dtype == E1000_TXD_CMD_DEXT) {	// context descriptor
        op = le32_to_cpu(xp->cmd_and_length);
        tp->ipcss = xp->lower_setup.ip_fields.ipcss;
        tp->ipcso = xp->lower_setup.ip_fields.ipcso;
        tp->ipcse = le16_to_cpu(xp->lower_setup.ip_fields.ipcse);
        tp->tucss = xp->upper_setup.tcp_fields.tucss;
        tp->tucso = xp->upper_setup.tcp_fields.tucso;
        tp->tucse = le16_to_cpu(xp->upper_setup.tcp_fields.tucse);
        tp->paylen = op & 0xfffff;
        tp->hdr_len = xp->tcp_seg_setup.fields.hdr_len;
        tp->mss = le16_to_cpu(xp->tcp_seg_setup.fields.mss);
        tp->ip = (op & E1000_TXD_CMD_IP) ? 1 : 0;
        tp->tcp = (op & E1000_TXD_CMD_TCP) ? 1 : 0;
        tp->tse = (op & E1000_TXD_CMD_TSE) ? 1 : 0;
        tp->tso_frames = 0;
        if (tp->tucso == 0) {	// this is probably wrong
            DBGOUT(TXSUM, "TCP/UDP: cso 0!\n");
            tp->tucso = tp->tucss + (tp->tcp ? 16 : 6);
        }
        return;
    } else if (dtype == (E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D)) {
        // data descriptor
        if (tp->size == 0) {
            tp->sum_needed = le32_to_cpu(dp->upper.data) >> 8;
        }
        tp->cptse = ( txd_lower & E1000_TXD_CMD_TSE ) ? 1 : 0;
    } else {
        // legacy descriptor
        tp->cptse = 0;
    }

    if (vlan_enabled(s) && is_vlan_txd(txd_lower) &&
        (tp->cptse || txd_lower & E1000_TXD_CMD_EOP)) {
        tp->vlan_needed = 1;
        cpu_to_be16wu((uint16_t *)(tp->vlan_header),
                      le16_to_cpup((uint16_t *)(s->mac_reg + VET)));
        cpu_to_be16wu((uint16_t *)(tp->vlan_header + 2),
                      le16_to_cpu(dp->upper.fields.special));
    }
        
    addr = le64_to_cpu(dp->buffer_addr);
    if (tp->tse && tp->cptse) {
        hdr = tp->hdr_len;
        msh = hdr + tp->mss;
        do {
            bytes = split_size;
            if (tp->size + bytes > msh)
                bytes = msh - tp->size;

            bytes = MIN(sizeof(tp->data) - tp->size, bytes);
            pci_dma_read(&s->dev, addr, tp->data + tp->size, bytes);
            if ((sz = tp->size + bytes) >= hdr && tp->size < hdr)
                memmove(tp->header, tp->data, hdr);
            tp->size = sz;
            addr += bytes;
            if (sz == msh) {
                xmit_seg(s);
                memmove(tp->data, tp->header, hdr);
                tp->size = hdr;
            }
        } while (split_size -= bytes);
    } else if (!tp->tse && tp->cptse) {
        // context descriptor TSE is not set, while data descriptor TSE is set
        DBGOUT(TXERR, "TCP segmentation error\n");
    } else {
        split_size = MIN(sizeof(tp->data) - tp->size, split_size);
        pci_dma_read(&s->dev, addr, tp->data + tp->size, split_size);
        tp->size += split_size;
    }

    if (!(txd_lower & E1000_TXD_CMD_EOP))
        return;
    if (!(tp->tse && tp->cptse && tp->size < hdr))
        xmit_seg(s);
    tp->tso_frames = 0;
    tp->sum_needed = 0;
    tp->vlan_needed = 0;
    tp->size = 0;
    tp->cptse = 0;
}

static uint32_t
txdesc_writeback(E1000State *s, dma_addr_t base, struct e1000_tx_desc *dp)
{
    uint32_t txd_upper, txd_lower = le32_to_cpu(dp->lower.data);

    if (!(txd_lower & (E1000_TXD_CMD_RS|E1000_TXD_CMD_RPS)))
        return 0;
    txd_upper = (le32_to_cpu(dp->upper.data) | E1000_TXD_STAT_DD) &
                ~(E1000_TXD_STAT_EC | E1000_TXD_STAT_LC | E1000_TXD_STAT_TU);
    dp->upper.data = cpu_to_le32(txd_upper);
    pci_dma_write(&s->dev, base + ((char *)&dp->upper - (char *)dp),
                  &dp->upper, sizeof(dp->upper));
    return E1000_ICR_TXDW;
}

static uint64_t tx_desc_base(E1000State *s)
{
    uint64_t bah = s->mac_reg[TDBAH];
    uint64_t bal = s->mac_reg[TDBAL] & ~0xf;

    return (bah << 32) + bal;
}

static void
start_xmit(E1000State *s)
{
    dma_addr_t base;
    struct e1000_tx_desc desc;
    uint32_t tdh_start = s->mac_reg[TDH], cause = E1000_ICS_TXQE;

    if (!(s->mac_reg[TCTL] & E1000_TCTL_EN)) {
        DBGOUT(TX, "tx disabled\n");
        return;
    }

    while (s->mac_reg[TDH] != s->mac_reg[TDT]) {
        base = tx_desc_base(s) +
               sizeof(struct e1000_tx_desc) * s->mac_reg[TDH];
        pci_dma_read(&s->dev, base, &desc, sizeof(desc));

        DBGOUT(TX, "index %d: %p : %x %x\n", s->mac_reg[TDH],
               (void *)(intptr_t)desc.buffer_addr, desc.lower.data,
               desc.upper.data);

        process_tx_desc(s, &desc);
        cause |= txdesc_writeback(s, base, &desc);

        if (++s->mac_reg[TDH] * sizeof(desc) >= s->mac_reg[TDLEN])
            s->mac_reg[TDH] = 0;
        /*
         * the following could happen only if guest sw assigns
         * bogus values to TDT/TDLEN.
         * there's nothing too intelligent we could do about this.
         */
        if (s->mac_reg[TDH] == tdh_start) {
            DBGOUT(TXERR, "TDH wraparound @%x, TDT %x, TDLEN %x\n",
                   tdh_start, s->mac_reg[TDT], s->mac_reg[TDLEN]);
            break;
        }
    }
    set_ics(s, 0, cause);
}

static int
receive_filter(E1000State *s, const uint8_t *buf, int size)
{
    static const uint8_t bcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static const int mta_shift[] = {4, 3, 2, 0};
    uint32_t f, rctl = s->mac_reg[RCTL], ra[2], *rp;

    if (is_vlan_packet(s, buf) && vlan_rx_filter_enabled(s)) {
        uint16_t vid = be16_to_cpup((uint16_t *)(buf + 14));
        uint32_t vfta = le32_to_cpup((uint32_t *)(s->mac_reg + VFTA) +
                                     ((vid >> 5) & 0x7f));
        if ((vfta & (1 << (vid & 0x1f))) == 0)
            return 0;
    }

    if (rctl & E1000_RCTL_UPE)			// promiscuous
        return 1;

    if ((buf[0] & 1) && (rctl & E1000_RCTL_MPE))	// promiscuous mcast
        return 1;

    if ((rctl & E1000_RCTL_BAM) && !memcmp(buf, bcast, sizeof bcast))
        return 1;

    for (rp = s->mac_reg + RA; rp < s->mac_reg + RA + 32; rp += 2) {
        if (!(rp[1] & E1000_RAH_AV))
            continue;
        ra[0] = cpu_to_le32(rp[0]);
        ra[1] = cpu_to_le32(rp[1]);
        if (!memcmp(buf, (uint8_t *)ra, 6)) {
            DBGOUT(RXFILTER,
                   "unicast match[%d]: %02x:%02x:%02x:%02x:%02x:%02x\n",
                   (int)(rp - s->mac_reg - RA)/2,
                   buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
            return 1;
        }
    }
    DBGOUT(RXFILTER, "unicast mismatch: %02x:%02x:%02x:%02x:%02x:%02x\n",
           buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

    f = mta_shift[(rctl >> E1000_RCTL_MO_SHIFT) & 3];
    f = (((buf[5] << 8) | buf[4]) >> f) & 0xfff;
    if (s->mac_reg[MTA + (f >> 5)] & (1 << (f & 0x1f)))
        return 1;
    DBGOUT(RXFILTER,
           "dropping, inexact filter mismatch: %02x:%02x:%02x:%02x:%02x:%02x MO %d MTA[%d] %x\n",
           buf[0], buf[1], buf[2], buf[3], buf[4], buf[5],
           (rctl >> E1000_RCTL_MO_SHIFT) & 3, f >> 5,
           s->mac_reg[MTA + (f >> 5)]);

    return 0;
}

static void
e1000_set_link_status(NetClientState *nc)
{
    E1000State *s = qemu_get_nic_opaque(nc);
    uint32_t old_status = s->mac_reg[STATUS];

    if (nc->link_down) {
        e1000_link_down(s);
    } else {
        e1000_link_up(s);
    }

    if (s->mac_reg[STATUS] != old_status)
        set_ics(s, 0, E1000_ICR_LSC);
}

static bool e1000_has_rxbufs(E1000State *s, size_t total_size)
{
    int bufs;
    /* Fast-path short packets */
    if (total_size <= s->rxbuf_size) {
        return s->mac_reg[RDH] != s->mac_reg[RDT];
    }
    if (s->mac_reg[RDH] < s->mac_reg[RDT]) {
        bufs = s->mac_reg[RDT] - s->mac_reg[RDH];
    } else if (s->mac_reg[RDH] > s->mac_reg[RDT]) {
        bufs = s->mac_reg[RDLEN] /  sizeof(struct e1000_rx_desc) +
            s->mac_reg[RDT] - s->mac_reg[RDH];
    } else {
        return false;
    }
    return total_size <= bufs * s->rxbuf_size;
}

static int
e1000_can_receive(NetClientState *nc)
{
    E1000State *s = qemu_get_nic_opaque(nc);

    return (s->mac_reg[STATUS] & E1000_STATUS_LU) &&
        (s->mac_reg[RCTL] & E1000_RCTL_EN) && e1000_has_rxbufs(s, 1);
}

static uint64_t rx_desc_base(E1000State *s)
{
    uint64_t bah = s->mac_reg[RDBAH];
    uint64_t bal = s->mac_reg[RDBAL] & ~0xf;

    return (bah << 32) + bal;
}

static ssize_t
e1000_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    E1000State *s = qemu_get_nic_opaque(nc);
    struct e1000_rx_desc desc;
    dma_addr_t base;
    unsigned int n, rdt;
    uint32_t rdh_start;
    uint16_t vlan_special = 0;
    uint8_t vlan_status = 0, vlan_offset = 0;
    uint8_t min_buf[MIN_BUF_SIZE];
    size_t desc_offset;
    size_t desc_size;
    size_t total_size;

    if (!(s->mac_reg[STATUS] & E1000_STATUS_LU)) {
        return -1;
    }

    if (!(s->mac_reg[RCTL] & E1000_RCTL_EN)) {
        return -1;
    }

    /* Pad to minimum Ethernet frame length */
    if (size < sizeof(min_buf)) {
        memcpy(min_buf, buf, size);
        memset(&min_buf[size], 0, sizeof(min_buf) - size);
        buf = min_buf;
        size = sizeof(min_buf);
    }

    /* Discard oversized packets if !LPE and !SBP. */
    if ((size > MAXIMUM_ETHERNET_LPE_SIZE ||
        (size > MAXIMUM_ETHERNET_VLAN_SIZE
        && !(s->mac_reg[RCTL] & E1000_RCTL_LPE)))
        && !(s->mac_reg[RCTL] & E1000_RCTL_SBP)) {
        return size;
    }

    if (!receive_filter(s, buf, size))
        return size;

    if (vlan_enabled(s) && is_vlan_packet(s, buf)) {
        vlan_special = cpu_to_le16(be16_to_cpup((uint16_t *)(buf + 14)));
        memmove((uint8_t *)buf + 4, buf, 12);
        vlan_status = E1000_RXD_STAT_VP;
        vlan_offset = 4;
        size -= 4;
    }

    rdh_start = s->mac_reg[RDH];
    desc_offset = 0;
    total_size = size + fcs_len(s);
    if (!e1000_has_rxbufs(s, total_size)) {
            set_ics(s, 0, E1000_ICS_RXO);
            return -1;
    }
    do {
        desc_size = total_size - desc_offset;
        if (desc_size > s->rxbuf_size) {
            desc_size = s->rxbuf_size;
        }
        base = rx_desc_base(s) + sizeof(desc) * s->mac_reg[RDH];
        pci_dma_read(&s->dev, base, &desc, sizeof(desc));
        desc.special = vlan_special;
        desc.status |= (vlan_status | E1000_RXD_STAT_DD);
        if (desc.buffer_addr) {
            if (desc_offset < size) {
                size_t copy_size = size - desc_offset;
                if (copy_size > s->rxbuf_size) {
                    copy_size = s->rxbuf_size;
                }
                pci_dma_write(&s->dev, le64_to_cpu(desc.buffer_addr),
                              buf + desc_offset + vlan_offset, copy_size);
            }
            desc_offset += desc_size;
            desc.length = cpu_to_le16(desc_size);
            if (desc_offset >= total_size) {
                desc.status |= E1000_RXD_STAT_EOP | E1000_RXD_STAT_IXSM;
            } else {
                /* Guest zeroing out status is not a hardware requirement.
                   Clear EOP in case guest didn't do it. */
                desc.status &= ~E1000_RXD_STAT_EOP;
            }
        } else { // as per intel docs; skip descriptors with null buf addr
            DBGOUT(RX, "Null RX descriptor!!\n");
        }
        pci_dma_write(&s->dev, base, &desc, sizeof(desc));

        if (++s->mac_reg[RDH] * sizeof(desc) >= s->mac_reg[RDLEN])
            s->mac_reg[RDH] = 0;
        /* see comment in start_xmit; same here */
        if (s->mac_reg[RDH] == rdh_start) {
            DBGOUT(RXERR, "RDH wraparound @%x, RDT %x, RDLEN %x\n",
                   rdh_start, s->mac_reg[RDT], s->mac_reg[RDLEN]);
            set_ics(s, 0, E1000_ICS_RXO);
            return -1;
        }
    } while (desc_offset < total_size);

    s->mac_reg[GPRC]++;
    s->mac_reg[TPR]++;
    /* TOR - Total Octets Received:
     * This register includes bytes received in a packet from the <Destination
     * Address> field through the <CRC> field, inclusively.
     */
    n = s->mac_reg[TORL] + size + /* Always include FCS length. */ 4;
    if (n < s->mac_reg[TORL])
        s->mac_reg[TORH]++;
    s->mac_reg[TORL] = n;

    n = E1000_ICS_RXT0;
    if ((rdt = s->mac_reg[RDT]) < s->mac_reg[RDH])
        rdt += s->mac_reg[RDLEN] / sizeof(desc);
    if (((rdt - s->mac_reg[RDH]) * sizeof(desc)) <= s->mac_reg[RDLEN] >>
        s->rxbuf_min_shift)
        n |= E1000_ICS_RXDMT0;

    set_ics(s, 0, n);

    return size;
}

static uint32_t
mac_readreg(E1000State *s, int index)
{
    return s->mac_reg[index];
}

static uint32_t
mac_icr_read(E1000State *s, int index)
{
    uint32_t ret = s->mac_reg[ICR];

    DBGOUT(INTERRUPT, "ICR read: %x\n", ret);
    set_interrupt_cause(s, 0, 0);
    return ret;
}

static uint32_t
mac_read_clr4(E1000State *s, int index)
{
    uint32_t ret = s->mac_reg[index];

    s->mac_reg[index] = 0;
    return ret;
}

static uint32_t
mac_read_clr8(E1000State *s, int index)
{
    uint32_t ret = s->mac_reg[index];

    s->mac_reg[index] = 0;
    s->mac_reg[index-1] = 0;
    return ret;
}

static void
mac_writereg(E1000State *s, int index, uint32_t val)
{
    s->mac_reg[index] = val;
}

static void
set_rdt(E1000State *s, int index, uint32_t val)
{
    s->mac_reg[index] = val & 0xffff;
    if (e1000_has_rxbufs(s, 1)) {
        qemu_flush_queued_packets(qemu_get_queue(s->nic));
    }
}

static void
set_16bit(E1000State *s, int index, uint32_t val)
{
    s->mac_reg[index] = val & 0xffff;
}

static void
set_dlen(E1000State *s, int index, uint32_t val)
{
    s->mac_reg[index] = val & 0xfff80;
}

static void
set_tctl(E1000State *s, int index, uint32_t val)
{
    s->mac_reg[index] = val;
    s->mac_reg[TDT] &= 0xffff;
    start_xmit(s);
}

static void
set_icr(E1000State *s, int index, uint32_t val)
{
    DBGOUT(INTERRUPT, "set_icr %x\n", val);
    set_interrupt_cause(s, 0, s->mac_reg[ICR] & ~val);
}

static void
set_imc(E1000State *s, int index, uint32_t val)
{
    s->mac_reg[IMS] &= ~val;
    set_ics(s, 0, 0);
}

static void
set_ims(E1000State *s, int index, uint32_t val)
{
    s->mac_reg[IMS] |= val;
    set_ics(s, 0, 0);
}

#define getreg(x)	[x] = mac_readreg
static uint32_t (*macreg_readops[])(E1000State *, int) = {
    getreg(PBA),	getreg(RCTL),	getreg(TDH),	getreg(TXDCTL),
    getreg(WUFC),	getreg(TDT),	getreg(CTRL),	getreg(LEDCTL),
    getreg(MANC),	getreg(MDIC),	getreg(SWSM),	getreg(STATUS),
    getreg(TORL),	getreg(TOTL),	getreg(IMS),	getreg(TCTL),
    getreg(RDH),	getreg(RDT),	getreg(VET),	getreg(ICS),
    getreg(TDBAL),	getreg(TDBAH),	getreg(RDBAH),	getreg(RDBAL),
    getreg(TDLEN),	getreg(RDLEN),

    [TOTH] = mac_read_clr8,	[TORH] = mac_read_clr8,	[GPRC] = mac_read_clr4,
    [GPTC] = mac_read_clr4,	[TPR] = mac_read_clr4,	[TPT] = mac_read_clr4,
    [ICR] = mac_icr_read,	[EECD] = get_eecd,	[EERD] = flash_eerd_read,
    [CRCERRS ... MPC] = &mac_readreg,
    [RA ... RA+31] = &mac_readreg,
    [MTA ... MTA+127] = &mac_readreg,
    [VFTA ... VFTA+127] = &mac_readreg,
};
enum { NREADOPS = ARRAY_SIZE(macreg_readops) };

#define putreg(x)	[x] = mac_writereg
static void (*macreg_writeops[])(E1000State *, int, uint32_t) = {
    putreg(PBA),	putreg(EERD),	putreg(SWSM),	putreg(WUFC),
    putreg(TDBAL),	putreg(TDBAH),	putreg(TXDCTL),	putreg(RDBAH),
    putreg(RDBAL),	putreg(LEDCTL), putreg(VET),
    [TDLEN] = set_dlen,	[RDLEN] = set_dlen,	[TCTL] = set_tctl,
    [TDT] = set_tctl,	[MDIC] = set_mdic,	[ICS] = set_ics,
    [TDH] = set_16bit,	[RDH] = set_16bit,	[RDT] = set_rdt,
    [IMC] = set_imc,	[IMS] = set_ims,	[ICR] = set_icr,
    [EECD] = set_eecd,	[RCTL] = set_rx_control, [CTRL] = set_ctrl,
    [RA ... RA+31] = &mac_writereg,
    [MTA ... MTA+127] = &mac_writereg,
    [VFTA ... VFTA+127] = &mac_writereg,
};

enum { NWRITEOPS = ARRAY_SIZE(macreg_writeops) };

static void
e1000_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                 unsigned size)
{
    E1000State *s = opaque;
    unsigned int index = (addr & 0x1ffff) >> 2;

    if (index < NWRITEOPS && macreg_writeops[index]) {
        macreg_writeops[index](s, index, val);
    } else if (index < NREADOPS && macreg_readops[index]) {
        DBGOUT(MMIO, "e1000_mmio_writel RO %x: 0x%04"PRIx64"\n", index<<2, val);
    } else {
        DBGOUT(UNKNOWN, "MMIO unknown write addr=0x%08x,val=0x%08"PRIx64"\n",
               index<<2, val);
    }
}

static uint64_t
e1000_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    E1000State *s = opaque;
    unsigned int index = (addr & 0x1ffff) >> 2;

    if (index < NREADOPS && macreg_readops[index])
    {
        return macreg_readops[index](s, index);
    }
    DBGOUT(UNKNOWN, "MMIO unknown read addr=0x%08x\n", index<<2);
    return 0;
}

static const MemoryRegionOps e1000_mmio_ops = {
    .read = e1000_mmio_read,
    .write = e1000_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t e1000_io_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    E1000State *s = opaque;

    (void)s;
    return 0;
}

static void e1000_io_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    E1000State *s = opaque;

    (void)s;
}

static const MemoryRegionOps e1000_io_ops = {
    .read = e1000_io_read,
    .write = e1000_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static bool is_version_1(void *opaque, int version_id)
{
    return version_id == 1;
}

static void e1000_pre_save(void *opaque)
{
    E1000State *s = opaque;
    NetClientState *nc = qemu_get_queue(s->nic);

    if (!(s->compat_flags & E1000_FLAG_AUTONEG)) {
        return;
    }

    /*
     * If link is down and auto-negotiation is ongoing, complete
     * auto-negotiation immediately.  This allows is to look at
     * MII_SR_AUTONEG_COMPLETE to infer link status on load.
     */
    if (nc->link_down &&
        s->phy_reg[PHY_CTRL] & MII_CR_AUTO_NEG_EN &&
        s->phy_reg[PHY_CTRL] & MII_CR_RESTART_AUTO_NEG) {
         s->phy_reg[PHY_STATUS] |= MII_SR_AUTONEG_COMPLETE;
    }
}

static int e1000_post_load(void *opaque, int version_id)
{
    E1000State *s = opaque;
    NetClientState *nc = qemu_get_queue(s->nic);

    /* nc.link_down can't be migrated, so infer link_down according
     * to link status bit in mac_reg[STATUS].
     * Alternatively, restart link negotiation if it was in progress. */
    nc->link_down = (s->mac_reg[STATUS] & E1000_STATUS_LU) == 0;

    if (!(s->compat_flags & E1000_FLAG_AUTONEG)) {
        return 0;
    }

    if (s->phy_reg[PHY_CTRL] & MII_CR_AUTO_NEG_EN &&
        s->phy_reg[PHY_CTRL] & MII_CR_RESTART_AUTO_NEG &&
        !(s->phy_reg[PHY_STATUS] & MII_SR_AUTONEG_COMPLETE)) {
        nc->link_down = false;
        qemu_mod_timer(s->autoneg_timer, qemu_get_clock_ms(vm_clock) + 500);
    }

    return 0;
}

static const VMStateDescription vmstate_e1000 = {
    .name = "e1000",
    .version_id = 2,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .pre_save = e1000_pre_save,
    .post_load = e1000_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_PCI_DEVICE(dev, E1000State),
        VMSTATE_UNUSED_TEST(is_version_1, 4), /* was instance id */
        VMSTATE_UNUSED(4), /* Was mmio_base.  */
        VMSTATE_UINT32(rxbuf_size, E1000State),
        VMSTATE_UINT32(rxbuf_min_shift, E1000State),
        VMSTATE_UINT32(eecd_state.val_in, E1000State),
        VMSTATE_UINT16(eecd_state.bitnum_in, E1000State),
        VMSTATE_UINT16(eecd_state.bitnum_out, E1000State),
        VMSTATE_UINT16(eecd_state.reading, E1000State),
        VMSTATE_UINT32(eecd_state.old_eecd, E1000State),
        VMSTATE_UINT8(tx.ipcss, E1000State),
        VMSTATE_UINT8(tx.ipcso, E1000State),
        VMSTATE_UINT16(tx.ipcse, E1000State),
        VMSTATE_UINT8(tx.tucss, E1000State),
        VMSTATE_UINT8(tx.tucso, E1000State),
        VMSTATE_UINT16(tx.tucse, E1000State),
        VMSTATE_UINT32(tx.paylen, E1000State),
        VMSTATE_UINT8(tx.hdr_len, E1000State),
        VMSTATE_UINT16(tx.mss, E1000State),
        VMSTATE_UINT16(tx.size, E1000State),
        VMSTATE_UINT16(tx.tso_frames, E1000State),
        VMSTATE_UINT8(tx.sum_needed, E1000State),
        VMSTATE_INT8(tx.ip, E1000State),
        VMSTATE_INT8(tx.tcp, E1000State),
        VMSTATE_BUFFER(tx.header, E1000State),
        VMSTATE_BUFFER(tx.data, E1000State),
        VMSTATE_UINT16_ARRAY(eeprom_data, E1000State, 64),
        VMSTATE_UINT16_ARRAY(phy_reg, E1000State, 0x20),
        VMSTATE_UINT32(mac_reg[CTRL], E1000State),
        VMSTATE_UINT32(mac_reg[EECD], E1000State),
        VMSTATE_UINT32(mac_reg[EERD], E1000State),
        VMSTATE_UINT32(mac_reg[GPRC], E1000State),
        VMSTATE_UINT32(mac_reg[GPTC], E1000State),
        VMSTATE_UINT32(mac_reg[ICR], E1000State),
        VMSTATE_UINT32(mac_reg[ICS], E1000State),
        VMSTATE_UINT32(mac_reg[IMC], E1000State),
        VMSTATE_UINT32(mac_reg[IMS], E1000State),
        VMSTATE_UINT32(mac_reg[LEDCTL], E1000State),
        VMSTATE_UINT32(mac_reg[MANC], E1000State),
        VMSTATE_UINT32(mac_reg[MDIC], E1000State),
        VMSTATE_UINT32(mac_reg[MPC], E1000State),
        VMSTATE_UINT32(mac_reg[PBA], E1000State),
        VMSTATE_UINT32(mac_reg[RCTL], E1000State),
        VMSTATE_UINT32(mac_reg[RDBAH], E1000State),
        VMSTATE_UINT32(mac_reg[RDBAL], E1000State),
        VMSTATE_UINT32(mac_reg[RDH], E1000State),
        VMSTATE_UINT32(mac_reg[RDLEN], E1000State),
        VMSTATE_UINT32(mac_reg[RDT], E1000State),
        VMSTATE_UINT32(mac_reg[STATUS], E1000State),
        VMSTATE_UINT32(mac_reg[SWSM], E1000State),
        VMSTATE_UINT32(mac_reg[TCTL], E1000State),
        VMSTATE_UINT32(mac_reg[TDBAH], E1000State),
        VMSTATE_UINT32(mac_reg[TDBAL], E1000State),
        VMSTATE_UINT32(mac_reg[TDH], E1000State),
        VMSTATE_UINT32(mac_reg[TDLEN], E1000State),
        VMSTATE_UINT32(mac_reg[TDT], E1000State),
        VMSTATE_UINT32(mac_reg[TORH], E1000State),
        VMSTATE_UINT32(mac_reg[TORL], E1000State),
        VMSTATE_UINT32(mac_reg[TOTH], E1000State),
        VMSTATE_UINT32(mac_reg[TOTL], E1000State),
        VMSTATE_UINT32(mac_reg[TPR], E1000State),
        VMSTATE_UINT32(mac_reg[TPT], E1000State),
        VMSTATE_UINT32(mac_reg[TXDCTL], E1000State),
        VMSTATE_UINT32(mac_reg[WUFC], E1000State),
        VMSTATE_UINT32(mac_reg[VET], E1000State),
        VMSTATE_UINT32_SUB_ARRAY(mac_reg, E1000State, RA, 32),
        VMSTATE_UINT32_SUB_ARRAY(mac_reg, E1000State, MTA, 128),
        VMSTATE_UINT32_SUB_ARRAY(mac_reg, E1000State, VFTA, 128),
        VMSTATE_END_OF_LIST()
    }
};

static const uint16_t e1000_eeprom_template[64] = {
    0x0000, 0x0000, 0x0000, 0x0000,      0xffff, 0x0000,      0x0000, 0x0000,
    0x3000, 0x1000, 0x6403, E1000_DEVID, 0x8086, E1000_DEVID, 0x8086, 0x3040,
    0x0008, 0x2000, 0x7e14, 0x0048,      0x1000, 0x00d8,      0x0000, 0x2700,
    0x6cc9, 0x3150, 0x0722, 0x040b,      0x0984, 0x0000,      0xc000, 0x0706,
    0x1008, 0x0000, 0x0f04, 0x7fff,      0x4d01, 0xffff,      0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff,      0xffff, 0xffff,      0xffff, 0xffff,
    0x0100, 0x4000, 0x121c, 0xffff,      0xffff, 0xffff,      0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff,      0xffff, 0xffff,      0xffff, 0x0000,
};

/* PCI interface */

static void
e1000_mmio_setup(E1000State *d)
{
    int i;
    const uint32_t excluded_regs[] = {
        E1000_MDIC, E1000_ICR, E1000_ICS, E1000_IMS,
        E1000_IMC, E1000_TCTL, E1000_TDT, PNPMMIO_SIZE
    };

    memory_region_init_io(&d->mmio, &e1000_mmio_ops, d, "e1000-mmio",
                          PNPMMIO_SIZE);
    memory_region_add_coalescing(&d->mmio, 0, excluded_regs[0]);
    for (i = 0; excluded_regs[i] != PNPMMIO_SIZE; i++)
        memory_region_add_coalescing(&d->mmio, excluded_regs[i] + 4,
                                     excluded_regs[i+1] - excluded_regs[i] - 4);
    memory_region_init_io(&d->io, &e1000_io_ops, d, "e1000-io", IOPORT_SIZE);
}

static void
e1000_cleanup(NetClientState *nc)
{
    E1000State *s = qemu_get_nic_opaque(nc);

    s->nic = NULL;
}

static NetClientInfo net_e1000_info = {
    .type = NET_CLIENT_OPTIONS_KIND_NIC,
    .size = sizeof(NICState),
    .can_receive = e1000_can_receive,
    .receive = e1000_receive,
    .cleanup = e1000_cleanup,
    .link_status_changed = e1000_set_link_status,
};

#endif /* E1000_IMPL */

static void
pci_external_uninit(PCIDevice *dev)
{
    ExternalPCIState *d = DO_UPCAST(ExternalPCIState, dev, dev);
    BARInfo *bar_info = d->bar_info;
    int i;

    for (i = 0; i < PCI_NUM_BARS; ++i) {
        memory_region_destroy(&bar_info[i].region);
        qemu_unmap_file_data(&bar_info[i].file_data);
    }

#ifdef E1000_IMPL
    qemu_del_timer(d->autoneg_timer);
    qemu_free_timer(d->autoneg_timer);
#endif /* E1000_IMPL */
    qemu_del_nic(d->nic);
}

static uint32_t
external_pci_config_read(PCIDevice *pci_dev, uint32_t address, int len)
{
    DBGOUT(GENERAL, "external_pci_config_read addr=0x%X len=%d",
           address, len);
    /* TODO: call the peer. */
    return 0;
}

static uint16_t
retrieve_pci_command(PCIDevice *pci_dev)
{
    return (uint16_t)external_pci_config_read(pci_dev, PCI_COMMAND, 2);
}

static uint32_t
retrieve_pci_bar(PCIDevice *pci_dev, int index)
{
    return external_pci_config_read(pci_dev, PCI_BASE_ADDRESS_0 + index * 4, 4);
}

static pcibus_t
retrieve_pci_rom_address(PCIDevice *pci_dev)
{
    uint32_t address = external_pci_config_read(pci_dev, PCI_ROM_ADDRESS, 4);
    if ((address & PCI_ROM_ADDRESS_ENABLE) == 0) {
        return PCI_BAR_UNMAPPED;
    }
    return address & PCI_ROM_ADDRESS_MASK;
}

static pcibus_t
retrieve_pci_base_address(PCIDevice *pci_dev, int index, uint16_t pci_command,
                          bool *is_io, bool *is_64bit)
{
    bool memory_enabled = (pci_command & PCI_COMMAND_MEMORY) != 0;
    bool io_enabled = (pci_command & PCI_COMMAND_IO) != 0;
    uint32_t address;

    if (!memory_enabled && !io_enabled) {
        return PCI_BAR_UNMAPPED;
    }
    address = retrieve_pci_bar(pci_dev, index);
    *is_io = ((address & PCI_BASE_ADDRESS_SPACE_IO) != 0);
    if (*is_io) {
        if (!io_enabled) {
            return PCI_BAR_UNMAPPED;
        }
        *is_64bit = false;
        return address & PCI_BASE_ADDRESS_IO_MASK;
    } else {
        if (!memory_enabled) {
            return PCI_BAR_UNMAPPED;
        }
        *is_64bit = ((address & PCI_BASE_ADDRESS_MEM_TYPE_64) != 0);
        address = address & PCI_BASE_ADDRESS_MEM_MASK;
        if (*is_64bit) {
            return ((pcibus_t)retrieve_pci_bar(pci_dev, index + 1) << 32) +
                address;
        } else {
            return address;
        }
    }
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
        memory_region_add_subregion_overlap(r->address_space,
                                            r->addr, r->memory, 1);
    }
}

static void pci_update_bar_mapping(PCIDevice *d, uint16_t pci_command,
                                   int index)
{
    bool is_io, is_64bit;
    PCIIORegion *r = &d->io_regions[index];

    /* this region isn't registered */
    if (!r->size) {
        return;
    }

    pci_update_region_mapping(d, r,
                              retrieve_pci_base_address(d, index, pci_command,
                                                        &is_io, &is_64bit));
}

static void pci_update_rom_mapping(PCIDevice *d)
{
    PCIIORegion *r = &d->io_regions[PCI_NUM_BARS];

    /* this region isn't registered */
    if (!r->size) {
        return;
    }

    pci_update_region_mapping(d, r, retrieve_pci_rom_address(d));
}

static void pci_update_all_bar_mappings(PCIDevice *d, uint16_t pci_command)
{
    int i;

    for(i = 0; i < PCI_NUM_REGIONS; i++) {
        pci_update_bar_mapping(d, pci_command, i);
    }
    pci_update_rom_mapping(d);
}

static void
external_pci_config_write(PCIDevice *pci_dev, uint32_t addr, uint32_t val,
                          int l)
{
    bool covers_pci_command = range_covers_byte(addr, l, PCI_COMMAND);
#if 0
    bool was_irq_disabled = 0;
    if (covers_pci_command) {
        was_irq_disabled =
            (retrieve_pci_command(pci_dev) & PCI_COMMAND_INTX_DISABLE) != 0;
    }
#endif

    /* TODO: call the peer HERE. */

    if (covers_pci_command) {
        uint16_t pci_command = retrieve_pci_command(pci_dev);
        pci_update_all_bar_mappings(pci_dev, pci_command);
#if 0
        pci_update_irq_disabled(d, was_irq_disabled);
#endif
        memory_region_set_enabled(&pci_dev->bus_master_enable_region,
                                  pci_command & PCI_COMMAND_MASTER);
    } else if (addr >= PCI_BASE_ADDRESS_0) {
        int index = (addr - PCI_BASE_ADDRESS_0) / 4;
        if (index < PCI_NUM_BARS) {
            pci_update_bar_mapping(pci_dev, retrieve_pci_command(pci_dev),
                                   index);
        } else if (index == (PCI_ROM_ADDRESS - PCI_BASE_ADDRESS_0) / 4) {
            pci_update_rom_mapping(pci_dev);
        }
    }

    /* TODO: handle MSI. */
    /* TODO: handle MSIX. */
}

static inline uint64_t
external_pci_read_direct(void *opaque, hwaddr addr, unsigned size)
{
    uint8_t *pointer = opaque;
    pointer += addr;

    DBGOUT(GENERAL, "external_pci_read_direct addr=0x%llX size=%d",
           (unsigned long long)addr, size);

    switch (size) {
    case 1:
        return *pointer;
    case 2:
        return *(uint16_t *)pointer;
    case 4:
        return *(uint32_t *)pointer;
    case 8:
        return *(uint64_t *)pointer;
    default:
        abort();
    }

    return 0;
}

static void
external_pci_write_direct(void *opaque, hwaddr addr, uint64_t val,
                          unsigned size)
{
    uint8_t *pointer = opaque;
    pointer += addr;
    
    DBGOUT(GENERAL, "external_pci_write_direct addr=0x%llX val=0x%llX size=%d",
           (unsigned long long)addr, (unsigned long long)val, size);

    switch (size) {
    case 1:
        *pointer = (uint8_t)val;
        break;
    case 2:
        *(uint16_t *)pointer = (uint16_t)val;
        break;
    case 4:
        *(uint32_t *)pointer = (uint32_t)val;
        break;
    case 8:
        *(uint64_t *)pointer = (uint64_t)val;
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
        /* TODO: call the peer. */
        bar_info->need_flush = false;
    }

    return external_pci_read_direct(bar_info->file_data.pointer, addr, size);
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
    /* TODO: call the peer one-way. */
    bar_info->need_flush = true;
}

static uint64_t
external_pci_read_mmio(void *opaque, hwaddr addr, unsigned size)
{
    BARInfo *bar_info = opaque;
    (void)bar_info;

    DBGOUT(GENERAL, "external_pci_read_mmio addr=0x%llX size=%d",
           (unsigned long long)addr, size);

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
    /* TODO: call the peer. */

    return 0;
}

static void
external_pci_write_mmio(void *opaque, hwaddr addr, uint64_t val,
                        unsigned size)
{
    BARInfo *bar_info = opaque;
    (void)bar_info;
    
    DBGOUT(GENERAL, "external_pci_write_mmio addr=0x%llX val=0x%llX size=%d",
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
    /* TODO: call the peer one-way. */
}

static uint64_t
external_pci_read_io(void *opaque, hwaddr addr, unsigned size)
{
    BARInfo *bar_info = opaque;
    (void)bar_info;

    DBGOUT(GENERAL, "external_pci_read_io addr=0x%llX size=%d",
           (unsigned long long)addr, size);

    switch (size) {
    case 1:
        break;
    case 2:
        break;
    case 4:
        break;
    default:
        abort();
    }
    /* TODO: call the peer. */

    return 0;
}

static void
external_pci_write_io(void *opaque, hwaddr addr, uint64_t val,
                        unsigned size)
{
    BARInfo *bar_info = opaque;
    (void)bar_info;
    
    DBGOUT(GENERAL, "external_pci_write_io addr=0x%llX val=0x%llX size=%d",
           (unsigned long long)addr, (unsigned long long)val, size);

    switch (size) {
    case 1:
        break;
    case 2:
        break;
    case 4:
        break;
    default:
        abort();
    }
    /* TODO: call the peer. */
}

static int pci_external_init(PCIDevice *pci_dev)
{
    ExternalPCIState *d = DO_UPCAST(ExternalPCIState, dev, pci_dev);
    MemoryRegion *address_space_memory = pci_address_space(pci_dev);
    MemoryRegion *address_space_io = pci_address_space_io(pci_dev);
    BARInfo *bar_info = d->bar_info;
    PCIIORegion *io_region = pci_dev->io_regions;
    bool upper_bar = false;
    int i;

    for (i = 0; i < PCI_NUM_BARS; ++i, ++bar_info, ++io_region) {
        bar_update_size(bar_info);
        if (upper_bar) {
            upper_bar = false;
            if (bar_info->flags != 0) {
                /* Report error. */
                return -1;
            }
            if (bar_size(bar_info) != 0) {
                /* Report error. */
                return -1;
            }
        } else if (bar_size(bar_info) == 0) {
            if (bar_info->flags == 0) {
                continue;
            } else {
                /* Report error. */
                return -1;
            }
        }

        if (!bar_size_power_of_two(bar_info)) {
            /* Report error. */
            return -1;
        }

        bar_update_endianness(bar_info, d);
        
        if ((bar_info->name == NULL) || (*bar_info->name == '\0')) {
            bar_info->name = g_strdup_printf("%s-bar%d", pci_dev->name, i);
        }
        
        if (bar_is_io(bar_info)) {
            if (bar_is_ram(bar_info)) {
                /* Report error. */
                return -1;
            }
            if (bar_prefetchable(bar_info)) {
                /* Report error. */
                return -1;
            }
            if (bar_is_64bit(bar_info)) {
                /* Report error. */
                return -1;
            }
            io_region->type = PCI_BASE_ADDRESS_SPACE_IO;
            io_region->address_space = address_space_io;
        } else {
            io_region->type = PCI_BASE_ADDRESS_SPACE_MEMORY;
            if (bar_is_64bit(bar_info)) {
                if (i + 1 >= PCI_NUM_BARS) {
                    /* Report error. */
                    return -1;
                }
                upper_bar = true;
                io_region->type |= PCI_BASE_ADDRESS_MEM_TYPE_64;
            } else {
                if (bar_size(bar_info) > 0xFFFFFFFFu) {
                    /* Report error. */
                    return -1;
                }
                io_region->type |= PCI_BASE_ADDRESS_MEM_TYPE_32;
            }

            if (bar_prefetchable(bar_info) || bar_is_ram(bar_info)) {
                if(bar_file(bar_info) &&
                   !qemu_map_file_data(&bar_info->file_data)) {
                    DBGOUT(GENERAL, "PCI bar configuration failed");
                    /* Report error. */
                    return -1;
                }
                io_region->type |= PCI_BASE_ADDRESS_MEM_PREFETCH;
            }
            io_region->address_space = address_space_memory;
        }

        io_region->addr = PCI_BAR_UNMAPPED;
        io_region->size = bar_size(bar_info);
        io_region->memory = &bar_info->region;

        if (bar_is_ram(bar_info) && !is_wrong_endian(bar_endianness(bar_info))) {
            memory_region_init_ram_ptr(&bar_info->region,
                                       bar_info->name, io_region->size,
                                       bar_info->file_data.pointer);
        } else {
            void *opaque = bar_info;
            MemoryRegionOps *ops = &bar_info->ops;
            memset(ops, 0, sizeof(MemoryRegionOps));

            ops->endianness = bar_endianness(bar_info);
            
            if (bar_is_io(bar_info)) {
                ops->valid.min_access_size = 4;
                ops->valid.max_access_size = 4;
            } else if(bar_is_ram(bar_info) ||
                      bar_prefetchable(bar_info)) {
                ops->valid.min_access_size = 1;
                ops->valid.max_access_size = 8;
            }

            if (bar_is_io(bar_info)) {
                ops->read = external_pci_read_io;
                ops->write = external_pci_write_io;
            } else if (bar_is_ram(bar_info) && bar_file(bar_info)) {
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

            memory_region_init_io(&bar_info->region, ops,
                                  opaque, bar_info->name, io_region->size);
        }

        if (bar_is_ram(bar_info)) {
            memory_region_set_coalescing(&bar_info->region);
        }
    }

    return 0;
}

static void qdev_external_pci_reset(DeviceState *dev)
{
#ifdef E1000_IMPL
    ExternalPCIState *d = DO_UPCAST(ExternalPCIState, dev.qdev, dev);
    e1000_reset(d);
#endif /* E1000_IMPL */
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

#ifdef E1000_IMPL
    DEFINE_PROP_BIT("autonegotiation", ExternalPCIState,
                    compat_flags, E1000_FLAG_AUTONEG_BIT, true),
#endif /* E1000_IMPL */
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
    /* dc->desc = "Intel Gigabit Ethernet"; */ /* Should read 'desc' property. */
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
