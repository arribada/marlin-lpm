/*
 * Emulator for the TI INA219 I2C power monitor
 *
 * Emulates the I2C register interface of the INA219 so that
 * the real Zephyr INA219 sensor driver can be tested on native_sim.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "ina219_emul.h"

LOG_MODULE_REGISTER(INA219_EMUL, CONFIG_SENSOR_LOG_LEVEL);

#define INA219_NUM_REGS 6

struct ina219_reg {
    uint8_t id;
    uint16_t value;
};

struct ina219_emul_cfg {
    uint16_t addr;
};

struct ina219_emul_data {
    struct ina219_reg regs[INA219_NUM_REGS];
};

static struct ina219_reg *get_register(struct ina219_emul_data *data, int reg)
{
    for (int i = 0; i < INA219_NUM_REGS; i++)
    {
        if (data->regs[i].id == reg)
        {
            return &data->regs[i];
        }
    }
    return NULL;
}

int ina219_mock_set_register(void *data_ptr, int reg, uint16_t value)
{
    struct ina219_reg *reg_ptr = get_register(data_ptr, reg);
    if (reg_ptr == NULL)
    {
        return -EINVAL;
    }
    reg_ptr->value = value;
    return 0;
}

int ina219_mock_get_register(void *data_ptr, int reg, uint16_t *value_ptr)
{
    struct ina219_reg *reg_ptr = get_register(data_ptr, reg);
    if (reg_ptr == NULL || value_ptr == NULL)
    {
        return -EINVAL;
    }
    *value_ptr = reg_ptr->value;
    return 0;
}

static int forced_transfer_error;

void ina219_mock_set_transfer_error(int error)
{
    forced_transfer_error = error;
}

static int ina219_emul_transfer_i2c(const struct emul *target, struct i2c_msg msgs[], int num_msgs,
                                    int addr)
{
    struct ina219_emul_data *data = (struct ina219_emul_data *)target->data;
    ARG_UNUSED(addr);

    if (forced_transfer_error != 0)
    {
        return forced_transfer_error;
    }

    if (!msgs || num_msgs < 1 || num_msgs > 2)
    {
        LOG_ERR("Invalid number of messages: %d", num_msgs);
        return -EIO;
    }

    if (msgs[0].flags & I2C_MSG_READ)
    {
        LOG_ERR("Expected write");
        return -EIO;
    }

    if (num_msgs == 1)
    {
        /* Write16 Transaction: [reg, hi, lo] */
        if (msgs[0].len != 3)
        {
            LOG_ERR("Expected 3 bytes for write, got %d", msgs[0].len);
            return -EIO;
        }

        uint8_t reg = msgs[0].buf[0];
        uint16_t val = sys_get_be16(&msgs[0].buf[1]);

        struct ina219_reg *reg_ptr = get_register(data, reg);
        if (!reg_ptr)
        {
            LOG_ERR("Invalid register: %02x", reg);
            return -EIO;
        }
        reg_ptr->value = val;
        LOG_DBG("Write reg %02x: %04x", reg, val);
    }
    else
    {
        /* Read Transaction: [reg] + read 2 bytes */
        if ((msgs[1].flags & I2C_MSG_READ) == 0)
        {
            LOG_ERR("Expected read");
            return -EIO;
        }

        uint8_t reg = msgs[0].buf[0];
        struct ina219_reg *reg_ptr = get_register(data, reg);

        if (!reg_ptr)
        {
            LOG_ERR("Invalid register: %02x", reg);
            return -EIO;
        }

        if (msgs[1].len != 2)
        {
            LOG_ERR("Expected 2 bytes for read, got %d", msgs[1].len);
            return -EIO;
        }

        sys_put_be16(reg_ptr->value, msgs[1].buf);
        LOG_DBG("Read reg %02x: %04x", reg, reg_ptr->value);
    }

    return 0;
}

static int ina219_emul_init(const struct emul *target, const struct device *parent)
{
    ARG_UNUSED(target);
    ARG_UNUSED(parent);
    return 0;
}

static const struct i2c_emul_api ina219_emul_api_i2c = {
    .transfer = ina219_emul_transfer_i2c,
};

#define INA219_EMUL_INIT(n)                                                                        \
    static struct ina219_emul_data ina219_emul_data_##n = {                                        \
        .regs = {                                                                                  \
            {INA219_EMUL_REG_CONF,     0x399F},                                                    \
            {INA219_EMUL_REG_V_SHUNT,  0x0000},                                                    \
            {INA219_EMUL_REG_V_BUS,    0x0004},                                                    \
            {INA219_EMUL_REG_POWER,    0x0000},                                                    \
            {INA219_EMUL_REG_CURRENT,  0x0000},                                                    \
            {INA219_EMUL_REG_CALIB,    0x0000},                                                    \
        },                                                                                         \
    };                                                                                             \
    static const struct ina219_emul_cfg ina219_emul_cfg_##n = {                                    \
        .addr = DT_INST_REG_ADDR(n),                                                               \
    };                                                                                             \
    EMUL_DT_INST_DEFINE(n, ina219_emul_init, &ina219_emul_data_##n, &ina219_emul_cfg_##n,          \
                        &ina219_emul_api_i2c, NULL)

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT ti_ina219
DT_INST_FOREACH_STATUS_OKAY(INA219_EMUL_INIT)