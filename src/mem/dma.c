#include "dma.h"

#include "pmm.h"

void* dma_alloc_page(void) {
    return pmm_alloc();
}

void dma_free_page(void* page) {
    if (!page) return;
    pmm_free(page);
}
