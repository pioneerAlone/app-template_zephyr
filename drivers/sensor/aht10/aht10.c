/*
 * Copyright (c) 2024 Custom App Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT aosong_aht10

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(aht10, CONFIG_SENSOR_LOG_LEVEL);

#define AHT10_I2C_ADDRESS              0x38

/* AHT10 commands */
#define AHT10_CMD_INITIALIZATION       0xE1
#define AHT10_CMD_TRIGGER_MEASUREMENT  0xAC
#define AHT10_CMD_SOFT_RESET           0xBA

/* AHT10 constants */
#define AHT10_MEASUREMENT_DELAY_MS     80
#define AHT10_RESET_DELAY_MS           20
#define AHT10_STATUS_BUSY_MASK         0x80

/* AHT10 response structure */
#define AHT10_STATUS_BYTE_IDX          0
#define AHT10_HUMIDITY_HBYTE_IDX       1
#define AHT10_HUMIDITY_LBYTE_IDX       2
#define AHT10_HUMIDITY_XBYTE_IDX       3
#define AHT10_TEMPERATURE_HBYTE_IDX    3
#define AHT10_TEMPERATURE_LBYTE_IDX    4
#define AHT10_TEMPERATURE_XBYTE_IDX    5

struct aht10_data {
    struct sensor_value temperature;
    struct sensor_value humidity;
};

struct aht10_config {
    struct i2c_dt_spec i2c;
};

static int aht10_write_command(const struct device *dev, uint8_t cmd, uint8_t data1, uint8_t data2)
{
    const struct aht10_config *cfg = dev->config;
    uint8_t buffer[3] = {cmd, data1, data2};

    return i2c_write_dt(&cfg->i2c, buffer, sizeof(buffer));
}

static int aht10_soft_reset(const struct device *dev)
{
    const struct aht10_config *cfg = dev->config;
    int ret;

    ret = aht10_write_command(dev, AHT10_CMD_SOFT_RESET, 0, 0);
    if (ret < 0) {
        LOG_ERR("Failed to send soft reset command");
        return ret;
    }

    k_sleep(K_MSEC(AHT10_RESET_DELAY_MS));

    return 0;
}

static int aht10_initialize(const struct device *dev)
{
    const struct aht10_config *cfg = dev->config;
    int ret;

    if (!device_is_ready(cfg->i2c.bus)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    /* Send initialization command */
    ret = aht10_write_command(dev, AHT10_CMD_INITIALIZATION, 0x08, 0x00);
    if (ret < 0) {
        LOG_ERR("Failed to send initialization command");
        return ret;
    }

    k_sleep(K_MSEC(AHT10_RESET_DELAY_MS));

    return 0;
}

static int aht10_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
    const struct aht10_config *cfg = dev->config;
    struct aht10_data *data = dev->data;
    uint8_t buffer[6];
    int ret;

    __ASSERT_NO_MSG(chan == SENSOR_CHAN_ALL || chan == SENSOR_CHAN_AMBIENT_TEMP ||
                    chan == SENSOR_CHAN_HUMIDITY);

    /* Trigger measurement */
    ret = aht10_write_command(dev, AHT10_CMD_TRIGGER_MEASUREMENT, 0x33, 0x00);
    if (ret < 0) {
        LOG_ERR("Failed to trigger measurement");
        return ret;
    }

    /* Wait for measurement to complete */
    k_sleep(K_MSEC(AHT10_MEASUREMENT_DELAY_MS));

    /* Read measurement data */
    ret = i2c_read_dt(&cfg->i2c, buffer, sizeof(buffer));
    if (ret < 0) {
        LOG_ERR("Failed to read measurement data");
        return ret;
    }

    /* Check if sensor is busy */
    if ((buffer[AHT10_STATUS_BYTE_IDX] & AHT10_STATUS_BUSY_MASK) != 0) {
        LOG_WRN("Sensor is busy");
        return -EBUSY;
    }

    /* Calculate humidity [%RH] */
    uint32_t raw_humidity = ((uint32_t)buffer[AHT10_HUMIDITY_HBYTE_IDX] << 16) |
                            ((uint32_t)buffer[AHT10_HUMIDITY_LBYTE_IDX] << 8) |
                            buffer[AHT10_HUMIDITY_XBYTE_IDX];
    
    /* Scale to percentage: (raw_value / 2^20) * 100 */
    double humidity = (double)raw_humidity * 100.0 / 1048576.0;
    data->humidity.val1 = (int32_t)humidity;
    data->humidity.val2 = (int32_t)((humidity - data->humidity.val1) * 1000000);

    /* Calculate temperature [°C] */
    uint32_t raw_temperature = (((uint32_t)buffer[AHT10_TEMPERATURE_HBYTE_IDX] & 0x0F) << 16) |
                               ((uint32_t)buffer[AHT10_TEMPERATURE_LBYTE_IDX] << 8) |
                               buffer[AHT10_TEMPERATURE_XBYTE_IDX];
    
    /* Scale to degrees Celsius: (raw_value / 2^20) * 200 - 50 */
    double temperature = (double)raw_temperature * 200.0 / 1048576.0 - 50.0;
    data->temperature.val1 = (int32_t)temperature;
    data->temperature.val2 = (int32_t)((temperature - data->temperature.val1) * 1000000);

    return 0;
}

static int aht10_channel_get(const struct device *dev, enum sensor_channel chan,
                             struct sensor_value *val)
{
    struct aht10_data *data = dev->data;

    switch (chan) {
    case SENSOR_CHAN_AMBIENT_TEMP:
        *val = data->temperature;
        break;

    case SENSOR_CHAN_HUMIDITY:
        *val = data->humidity;
        break;

    default:
        return -ENOTSUP;
    }

    return 0;
}

static const struct sensor_driver_api aht10_driver_api = {
    .sample_fetch = aht10_sample_fetch,
    .channel_get = aht10_channel_get,
};

static int aht10_init(const struct device *dev)
{
    int ret;

    ret = aht10_initialize(dev);
    if (ret < 0) {
        LOG_ERR("Failed to initialize AHT10");
        return ret;
    }

    return 0;
}

#define AHT10_INIT(i)                                                           \
    static struct aht10_data aht10_data_##i;                                    \
    static const struct aht10_config aht10_config_##i = {                       \
        .i2c = I2C_DT_SPEC_INST_GET(i),                                         \
    };                                                                          \
    DEVICE_DT_INST_DEFINE(i, aht10_init, NULL,                                  \
                          &aht10_data_##i, &aht10_config_##i, POST_KERNEL,      \
                          CONFIG_SENSOR_INIT_PRIORITY, &aht10_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AHT10_INIT)