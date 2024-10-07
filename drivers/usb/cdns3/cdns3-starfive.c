// SPDX-License-Identifier: GPL-2.0
/**
 * cdns-starfive.c - Cadence USB Controller
 *
 * Copyright (C) 2022-2023 StarFive Technology Co., Ltd.
 * Author:	yanhong <yanhong.wang@starfivetech.com>
 */

#include <asm/io.h>
#include <common.h>
#include <clk.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <dm/lists.h>
#include <linux/usb/otg.h>
#include <reset.h>
#include <regmap.h>
#include <syscon.h>

#include "core.h"

#define USB_STRAP_HOST			(2 << 0x10)
#define USB_STRAP_DEVICE		(4 << 0X10)
#define USB_STRAP_MASK			0x70000

#define USB_SUSPENDM_HOST		(1 << 0x13)
#define USB_SUSPENDM_DEVICE		(0 << 0x13)
#define USB_SUSPENDM_MASK		0x80000

#define USB_SUSPENDM_BYPS_SHIFT	0x14
#define USB_SUSPENDM_BYPS_MASK		0x100000
#define USB_REFCLK_MODE_SHIFT		0x17
#define USB_REFCLK_MODE_MASK		0x800000
#define USB_PLL_EN_SHIFT		0x16
#define USB_PLL_EN_MASK		0x400000
#define USB_PDRSTN_SPLIT_SHIFT		0x11
#define USB_PDRSTN_SPLIT_MASK		0x20000

#define PCIE_CKREF_SRC_SHIFT		0x12
#define PCIE_CKREF_SRC_MASK		0xC0000
#define PCIE_CLK_SEL_SHIFT		0x14
#define PCIE_CLK_SEL_MASK		0x300000
#define PCIE_PHY_MODE_SHIFT		0x14
#define PCIE_PHY_MODE_MASK		0x300000
#define PCIE_USB3_BUS_WIDTH_SHIFT	0x2
#define PCIE_USB3_BUS_WIDTH_MASK	0xC
#define PCIE_USB3_RATE_SHIFT		0x5
#define PCIE_USB3_RATE_MASK		0x60
#define PCIE_USB3_RX_STANDBY_SHIFT	0x7
#define PCIE_USB3_RX_STANDBY_MASK	0x80
#define PCIE_USB3_PHY_ENABLE_SHIFT	0x4
#define PCIE_USB3_PHY_ENABLE_MASK	0x10
#define PCIE_USB3_PHY_PLL_CTL_OFF	(0x1f * 4)

#define USB_125M_CLK_RATE		125000000

#define USB3_PHY_RES_INDEX		0
#define USB2_PHY_RES_INDEX		1
#define USB_LS_KEEPALIVE_OFF		0x4
#define USB_LS_KEEPALIVE_ENABLE		4

struct cdns_starfive {
	struct udevice *dev;
	struct clk_bulk clks;
	struct reset_ctl_bulk resets;
	struct regmap *stg_map;
	struct regmap *sys_map;
	struct clk usb_125m_clk;
	void __iomem *phy3_base;
	void __iomem *phy2_base;
	u32 sys_offset;
	u32 stg_offset_4;
	u32 stg_offset_196;
	u32 stg_offset_328;
	u32 stg_offset_500;
	u32 usb2_only;
	enum usb_dr_mode mode;
};

static int cdns_mode_init(struct cdns_starfive *data)
{
	enum usb_dr_mode mode;
	int ret;

	/* Init usb 2.0 utmi phy */
	regmap_update_bits(data->stg_map, data->stg_offset_4,
		USB_SUSPENDM_BYPS_MASK, BIT(USB_SUSPENDM_BYPS_SHIFT));
	regmap_update_bits(data->stg_map, data->stg_offset_4,
		USB_PLL_EN_MASK, BIT(USB_PLL_EN_SHIFT));
	regmap_update_bits(data->stg_map, data->stg_offset_4,
		USB_REFCLK_MODE_MASK, BIT(USB_REFCLK_MODE_SHIFT));

	if (data->usb2_only) {
		/* Disconnect usb 3.0 phy mode */
		regmap_update_bits(data->sys_map, data->sys_offset,
			USB_PDRSTN_SPLIT_MASK, BIT(USB_PDRSTN_SPLIT_SHIFT));
	} else {
		/* Config usb 3.0 pipe phy */
		regmap_update_bits(data->stg_map, data->stg_offset_196,
			PCIE_CKREF_SRC_MASK, (0<<PCIE_CKREF_SRC_SHIFT));
		regmap_update_bits(data->stg_map, data->stg_offset_196,
			PCIE_CLK_SEL_MASK, (0<<PCIE_CLK_SEL_SHIFT));
		regmap_update_bits(data->stg_map, data->stg_offset_328,
			PCIE_PHY_MODE_MASK, BIT(PCIE_PHY_MODE_SHIFT));
		regmap_update_bits(data->stg_map, data->stg_offset_500,
			PCIE_USB3_BUS_WIDTH_MASK, (0 << PCIE_USB3_BUS_WIDTH_SHIFT));
		regmap_update_bits(data->stg_map, data->stg_offset_500,
			PCIE_USB3_RATE_MASK, (0 << PCIE_USB3_RATE_SHIFT));
		regmap_update_bits(data->stg_map, data->stg_offset_500,
			PCIE_USB3_RX_STANDBY_MASK, (0 << PCIE_USB3_RX_STANDBY_SHIFT));
		regmap_update_bits(data->stg_map, data->stg_offset_500,
			PCIE_USB3_PHY_ENABLE_MASK, BIT(PCIE_USB3_PHY_ENABLE_SHIFT));

		/* Connect usb 3.0 phy mode */
		regmap_update_bits(data->sys_map, data->sys_offset,
			USB_PDRSTN_SPLIT_MASK, (0 << USB_PDRSTN_SPLIT_SHIFT));
	}

	mode = usb_get_dr_mode(dev_read_first_subnode(data->dev));
	if (mode == USB_DR_MODE_UNKNOWN) {
		ret = ofnode_read_u32(dev_read_first_subnode(data->dev), "dr_num_mode" , &mode);
		if (ret)
			return ret;
	}
	data->mode = mode;

	switch (mode) {
	case USB_DR_MODE_HOST:
		regmap_update_bits(data->stg_map,
			data->stg_offset_4,
			USB_STRAP_MASK,
			USB_STRAP_HOST);
		regmap_update_bits(data->stg_map,
			data->stg_offset_4,
			USB_SUSPENDM_MASK,
			USB_SUSPENDM_HOST);
		break;

	case USB_DR_MODE_PERIPHERAL:
		regmap_update_bits(data->stg_map, data->stg_offset_4,
			USB_STRAP_MASK, USB_STRAP_DEVICE);
		regmap_update_bits(data->stg_map, data->stg_offset_4,
			USB_SUSPENDM_MASK, USB_SUSPENDM_DEVICE);
		break;

	case USB_DR_MODE_UNKNOWN:
	case USB_DR_MODE_OTG:
	default:
		break;
	}

	return 0;
}

static int cdns_clk_rst_init(struct cdns_starfive *data)
{
	int ret;

	ret = clk_get_by_name(data->dev, "125m", &data->usb_125m_clk);
	if (ret)
		goto exit;

	ret = clk_get_bulk(data->dev, &data->clks);
	if (ret)
		goto err_125m_clk;

	ret = reset_get_bulk(data->dev, &data->resets);
	if (ret)
		goto err_clk;

	/* Needs to set the USB_125M clock explicitly,
	 * since it's divided from pll0 clock, and the pll0 clock
	 * changes per the cpu frequency.
	 */
	ret = clk_set_rate(&data->usb_125m_clk, USB_125M_CLK_RATE);
	if (!ret)
		goto err_en_clk;

	ret = clk_enable_bulk(&data->clks);
	if (ret)
		goto err_en_clk;

	ret = reset_deassert_bulk(&data->resets);
	if (ret)
		goto err_reset;

	return 0;

err_reset:
	clk_disable_bulk(&data->clks);
err_en_clk:
	reset_release_bulk(&data->resets);
err_clk:
	clk_release_bulk(&data->clks);
err_125m_clk:
	clk_free(&data->usb_125m_clk);
exit:
	return ret;
}

static void cdns_starfive_set_phy(struct cdns_starfive *data)
{
	unsigned int val;
	void __iomem *addr;

	if (data->mode != USB_DR_MODE_PERIPHERAL) {
		/* Enable the LS speed keep-alive signal */
		addr = data->phy2_base + USB_LS_KEEPALIVE_OFF;
		val = readl(addr);
		val |= BIT(USB_LS_KEEPALIVE_ENABLE);
		writel(val, addr);
	}

	if (!data->usb2_only) {
		addr = data->phy3_base + PCIE_USB3_PHY_PLL_CTL_OFF;
		/* Configuare spread-spectrum mode: down-spread-spectrum */
		writel(BIT(4), addr);
	}
}

int cdns3_starfive_bind(struct udevice *parent)
{
	enum usb_dr_mode dr_mode;
	struct udevice *dev;
	const char *driver;
	const char *name;
	ofnode node;
	int ret;

	node = ofnode_by_compatible(dev_ofnode(parent), "cdns,usb3");
	if (!ofnode_valid(node)) {
		printf("%s: failed to get usb node\n",
			__func__);
		goto fail;
	}

	dr_mode = usb_get_dr_mode(node);
	if (dr_mode != USB_DR_MODE_UNKNOWN)
		return cdns3_bind(parent);

	name = ofnode_get_name(node);

#if defined(CONFIG_SPL_USB_HOST) || \
		(!defined(CONFIG_SPL_BUILD) && defined(CONFIG_USB_HOST))
	ret = device_bind_driver_to_node(parent, "cdns-usb3-host", name, node, &dev);
	if (ret) {
		printf("%s: not able to bind usb host mode\n",
			__func__);
		goto fail;
	}
#endif
#if CONFIG_IS_ENABLED(DM_USB_GADGET)
	ret = device_bind_driver_to_node(parent, "cdns-usb3-peripheral", name, node, &dev);
	if (ret) {
		printf("%s: not able to bind usb device mode\n",
		       __func__);
		goto fail;
	}

#endif
fail:
	/* do not return an error: failing to bind would hang the board */
	return 0;
}

static int cdns_starfive_probe(struct udevice *dev)
{
	struct cdns_starfive *data = dev_get_plat(dev);
	struct ofnode_phandle_args args;
	int ret;

	data->dev = dev;
	data->phy3_base = (void *)dev_read_addr_index(dev, USB3_PHY_RES_INDEX);
	if (data->phy3_base == (void __iomem *)FDT_ADDR_T_NONE) {
		dev_err(dev, "Missing phy 3.0 reg base\n");
		return -EINVAL;
	}

	data->phy2_base = (void *)dev_read_addr_index(dev, USB2_PHY_RES_INDEX);
	if (data->phy2_base == (void __iomem *)FDT_ADDR_T_NONE) {
		dev_err(dev, "Missing phy 2.0 reg base");
		return -EINVAL;
	}

	ret = dev_read_u32(dev, "starfive,usb2-only", &data->usb2_only);
	if (ret)
		return ret;

	ret = dev_read_phandle_with_args(dev, "starfive,stg-syscon", NULL, 4, 0, &args);
	if (ret)
		return ret;

	data->stg_map = syscon_node_to_regmap(args.node);
	if (IS_ERR(data->stg_map))
		return PTR_ERR(data->stg_map);

	data->stg_offset_4 = args.args[0];
	data->stg_offset_196 = args.args[1];
	data->stg_offset_328 = args.args[2];
	data->stg_offset_500 = args.args[3];

	ret = dev_read_phandle_with_args(dev, "starfive,sys-syscon", NULL, 1, 0, &args);
	if (ret)
		return ret;

	data->sys_map = syscon_node_to_regmap(args.node);
	if (IS_ERR(data->sys_map))
		return PTR_ERR(data->sys_map);
	data->sys_offset = args.args[0];

	cdns_mode_init(data);

	ret = cdns_clk_rst_init(data);
	if (ret < 0) {
		pr_err("Failed to init usb clk reset: %d\n", ret);
		return ret;
	}
	cdns_starfive_set_phy(data);

	return ret;
}

static int cdns_starfive_remove(struct udevice *dev)
{
	struct cdns_starfive *data = dev_get_plat(dev);

	clk_release_bulk(&data->clks);
	reset_assert_bulk(&data->resets);
	clk_free(&data->usb_125m_clk);

	return 0;
}

static const struct udevice_id cdns_starfive_of_match[] = {
	{ .compatible = "starfive,jh7110-cdns3", },
	{},
};

U_BOOT_DRIVER(cdns_starfive) = {
	.name = "cdns-starfive",
	.id = UCLASS_NOP,
	.of_match = cdns_starfive_of_match,
	.bind = cdns3_starfive_bind,
	.probe = cdns_starfive_probe,
	.remove = cdns_starfive_remove,
	.plat_auto	= sizeof(struct cdns_starfive),
};
