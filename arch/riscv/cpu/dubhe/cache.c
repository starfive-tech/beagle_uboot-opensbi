// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Copyright (c) 2021 StarFive Technology Co., Ltd. */

#include <common.h>
#include <asm/sbi.h>

#define SBI_EXT_CACHE	0x0900067e

enum sbi_ext_cache_fid {
	SBI_EXT_BASE_L2_FLUSH = 0,
	SBI_EXT_BASE_L2_INVALIDATE,
};

void flush_dcache_range(unsigned long start, unsigned long end)
{
	sbi_ecall(SBI_EXT_CACHE, SBI_EXT_BASE_L2_FLUSH, start, end - start, 0, 0, 0, 0);
}

void invalidate_dcache_range(unsigned long start, unsigned long end)
{
	sbi_ecall(SBI_EXT_CACHE, SBI_EXT_BASE_L2_INVALIDATE, start, end - start, 0, 0, 0, 0);
}
