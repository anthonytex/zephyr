/*
 * Copyright (C) 2022, Antonio Tessarolo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT fsl_imx21_wdt

#include <drivers/watchdog.h>
#include <wdog_imx.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(wdt_imx_wdog, CONFIG_WDT_LOG_LEVEL);

#define WDOG_TMOUT_SEC(x)  ((((x) * 2) / MSEC_PER_SEC) - 1)
#define WDOG_INT_TMOUT_SEC(x)  (((x) / MSEC_PER_SEC) * 2)


struct imx_wdog_config
{
	WDOG_Type *base;
	void (*irq_config_func)(const struct device *dev);
};

struct imx_wdog_data
{
	wdt_callback_t callback;
	uint32_t timeout;
};

static int imx_wdog_setup(const struct device *dev, uint8_t options)
{
	const struct imx_wdog_config *config = dev->config;
	struct imx_wdog_data *data = dev->data;
	wdog_init_config_t init_config = {0};
	WDOG_Type *base = config->base;

	if (!data->timeout) {
		LOG_ERR("No valid timeouts installed");
		return -EINVAL;
	}

	if (options & WDT_OPT_PAUSE_IN_SLEEP) {
		init_config.wdzst = true;
	}

	if (options & WDT_OPT_PAUSE_HALTED_BY_DBG) {
		init_config.wdbg = true;
	}

	init_config.wdt = true;

	WDOG_Init(base, &init_config);

#ifdef CONFIG_WDT_IMX_RESET_PLATFORM
	sys_set_bit((mem_addr_t)&SRC_SCR, SRC_SCR_wdog3_rst_optn_SHIFT);
#endif

	WDOG_Refresh(base);

	if (data->callback) {
		WDOG_EnableInt(base, WDOG_INT_TMOUT_SEC(CONFIG_WDT_IMX_INT_TIMEOUT));
	}

	WDOG_Enable(base, WDOG_TMOUT_SEC(data->timeout));


	LOG_DBG("Setup the watchdog, timeout %d reg:%d", data->timeout, WDOG_TMOUT_SEC(data->timeout));

	return 0;
}

static int imx_wdog_disable(const struct device *dev)
{
	const struct imx_wdog_config *config = dev->config;
	struct imx_wdog_data *data = dev->data;
	WDOG_Type *base = config->base;

	WDOG_DisablePowerdown(base);
	data->timeout = 0;
	LOG_DBG("Disabled the watchdog");

	return 0;
}

static int imx_wdog_install_timeout(const struct device *dev,
									const struct wdt_timeout_cfg *cfg)
{
	struct imx_wdog_data *data = dev->data;


	if (data->timeout) {
		LOG_ERR("No more timeouts can be installed");
		return -ENOMEM;
	}

	if (cfg->window.min) {
		LOG_ERR("Invalid window.min, Do not support window model");
		return -EINVAL;
	}

	if (cfg->window.max < (MSEC_PER_SEC / 2)) {
		LOG_ERR("Invalid window max, shortest window is 500ms");
		return -EINVAL;
	}

	if ( cfg->window.max > (128 * MSEC_PER_SEC)) {
		LOG_ERR("Invalid timeoutValue, valid (0.5s - 128.0s)");
		return -EINVAL;
	}

	data->timeout = cfg->window.max;
	data->callback = cfg->callback;

	return 0;
}

static int imx_wdog_feed(const struct device *dev, int channel_id)
{
	const struct imx_wdog_config *config = dev->config;
	WDOG_Type *base = config->base;

	if (channel_id != 0) {
		LOG_ERR("Invalid channel id");
		return -EINVAL;
	}

	WDOG_Refresh(base);
	LOG_DBG("Fed the watchdog");

	return 0;
}

static void imx_wdog_isr(void *arg)
{
	const struct device *dev = (const struct device *)arg;
	const struct imx_wdog_config *config = dev->config;
	struct imx_wdog_data *data = dev->data;
	WDOG_Type *base = config->base;

	WDOG_ClearStatusFlag(base);

	if (data->callback) {
		data->callback(dev, 0);
	}
}

static int imx_wdog_init(const struct device *dev)
{
	const struct imx_wdog_config *config = dev->config;

	config->irq_config_func(dev);

	return 0;
}

static const struct wdt_driver_api imx_wdog_api = {
		.setup = imx_wdog_setup,
		.disable = imx_wdog_disable,
		.install_timeout = imx_wdog_install_timeout,
		.feed = imx_wdog_feed,
};

static void imx_wdog_config_func(const struct device *dev);

static const struct imx_wdog_config imx_wdog_config = {
		.base = (WDOG_Type *)DT_INST_REG_ADDR(0),
		.irq_config_func = imx_wdog_config_func,
};

static struct imx_wdog_data imx_wdog_data;

DEVICE_DT_INST_DEFINE(0,
					  &imx_wdog_init,
					  NULL,
					  &imx_wdog_data, &imx_wdog_config,
					  POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
					  &imx_wdog_api);

static void imx_wdog_config_func(const struct device *dev)
{
	IRQ_CONNECT(DT_INST_IRQN(0),
				DT_INST_IRQ(0, priority),
				imx_wdog_isr, DEVICE_DT_INST_GET(0), 0);

	irq_enable(DT_INST_IRQN(0));
}
