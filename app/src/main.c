/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

void main(void)
{
	int ret;
	struct sensor_value temp, hum;
	const struct device *aht10;

	printk("AHT10 sensor example\n");

	/* Get AHT10 device */
	aht10 = DEVICE_DT_GET(DT_NODELABEL(aht10));
	if (!device_is_ready(aht10)) {
		LOG_ERR("AHT10 device is not ready");
		return;
	}

	while (1) {
		/* Fetch sensor data */
		ret = sensor_sample_fetch(aht10);
		if (ret != 0) {
			LOG_ERR("Failed to fetch sample (%d)", ret);
			k_sleep(K_MSEC(1000));
			continue;
		}

		/* Get temperature channel data */
		ret = sensor_channel_get(aht10, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		if (ret != 0) {
			LOG_ERR("Failed to get temperature channel data (%d)", ret);
			k_sleep(K_MSEC(1000));
			continue;
		}

		/* Get humidity channel data */
		ret = sensor_channel_get(aht10, SENSOR_CHAN_HUMIDITY, &hum);
		if (ret != 0) {
			LOG_ERR("Failed to get humidity channel data (%d)", ret);
			k_sleep(K_MSEC(1000));
			continue;
		}

		/* Print sensor data */
		printk("Temperature: %d.%06d °C, Humidity: %d.%06d %%RH\n",
		       temp.val1, abs(temp.val2),
		       hum.val1, abs(hum.val2));

		k_sleep(K_MSEC(2000));
	}
}

