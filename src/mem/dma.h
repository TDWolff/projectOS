#ifndef DMA_H
#define DMA_H

#include "../include/types.h"

// Very small DMA helper layer.
// For now this is just a thin wrapper over PMM so drivers allocate
// physical, page-aligned memory suitable for bus-master DMA.
//
// Later you can extend this to:
// - map/unmap for non-identity-mapped kernels
// - support contiguous multi-page allocations
// - bounce buffers

void* dma_alloc_page(void);
void dma_free_page(void* page);

#endif
