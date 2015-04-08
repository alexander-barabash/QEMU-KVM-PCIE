#include "exec/memory-page.h"
#include "sysemu/kvm.h"


void mem_page_setup(MemoryPage *page, AddressSpace *as, uint64_t addr,
                    uint32_t len, bool is_write)
{
    do {
        if (!page->valid) {
            break;
        }
        if (page->as != as) {
            break;
        }
        if (page->addr != addr) {
            break;
        }
        if (page->len < len) {
            break;
        }
        return;
    } while (false);

    mem_page_invalidate(page);

    if (address_space_access_valid(as, addr, len, is_write)) {
        page->as = as;
        page->addr = addr;
        page->len = len;
        page->is_write = is_write;
        page->valid = true;
    }
}

uint8_t *mem_subpage_map(MemorySubPage *subpage,
                         MemoryPage *page, uint32_t shift, hwaddr len)
{
    if ((subpage->page != page) ||
        (subpage->shift != shift) ||
        (subpage->len < len)) {
        mem_subpage_clear(subpage);
    }

    if (!page->valid) {
        return NULL;
    }
    if (len == 0) {
        return NULL;
    }
    if (len > page->len) {
        return NULL;
    }
    if (len + shift > page->len) {
        return NULL;
    }
    
    subpage->page = page;
    subpage->shift = shift;
    subpage->len = len;
    return mem_subpage_remap(subpage);
}

uint8_t *mem_subpage_remap(MemorySubPage *subpage)
{
    MemoryPage *page = subpage->page;
    uint32_t shift = subpage->shift;
    hwaddr len = subpage->len;

    if (!page) {
        return NULL;
    }
    if (!subpage->ptr) {
        subpage->ptr = address_space_map(page->as, page->addr + shift, &len,
                                         page->is_write);
        if (subpage->ptr) {
            if (len < subpage->len) {
                address_space_unmap(page->as, subpage->ptr, len, page->is_write,
                                    0);
                subpage->ptr = NULL;
            }
            subpage->len = len;
        }
    }
    return subpage->ptr;
}

void mem_subpage_unmap_full(MemorySubPage *subpage, uint32_t access_len)
{
    MemoryPage *page = subpage->page;
    if (!page) {
        return;
    }
    if (!page->valid) {
        return;
    }
    if (!subpage->ptr) {
        return;
    }
    address_space_unmap(page->as, subpage->ptr, subpage->len, page->is_write,
                        access_len);
    subpage->ptr = NULL;
}

/* Returns true on error. */
bool mem_subpage_rw(MemorySubPage *subpage, uint32_t shift,
                    uint8_t *buf, uint32_t len, bool is_write)
{
    MemoryPage *page = subpage->page;

    shift += subpage->shift;

    if (page &&
        page->valid &&
        len <= page->len &&
        len + shift <= page->len) {

        // DMA Memory barrier
        if (kvm_enabled()) {
            smp_mb();
        }

        return address_space_rw(page->as, page->addr + shift, buf, len,
                                is_write);
    } else {
        return true;
    }
}

