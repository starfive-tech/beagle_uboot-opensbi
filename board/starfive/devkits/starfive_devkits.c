// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022-2023 StarFive Technology Co., Ltd.
 * Author:	yanhong <yanhong.wang@starfivetech.com>
 *
 */

#include <common.h>
#include <asm/io.h>
#include <asm/arch/jh7110-regs.h>
#include <asm/gpio.h>
#include <cpu_func.h>
#include <dm/uclass.h>
#include <dm/device.h>
#include <env.h>
#include <i2c.h>
#include <inttypes.h>
#include <misc.h>
#include <linux/bitops.h>
#include <asm/arch/gpio.h>
#include <bmp_logo.h>
#include <video.h>
#include <splash.h>
#include <asm/gpio.h>
#include <linux/err.h>


#define SYS_CLOCK_ENABLE(clk) \
	setbits_le32(SYS_CRG_BASE + clk, CLK_ENABLE_MASK)

DECLARE_GLOBAL_DATA_PTR;

#define CPU_VOL_BINNING_OFFSET 0x7fc
enum {
	BOOT_FLASH =	0,
	BOOT_SD,
	BOOT_EMMC,
	BOOT_UART,
};
enum cpu_voltage_type_t {
	CPU_VOL_1020 = 0xef0,
	CPU_VOL_1040 = 0xfff,
	CPU_VOL_1060 = 0xff0,
	CPU_VOL_1000  = 0x8f0,
};
#define CPU_VOL_MASK	0xfff

static void sys_reset_clear(ulong assert, ulong status, u32 rst)
{
	u32 value;

	clrbits_le32(SYS_CRG_BASE + assert, BIT(rst));
	do {
		value = in_le32(SYS_CRG_BASE + status);
	} while ((value & BIT(rst)) != BIT(rst));
}

static void jh7110_timer_init(void)
{
	SYS_CLOCK_ENABLE(TIMER_CLK_APB_SHIFT);
	SYS_CLOCK_ENABLE(TIMER_CLK_TIMER0_SHIFT);
	SYS_CLOCK_ENABLE(TIMER_CLK_TIMER1_SHIFT);
	SYS_CLOCK_ENABLE(TIMER_CLK_TIMER2_SHIFT);
	SYS_CLOCK_ENABLE(TIMER_CLK_TIMER3_SHIFT);

	sys_reset_clear(SYS_CRG_RESET_ASSERT3_SHIFT,
			SYS_CRG_RESET_STATUS3_SHIFT, TIMER_RSTN_APB_SHIFT);
	sys_reset_clear(SYS_CRG_RESET_ASSERT3_SHIFT,
			SYS_CRG_RESET_STATUS3_SHIFT, TIMER_RSTN_TIMER0_SHIFT);
	sys_reset_clear(SYS_CRG_RESET_ASSERT3_SHIFT,
			SYS_CRG_RESET_STATUS3_SHIFT, TIMER_RSTN_TIMER1_SHIFT);
	sys_reset_clear(SYS_CRG_RESET_ASSERT3_SHIFT,
			SYS_CRG_RESET_STATUS3_SHIFT, TIMER_RSTN_TIMER2_SHIFT);
	sys_reset_clear(SYS_CRG_RESET_ASSERT3_SHIFT,
			SYS_CRG_RESET_STATUS3_SHIFT, TIMER_RSTN_TIMER3_SHIFT);
}

static void jh7110_i2c_init (int id)
{
	switch (id) {
	case 5:
		//scl
		SYS_IOMUX_COMPLEX(19, 79, 0, 42);
		//sda
		SYS_IOMUX_COMPLEX(20, 80, 0, 43);

		break;

	default:
		break;
	}
}

static void jh7110_gmac_sel_tx_to_rgmii(int id)
{
	switch (id) {
	case 0:
		clrsetbits_le32(AON_CRG_BASE + GMAC5_0_CLK_TX_SHIFT,
		GMAC5_0_CLK_TX_MASK,
		BIT(GMAC5_0_CLK_TX_BIT) & GMAC5_0_CLK_TX_MASK);
		break;
	case 1:
		clrsetbits_le32(SYS_CRG_BASE + GMAC5_1_CLK_TX_SHIFT,
		GMAC5_1_CLK_TX_MASK,
		BIT(GMAC5_1_CLK_TX_BIT) & GMAC5_1_CLK_TX_MASK);
		break;
	default:
		break;
	}
}

static void jh7110_gmac_io_pad(int id)
{
	u32 cap = BIT(0); /* 2.5V */

	switch (id) {
	case 0:
		/* Improved GMAC0 TX I/O PAD capability */
		clrsetbits_le32(AON_IOMUX_BASE + 0x78, 0x3, cap & 0x3);
		clrsetbits_le32(AON_IOMUX_BASE + 0x7c, 0x3, cap & 0x3);
		clrsetbits_le32(AON_IOMUX_BASE + 0x80, 0x3, cap & 0x3);
		clrsetbits_le32(AON_IOMUX_BASE + 0x84, 0x3, cap & 0x3);
		clrsetbits_le32(AON_IOMUX_BASE + 0x88, 0x3, cap & 0x3);
		break;
	case 1:
		/* Improved GMAC1 TX I/O PAD capability */
		clrsetbits_le32(SYS_IOMUX_BASE + 0x26c, 0x3, cap & 0x3);
		clrsetbits_le32(SYS_IOMUX_BASE + 0x270, 0x3, cap & 0x3);
		clrsetbits_le32(SYS_IOMUX_BASE + 0x274, 0x3, cap & 0x3);
		clrsetbits_le32(SYS_IOMUX_BASE + 0x278, 0x3, cap & 0x3);
		clrsetbits_le32(SYS_IOMUX_BASE + 0x27c, 0x3, cap & 0x3);
		break;
	}
}

static void jh7110_gmac_init(int id)
{
	jh7110_gmac_sel_tx_to_rgmii(id);
	jh7110_gmac_io_pad(id);
}

static void jh7110_usb_init(bool usb2_enable)
{
	if (usb2_enable) {
		/*usb 2.0 utmi phy init*/
		clrsetbits_le32(STG_SYSCON_BASE  + STG_SYSCON_4,
			USB_MODE_STRAP_MASK,
			(2<<USB_MODE_STRAP_SHIFT) &
			USB_MODE_STRAP_MASK);/*2:host mode, 4:device mode*/
		clrsetbits_le32(STG_SYSCON_BASE + STG_SYSCON_4,
			USB_OTG_SUSPENDM_BYPS_MASK,
			BIT(USB_OTG_SUSPENDM_BYPS_SHIFT)
			& USB_OTG_SUSPENDM_BYPS_MASK);
		clrsetbits_le32(STG_SYSCON_BASE + STG_SYSCON_4,
			USB_OTG_SUSPENDM_MASK,
			BIT(USB_OTG_SUSPENDM_SHIFT) &
			USB_OTG_SUSPENDM_MASK);/*HOST = 1. DEVICE = 0;*/
		clrsetbits_le32(STG_SYSCON_BASE + STG_SYSCON_4,
			USB_PLL_EN_MASK,
			BIT(USB_PLL_EN_SHIFT) & USB_PLL_EN_MASK);
		clrsetbits_le32(STG_SYSCON_BASE + STG_SYSCON_4,
			USB_REFCLK_MODE_MASK,
			BIT(USB_REFCLK_MODE_SHIFT) & USB_REFCLK_MODE_MASK);
		/* usb 2.0 phy mode,REPLACE USB3.0 PHY module = 1;else = 0*/
		clrsetbits_le32(SYS_SYSCON_BASE + SYS_SYSCON_24,
			PDRSTN_SPLIT_MASK,
			BIT(PDRSTN_SPLIT_SHIFT) &
			PDRSTN_SPLIT_MASK);
	} else {
		/*usb 3.0 pipe phy config*/
		clrsetbits_le32(STG_SYSCON_BASE  + STG_SYSCON_196,
			PCIE_CKREF_SRC_MASK,
			(0<<PCIE_CKREF_SRC_SHIFT) & PCIE_CKREF_SRC_MASK);
		clrsetbits_le32(STG_SYSCON_BASE  + STG_SYSCON_196,
			PCIE_CLK_SEL_MASK,
			(0<<PCIE_CLK_SEL_SHIFT) & PCIE_CLK_SEL_MASK);
		clrsetbits_le32(STG_SYSCON_BASE  + STG_SYSCON_328,
			PCIE_PHY_MODE_MASK,
			BIT(PCIE_PHY_MODE_SHIFT) & PCIE_PHY_MODE_MASK);
		clrsetbits_le32(STG_SYSCON_BASE  + STG_SYSCON_500,
			PCIE_USB3_BUS_WIDTH_MASK,
			(0 << PCIE_USB3_BUS_WIDTH_SHIFT) &
			PCIE_USB3_BUS_WIDTH_MASK);
		clrsetbits_le32(STG_SYSCON_BASE  + STG_SYSCON_500,
			PCIE_USB3_RATE_MASK,
			(0 << PCIE_USB3_RATE_SHIFT) & PCIE_USB3_RATE_MASK);
		clrsetbits_le32(STG_SYSCON_BASE  + STG_SYSCON_500,
			PCIE_USB3_RX_STANDBY_MASK,
			(0 << PCIE_USB3_RX_STANDBY_SHIFT)
			& PCIE_USB3_RX_STANDBY_MASK);
		clrsetbits_le32(STG_SYSCON_BASE  + STG_SYSCON_500,
			PCIE_USB3_PHY_ENABLE_MASK,
			BIT(PCIE_USB3_PHY_ENABLE_SHIFT)
			& PCIE_USB3_PHY_ENABLE_MASK);

		/* usb 3.0 phy mode,REPLACE USB3.0 PHY module = 1;else = 0*/
		clrsetbits_le32(SYS_SYSCON_BASE + SYS_SYSCON_24,
			PDRSTN_SPLIT_MASK,
			(0 << PDRSTN_SPLIT_SHIFT) & PDRSTN_SPLIT_MASK);
	}
}

#if CONFIG_IS_ENABLED(STARFIVE_OTP)
static void get_cpu_voltage_type(struct udevice *dev)
{
	int ret;
	u32 buf = CPU_VOL_1040;

	ret = misc_read(dev, CPU_VOL_BINNING_OFFSET, &buf, sizeof(buf));
	if (ret != sizeof(buf))
		printf("%s: error reading CPU vol from OTP\n", __func__);
	else {
		switch ((buf & CPU_VOL_MASK)) {
		case CPU_VOL_1000:
			env_set("cpu_max_vol", "1000000");
			break;
		case CPU_VOL_1060:
			env_set("cpu_max_vol", "1060000");
			break;
		case CPU_VOL_1020:
			env_set("cpu_max_vol", "1020000");
			break;
		default:
			break;
		}
	}
}
#endif

static void get_boot_mode(void)
{
	u32 value;

	value = in_le32(AON_IOMUX_BASE + AON_GPIO_DIN_REG);
	switch (value & 0x03) {
		case BOOT_FLASH:
			env_set("bootmode", "flash");
			env_set("devnum", "1");
			break;

		case BOOT_SD:
			env_set("bootmode", "sd");
			env_set("devnum", "1");
			break;

		case BOOT_EMMC:
			env_set("bootmode", "emmc");
			env_set("devnum", "0");
			break;

		default:
			env_set("bootmode", "uart");
			env_set("devnum", "1");
			break;
	}
}

#define CONFIG_SYS_PMIC_BUS_NUM			5
#define CONFIG_SYS_I2C_PMIC_ADDR		0x36
#define CONFIG_SYS_I2C_PMIC_ADDR_LEN		1
#define AXP15060_DCDC_MODE_CTRL_2		0x1b
#define AXP15060_DCDC5_MODE_BIT			BIT(4)
#define AXP15060_PWR_SEQUENCE_CTRL		0X32
#define AXP15060_RST_PMIC_BY_PWROK_BIT		BIT(4)

int set_pmic(void)
{
	int ret;
	struct udevice *dev;
	u8 temp_buf;

	ret = i2c_get_chip_for_busnum(CONFIG_SYS_PMIC_BUS_NUM,
				      CONFIG_SYS_I2C_PMIC_ADDR,
				      CONFIG_SYS_I2C_PMIC_ADDR_LEN,
				      &dev);

	if (!ret) {
		/* Set Pmic DCDC5 Mode as PWM */
		ret = dm_i2c_read(dev,
				  AXP15060_DCDC_MODE_CTRL_2,
				  &temp_buf,
				  sizeof(temp_buf));
		if (ret)
			printf("%s: can't read pmic dcdc5 mode register\n",
                                 __func__);

		temp_buf = temp_buf | AXP15060_DCDC5_MODE_BIT;
		ret = dm_i2c_write(dev,
				AXP15060_DCDC_MODE_CTRL_2,
				&temp_buf,
				sizeof(temp_buf));
		if (ret)
			printf("%s: can't write pmic dcdc5 mode register\n",
                                 __func__);

		/* Enable PWROK in PMIC */
		ret = dm_i2c_read(dev,
				  AXP15060_PWR_SEQUENCE_CTRL,
				  &temp_buf,
				  sizeof(temp_buf));
		if (ret)
                        printf("%s: can't read pmic power disbale & down sequence register\n",
                                 __func__);

		temp_buf = temp_buf | AXP15060_RST_PMIC_BY_PWROK_BIT;
		ret = dm_i2c_write(dev,
                                AXP15060_PWR_SEQUENCE_CTRL,
                                &temp_buf,
                                sizeof(temp_buf));
		if (ret)
                        printf("%s: can't write pmic power disbale & down sequence register\n",
                                 __func__);

	}

	return 0;
}

int board_init(void)
{
	enable_caches();

	jh7110_timer_init();
	jh7110_usb_init(true);
	jh7110_gmac_init(0);
	jh7110_gmac_init(1);
	jh7110_i2c_init(5);

	return 0;
}

#ifdef CONFIG_MISC_INIT_R

int misc_init_r(void)
{
	char mac0[6] = {0x6c, 0xcf, 0x39, 0x6c, 0xde, 0xad};
	char mac1[6] = {0x6c, 0xcf, 0x39, 0x7c, 0xae, 0x5d};

#if CONFIG_IS_ENABLED(STARFIVE_OTP)
	struct udevice *dev;
	char buf[16];
	int ret;
#define MACADDR_OFFSET 0x8

	ret = uclass_get_device_by_driver(UCLASS_MISC,
				DM_DRIVER_GET(starfive_otp), &dev);
	if (ret) {
		debug("%s: could not find otp device\n", __func__);
		goto err;
	}

	ret = misc_read(dev, MACADDR_OFFSET, buf, sizeof(buf));
	if (ret != sizeof(buf))
		printf("%s: error reading mac from OTP\n", __func__);
	else
		if (buf[0] != 0xff) {
			memcpy(mac0, buf, 6);
			memcpy(mac1, &buf[8], 6);
		}
err:
#endif
	eth_env_set_enetaddr("eth0addr", mac0);
	eth_env_set_enetaddr("eth1addr", mac1);

	/* Set the default cpu max voltage. */
	env_set("cpu_max_vol", "1040000");
#if CONFIG_IS_ENABLED(STARFIVE_OTP)
	get_cpu_voltage_type(dev);
#endif
	return 0;
}
#endif

int board_late_init(void)
{
	struct udevice *dev;
	int ret, offset;
	u8 mac0[6], mac1[6];
	u64 share_ram_addr;

	get_boot_mode();

	env_set("chip_vision", "B");

	/*
	 * save the memory info by environment variable in u-boot,
	 * It will used to update the memory configuration in dts,
	 * which passed to kernel lately.
	 */
	env_set_hex("memory_addr", gd->ram_base);
	env_set_hex("memory_size", gd->ram_size);

	ret = uclass_get_device(UCLASS_VIDEO, 0, &dev);
	if (ret)
		return ret;

	ret = video_bmp_display(dev, (ulong)&bmp_logo_bitmap[0], BMP_ALIGN_CENTER, BMP_ALIGN_CENTER, true);
	if (ret)
		goto err;

	/* AMP case : write MAC to share ram */
	offset = fdt_path_offset(gd->fdt_blob,
				 "/chosen/opensbi-domains/rpmsg_shmem");
	if (offset >= 0) {
		share_ram_addr =
			fdtdec_get_uint64(gd->fdt_blob, offset, "base", 0);
		if (share_ram_addr) {
			eth_env_get_enetaddr("eth0addr", mac0);
			eth_env_get_enetaddr("eth1addr", mac1);
			memcpy((void *)share_ram_addr, mac0, 6);
			memcpy((void *)(share_ram_addr + 8), mac1, 6);
		}
	}

err:
	return 0;

}

static int jh7110_get_gpio_val(u32 gpio)
{
	int ret;

	ret = gpio_request(gpio, "ddr_gpio");
	if (!ret) {
		ret = gpio_direction_input(gpio);
		if (!ret)
			ret = gpio_get_value(gpio);

		gpio_free(gpio);
	}

	return ret;
}

int board_ddr_size(void)
{
	int val, ret;

	val = jh7110_get_gpio_val(21);
	if (IS_ERR_VALUE(val))
		return val;

	ret = jh7110_get_gpio_val(22);
	if (IS_ERR_VALUE(ret))
		return ret;

	val |= (ret << 1);

	switch (val) {
	case 0x0:
		ret = 8; //8GB
		break;
	case 0x1:
		ret = 4; //4GB
		break;
	case 0x2:
		ret = 2; //2GB
		break;
	case 0x3:
		ret = 1; //1GB
		break;
	default:
		ret = -EINVAL;
		break;
	};

	return ret;
}

static int starfive_get_gpio_val(u32 gpio)
{
	int ret;

	ret = gpio_request(gpio, "ddr_gpio");
	if (!ret) {
		ret = gpio_direction_input(gpio);
		if (!ret)
			ret = gpio_get_value(gpio);

		gpio_free(gpio);
	}

	return ret;
}

/* get ddr type from GPIO23,GPIO24 DDR4 or LPDDR4 */
int starfive_get_ddr_type(void)
{
	int val, ret;

	val = starfive_get_gpio_val(23);
	if (IS_ERR_VALUE(val))
		return val;

	ret = starfive_get_gpio_val(24);
	if (IS_ERR_VALUE(ret))
		return ret;

	return val | (ret << 1);
}
