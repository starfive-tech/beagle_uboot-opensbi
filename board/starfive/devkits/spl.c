// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022-2023 StarFive Technology Co., Ltd.
 * Author:	yanhong <yanhong.wang@starfivetech.com>
 *
 */

#include <common.h>
#include <init.h>
#include <asm/arch/spl.h>
#include <asm/io.h>
#include <asm/arch/gpio.h>
#include <asm/arch/jh7110-regs.h>
#include <asm/arch/clk.h>
#include <image.h>
#include <log.h>
#include <spl.h>

#define MODE_SELECT_REG		0x1702002c

DECLARE_GLOBAL_DATA_PTR;

int spl_board_init_f(void)
{
	int ret;

	ret = spl_soc_init();
	if (ret) {
		debug("JH7110 SPL init failed: %d\n", ret);
		return ret;
	}

	return 0;
}

u32 spl_boot_device(void)
{
	int boot_mode = 0;

	boot_mode = readl((const volatile void *)MODE_SELECT_REG) & 0x3;
	switch (boot_mode) {
	case 0:
		return BOOT_DEVICE_SPI;
	case 1:
		return BOOT_DEVICE_MMC2;
	case 2:
		return BOOT_DEVICE_MMC1;
	case 3:
		return BOOT_DEVICE_UART;
	default:
		debug("Unsupported boot device 0x%x.\n",
		      boot_mode);
		return BOOT_DEVICE_NONE;
	}
}

struct image_header *spl_get_load_buffer(ssize_t offset, size_t size)
{
	return (struct image_header *)(STARFIVE_SPL_BOOT_LOAD_ADDR);
}

void board_init_f(ulong dummy)
{
	int ret;

	/* Adjust cpu frequency, the default is 1.0GHz */
	starfive_jh7110_pll_set_rate(PLL0, 1000000000);

	/*change pll2 to 1188MHz*/
	starfive_jh7110_pll_set_rate(PLL2, 1188000000);

	/*DDR control depend clk init*/
	clrsetbits_le32(SYS_CRG_BASE, CLK_CPU_ROOT_SW_MASK,
		BIT(CLK_CPU_ROOT_SW_SHIFT) & CLK_CPU_ROOT_SW_MASK);

	clrsetbits_le32(SYS_CRG_BASE + CLK_BUS_ROOT_OFFSET,
		CLK_BUS_ROOT_SW_MASK,
		BIT(CLK_BUS_ROOT_SW_SHIFT) & CLK_BUS_ROOT_SW_MASK);

	/*Set clk_perh_root clk default mux sel to pll2*/
	clrsetbits_le32(SYS_CRG_BASE + CLK_PERH_ROOT_OFFSET,
		CLK_PERH_ROOT_MASK,
		BIT(CLK_PERH_ROOT_SHIFT) & CLK_PERH_ROOT_MASK);

	clrsetbits_le32(SYS_CRG_BASE + CLK_NOC_BUS_STG_AXI_OFFSET,
		CLK_NOC_BUS_STG_AXI_EN_MASK,
		BIT(CLK_NOC_BUS_STG_AXI_EN_SHIFT)
		& CLK_NOC_BUS_STG_AXI_EN_MASK);

	clrsetbits_le32(AON_CRG_BASE + CLK_AON_APB_FUNC_OFFSET,
		CLK_AON_APB_FUNC_SW_MASK,
		BIT(CLK_AON_APB_FUNC_SW_SHIFT) & CLK_AON_APB_FUNC_SW_MASK);

	/* switch qspi clk to pll0 */
	clrsetbits_le32(SYS_CRG_BASE + CLK_QSPI_REF_OFFSET,
			CLK_QSPI_REF_SW_MASK,
			BIT(CLK_QSPI_REF_SW_SHIFT) & CLK_QSPI_REF_SW_MASK);

	/*set GPIO to 3.3v*/
	setbits_le32(SYS_SYSCON_BASE + 0xC, 0x0);

	/* Improved GMAC0 TX I/O PAD capability */
	clrsetbits_le32(AON_IOMUX_BASE + 0x78, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(AON_IOMUX_BASE + 0x7c, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(AON_IOMUX_BASE + 0x80, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(AON_IOMUX_BASE + 0x84, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(AON_IOMUX_BASE + 0x88, 0x3, BIT(0) & 0x3);

	/* Improved GMAC1 TX I/O PAD capability */
	clrsetbits_le32(SYS_IOMUX_BASE + 0x26c, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(SYS_IOMUX_BASE + 0x270, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(SYS_IOMUX_BASE + 0x274, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(SYS_IOMUX_BASE + 0x278, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(SYS_IOMUX_BASE + 0x27c, 0x3, BIT(0) & 0x3);

	SYS_IOMUX_DOEN(62, LOW);
	SYS_IOMUX_DOUT(62, 19);
	SYS_IOMUX_SET_DS(64, 2);
	SYS_IOMUX_SET_SLEW(64, 1);
	SYS_IOMUX_SET_DS(65, 1);
	SYS_IOMUX_SET_DS(66, 1);
	SYS_IOMUX_SET_DS(67, 1);
	SYS_IOMUX_SET_DS(68, 1);
	SYS_IOMUX_SET_DS(69, 1);
	SYS_IOMUX_SET_DS(70, 1);
	SYS_IOMUX_SET_DS(71, 1);
	SYS_IOMUX_SET_DS(72, 1);
	SYS_IOMUX_SET_DS(73, 1);

	SYS_IOMUX_DOEN(10, LOW);
	SYS_IOMUX_DOUT(10, 55);
	SYS_IOMUX_SET_SLEW(10, 1);
	SYS_IOMUX_SET_DS(10, 2);
	SYS_IOMUX_COMPLEX(9, 44, 57, 19);
	SYS_IOMUX_SET_DS(9, 1);
	SYS_IOMUX_COMPLEX(11, 45, 58, 20);
	SYS_IOMUX_SET_DS(11, 1);
	SYS_IOMUX_COMPLEX(12, 46, 59, 21);
	SYS_IOMUX_SET_DS(12, 1);
	SYS_IOMUX_COMPLEX(7, 47, 60, 22);
	SYS_IOMUX_SET_DS(7, 1);
	SYS_IOMUX_COMPLEX(8, 48, 61, 23);
	SYS_IOMUX_SET_DS(8, 1);

	ret = spl_early_init();
	if (ret)
		panic("spl_early_init() failed: %d\n", ret);

	arch_cpu_init_dm();

	preloader_console_init();

	ret = spl_board_init_f();
	if (ret) {
		debug("spl_board_init_f init failed: %d\n", ret);
		return;
	}
}

#ifdef CONFIG_SPL_LOAD_FIT
int board_fit_config_name_match(const char *name)
{
	/* boot using first FIT config */
	return 0;
}
#endif

static void spl_enable_uart2(void)
{
	/* uart2 clock */
	setbits_le32(SYS_CRG_BASE + CLK_UART2_APB_OFFSET, BIT(31));
	setbits_le32(SYS_CRG_BASE + CLK_UART2_CORE_OFFSET, BIT(31));
	clrsetbits_le32(SYS_CRG_BASE + CLK_RSTN_3_OFFSET, BIT(23) | BIT(24), 0);

	/*uart2 tx*/
	SYS_IOMUX_DOEN(45, LOW);
	SYS_IOMUX_DOUT(45, 0x4f);
	SYS_IOMUX_SET_DS(45, 3);
	/*uart2 rx*/
	SYS_IOMUX_DOEN(46, HIGH);
	SYS_IOMUX_DOUT(46, 0);
	SYS_IOMUX_DIN(46, 62);
}

void spl_perform_fixups(struct spl_image_info *spl_image)
{
	unsigned long rtos_offset, rtos_image_addr;
	unsigned long rtos_base;

	rtos_base = fdtdec_get_config_int(gd->fdt_blob,
					  "amp,rtos-code-base", 0);
	rtos_offset = fdtdec_get_config_int(gd->fdt_blob,
					    "amp,rtos-offset", 0);

	if (rtos_base && rtos_offset) {
		spl_enable_uart2();
		rtos_image_addr = CONFIG_SPL_OPENSBI_LOAD_ADDR + rtos_offset;
		memcpy((void *)rtos_base, (void *)(rtos_image_addr),
		       spl_image->size - rtos_offset);
	}
}
