// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Copyright (c) 2021 StarFive Technology Co., Ltd. */

#include <common.h>
#include <asm/sbi.h>
#include <dm.h>

#define SBI_EXT_CACHE	0x0900067e

enum sbi_ext_cache_fid {
	SBI_EXT_BASE_L2_FLUSH = 0,
	SBI_EXT_BASE_L2_INVALIDATE,
};

enum e_cache_mode {
	CACHE_MODE_COHERENT = 0,
	CACHE_MODE_CMO,
	CACHE_MODE_SBI_L2,
	CACHE_MODE_UNKNOWN,
};

static enum e_cache_mode cache_mode = CACHE_MODE_UNKNOWN;
static u32 cbom_block_size = CONFIG_SYS_CACHELINE_SIZE;

static bool is_noncoherent(void)
{
	ofnode node;

	node = ofnode_path("/soc");
	if (!ofnode_valid(node))
		return false;

	return ofnode_read_bool(node, "dma-noncoherent");
}

static bool is_need_l2_cache_ops(void)
{
	ofnode node;

	node = ofnode_by_compatible(ofnode_null(), "starfive,dubhe-l2cache");

	return ofnode_valid(node);
}

static void cbom_get_block_size(void)
{
	static struct device_node *node;
	u32 size;

	node = of_find_node_by_path("/cpus/cpu@0");
	if (!node)
		return;

	if (of_read_u32(node, "riscv,cbom-block-size", &size)) {
		debug("%s: could not get %s\n",
		      node->full_name, "riscv,cbom-block-size");
		return;
	}

	cbom_block_size = size;
}

static void cmo_clean(unsigned long start, unsigned long end)
{
	unsigned long addr;

	end = ALIGN(end, cbom_block_size);

	for (addr = start; addr < end; addr += cbom_block_size) {
		__asm__ __volatile__ (".insn i 0xf, 2, x0, %0, 1\t\n"
					: /* Outputs */
					: /* Inputs  */ "r"(addr)
					/* : No clobber */);
	}
}

static void cmo_inval(unsigned long start, unsigned long end)
{
	unsigned long addr;

	end = ALIGN(end, cbom_block_size);

	for (addr = start; addr < end; addr += cbom_block_size) {
		__asm__ __volatile__ (".insn i 0xf, 2, x0, %0, 0\t\n"
					: /* Outputs */
					: /* Inputs  */ "r"(addr)
					/* : No clobber */);
	}
}

static void get_cache_mode(void)
{
	if (cache_mode != CACHE_MODE_UNKNOWN)
		return;

	if (is_noncoherent()) {
		if (is_need_l2_cache_ops()) {
			cache_mode = CACHE_MODE_SBI_L2;
		} else {
			cache_mode = CACHE_MODE_CMO;
			cbom_get_block_size();
		}
	} else {
		cache_mode = CACHE_MODE_COHERENT;
	}

	printf("Dubhe cache mode: %d\n", cache_mode);

	return;
}

void enable_caches(void)
{
	get_cache_mode();
}

void flush_dcache_range(unsigned long start, unsigned long end)
{
	if (cache_mode == CACHE_MODE_SBI_L2)
		sbi_ecall(SBI_EXT_CACHE, SBI_EXT_BASE_L2_FLUSH, start,
			  end - start, 0, 0, 0, 0);
	else if (cache_mode == CACHE_MODE_CMO)
		cmo_clean(start, end);
}

void invalidate_dcache_range(unsigned long start, unsigned long end)
{
	if (cache_mode == CACHE_MODE_SBI_L2)
		sbi_ecall(SBI_EXT_CACHE, SBI_EXT_BASE_L2_INVALIDATE, start,
			  end - start, 0, 0, 0, 0);
	else if (cache_mode == CACHE_MODE_CMO)
		cmo_inval(start, end);
}
