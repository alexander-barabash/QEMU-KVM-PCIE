/*
 * Physical memory pages management API
 *
 * Copyright 2014 Mentor Graphics Corp.
 *
 * Authors:
 *  Alexander Barabash <alexander_barabash@mentor.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef MEMORY_PAGE_H
#define MEMORY_PAGE_H

#include "exec/memory.h"

struct MemoryPage;
struct MemorySubPage;

typedef struct MemorySubPage {
    struct MemoryPage *page;
    uint32_t shift;
    uint32_t len;
    uint8_t *ptr;
} MemorySubPage;

typedef struct MemoryPage {
    AddressSpace *as;
    uint64_t addr;
    uint32_t len;
    bool is_write;
    bool valid;
    struct MemorySubPage full_subpage;
} MemoryPage;

typedef struct MemoryPageVector {
    int n_pages;
    MemoryPage **pages;
} MemoryPageVector;

static inline bool mem_page_valid(MemoryPage *page)
{
    return page->valid;
}

static inline uint64_t mem_subpage_addr(MemorySubPage *subpage)
{
    if (subpage->page && subpage->page->valid) {
        return subpage->page->addr + subpage->shift;
    } else {
        return 0;
    }
}

void mem_page_setup(MemoryPage *page, AddressSpace *as, uint64_t addr,
                    uint32_t len, bool is_write);

static inline void mem_page_init(MemoryPage *page, AddressSpace *as,
                                 uint64_t addr, uint32_t len, bool is_write)
{
    page->valid = false;
    page->full_subpage.page = NULL;
    page->full_subpage.ptr = NULL;
    mem_page_setup(page, as, addr, len, is_write);
}

static inline MemoryPage *mem_page_create(AddressSpace *as,
                                          uint64_t addr, uint32_t len, bool is_write)
{
    MemoryPage *page = g_malloc(sizeof(*page));
    mem_page_init(page, as, addr, len, is_write);
    return page;
}

uint8_t *mem_subpage_map(MemorySubPage *subpage,
                         MemoryPage *page, uint32_t shift, hwaddr len);
uint8_t *mem_subpage_remap(MemorySubPage *subpage);
void mem_subpage_unmap_full(MemorySubPage *subpage, uint32_t access_len);

static inline uint8_t *mem_subpage_init(MemorySubPage *subpage,
                                        MemoryPage *page, uint32_t shift,
                                        hwaddr len)
{
    subpage->page = NULL;
    subpage->ptr = NULL;
    return mem_subpage_map(subpage, page, shift, len);
}

static inline void mem_subpage_unmap(MemorySubPage *subpage)
{
    mem_subpage_unmap_full(subpage, subpage->len);
}

static inline void mem_subpage_clear(MemorySubPage *subpage)
{
    mem_subpage_unmap(subpage);
    subpage->page = NULL;
}

static inline uint8_t *mem_page_map(MemoryPage *page)
{
    return mem_subpage_map(&page->full_subpage, page, 0, page->len);
}

static inline uint8_t *mem_page_remap(MemoryPage *page)
{
    return mem_subpage_remap(&page->full_subpage);
}

static inline void mem_page_unmap_full(MemoryPage *page, uint32_t access_len)
{
    mem_subpage_unmap_full(&page->full_subpage, access_len);
}

static inline void mem_page_unmap(MemoryPage *page)
{
    mem_page_unmap_full(page, page->len);
}

static inline void mem_page_invalidate(MemoryPage *page)
{
    mem_page_unmap(page);
    page->valid = false;
}

/* Returns true on error. */
bool mem_subpage_rw(MemorySubPage *subpage, uint32_t shift,
                    uint8_t *buf, uint32_t len, bool is_write);

/* Returns true on error. */
static inline bool mem_subpage_read(MemorySubPage *subpage, uint32_t shift,
                                    uint8_t *buf, uint32_t len)
{
    return mem_subpage_rw(subpage, shift, buf, len, false);
}

/* Returns true on error. */
static inline bool mem_subpage_write(MemorySubPage *subpage, uint32_t shift,
                                     uint8_t *buf, uint32_t len)
{
    return mem_subpage_rw(subpage, shift, buf, len, true);
}

static inline uint8_t mem_subpage_read8(MemorySubPage *subpage, uint32_t shift)
{
    uint8_t result;
    if (!mem_subpage_read(subpage, shift, &result, 1)) {
        return result;
    } else {
        return 0;
    }
}

static inline uint16_t mem_subpage_read16(MemorySubPage *subpage,
                                          uint32_t shift)
{
    union {
        uint16_t v16;
        uint8_t v8[2];
    } result;
    if (!mem_subpage_read(subpage, shift, &result.v8[0], 2)) {
        return result.v16;
    } else {
        return 0;
    }
}

static inline uint32_t mem_subpage_read32(MemorySubPage *subpage,
                                          uint32_t shift)
{
    union {
        uint32_t v32;
        uint8_t v8[4];
    } result;
    if (!mem_subpage_read(subpage, shift, &result.v8[0], 4)) {
        return result.v32;
    } else {
        return 0;
    }
}

static inline uint64_t mem_subpage_read64(MemorySubPage *subpage,
                                          uint32_t shift)
{
    union {
        uint64_t v64;
        uint8_t v8[8];
    } result;
    if (!mem_subpage_read(subpage, shift, &result.v8[0], 8)) {
        return result.v64;
    } else {
        return 0;
    }
}

/* Returns true on error. */
static inline bool mem_subpage_write8(MemorySubPage *subpage, uint32_t shift,
                                  uint8_t value)
{
    return mem_subpage_write(subpage, shift, &value, 1);
}

/* Returns true on error. */
static inline bool mem_subpage_write16(MemorySubPage *subpage, uint32_t shift,
                                   uint16_t value)
{
    union {
        uint16_t v16;
        uint8_t v8[2];
    } v;
    v.v16 = value;
    return mem_subpage_write(subpage, shift, &v.v8[0], 2);
}

/* Returns true on error. */
static inline bool mem_subpage_write32(MemorySubPage *subpage, uint32_t shift,
                                       uint32_t value)
{
    union {
        uint32_t v32;
        uint8_t v8[4];
    } v;
    v.v32 = value;
    return mem_subpage_write(subpage, shift, &v.v8[0], 4);
}

/* Returns true on error. */
static inline bool mem_subpage_write64(MemorySubPage *subpage, uint32_t shift,
                                       uint64_t value)
{
    union {
        uint64_t v64;
        uint8_t v8[8];
    } v;
    v.v64 = value;
    return mem_subpage_write(subpage, shift, &v.v8[0], 8);
}

static inline void mem_page_vector_init(MemoryPageVector *v)
{
    v->n_pages = 0;
    v->pages = NULL;
}

static inline void mem_page_vector_reset(MemoryPageVector *v)
{
    int i;
    for (i = 0; i < v->n_pages; ++i) {
        MemoryPage *page = v->pages[i];
        if (page) {
            mem_page_invalidate(page);
            g_free(page);
        }
    }
    g_free(v->pages);
    mem_page_vector_init(v);
}

#endif /* MEMORY_PAGE_H */
