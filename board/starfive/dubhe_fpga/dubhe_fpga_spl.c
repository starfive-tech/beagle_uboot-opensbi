// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Copyright (c) 2021 StarFive Technology Co., Ltd. */

#include <dm.h>
#include <spl.h>
#include <asm/csr.h>
#include <asm/arch-dubhe/csr.h>

int spl_board_init_f(void)
{
	int ret;
	struct udevice *dev;

	/* Init DRAM */
	ret = uclass_get_device(UCLASS_RAM, 0, &dev);
	if (ret) {
		debug("DRAM init failed: %d\n", ret);
		return ret;
	}

	return 0;
}

void board_boot_order(u32 *spl_boot_list)
{
	spl_boot_list[0] = BOOT_DEVICE_SPI;
}

int board_fit_config_name_match(const char *name)
{
	char config_name[8];
	u32 marchid;

	marchid = csr_read(CSR_MARCHID);

	switch (marchid) {
	case DUBHE90_MARCHID:
		sprintf(config_name, "conf-%s", "90");
		return strcmp(name, config_name);

	case DUBHE80_MARCHID:
		sprintf(config_name, "conf-%s", "80");
		return strcmp(name, config_name);

	case DUBHE70_MARCHID:
		sprintf(config_name, "conf-%s", "70");
		return strcmp(name, config_name);

	default:
		return 0;
	}
}
