/*
 * Copyright (C) 2017 SiFive
 *   Wesley Terpstra <wesley@sifive.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 */

#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>
#include <linux/scatterlist.h>
#include <linux/swiotlb.h>

static void *dma_riscv_alloc(struct device *dev, size_t size,
                            dma_addr_t *dma_handle, gfp_t gfp,
                            unsigned long attrs)
{
	// If the device cannot address ZONE_NORMAL, allocate from ZONE_DMA
	gfp &= ~(__GFP_DMA | __GFP_DMA32 | __GFP_HIGHMEM);
	if (IS_ENABLED(CONFIG_ZONE_DMA32) &&
	    (dev == NULL || dev->coherent_dma_mask <= DMA_BIT_MASK(32)))
	{
		gfp |= __GFP_DMA32;
	}

	if (dev_get_cma_area(dev) && gfpflags_allow_blocking(gfp)) {
		void* addr;
		struct page *page;
		page = dma_alloc_from_contiguous(dev, size >> PAGE_SHIFT,
				get_order(size), gfp);
		if (!page)
		{
			pr_err("Unabled to allocated from contiguous memory\n");
			return NULL;
		}

		*dma_handle = phys_to_dma(dev, page_to_phys(page));
		addr = page_address(page);
		memset(addr, 0, size);
		return addr;
	} else {
		return swiotlb_alloc_coherent(dev, size, dma_handle, gfp);
	}
}

static void dma_riscv_free(struct device *dev, size_t size,
                          void *cpu_addr, dma_addr_t dma_addr,
                          unsigned long attrs)
{
	bool freed;
	phys_addr_t paddr = dma_to_phys(dev, dma_addr);

	freed = dma_release_from_contiguous(dev, 
			phys_to_page(paddr), 
			size >> PAGE_SHIFT);
 	if(!freed) {
		swiotlb_free_coherent(dev, size, cpu_addr, dma_addr);
	}
	
	return;
}

static int dma_riscv_supported(struct device *dev, u64 mask)
{
	/* Work-around for broken PCIe controllers */
	if (IS_ENABLED(CONFIG_PCI_DMA_32) && mask > DMA_BIT_MASK(32))
		return 0;

	return swiotlb_dma_supported(dev, mask);
}

const struct dma_map_ops dma_riscv_ops = {
	.alloc			= dma_riscv_alloc,
	.free			= dma_riscv_free,
	.dma_supported		= dma_riscv_supported,
	.map_page		= swiotlb_map_page,
	.map_sg			= swiotlb_map_sg_attrs,
	.unmap_page		= swiotlb_unmap_page,
	.unmap_sg		= swiotlb_unmap_sg_attrs,
	.sync_single_for_cpu	= swiotlb_sync_single_for_cpu,
	.sync_single_for_device	= swiotlb_sync_single_for_device,
	.sync_sg_for_cpu	= swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device	= swiotlb_sync_sg_for_device,
	.mapping_error		= swiotlb_dma_mapping_error,
};

EXPORT_SYMBOL(dma_riscv_ops);
