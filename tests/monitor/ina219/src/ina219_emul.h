/*
 * Emulator for the TI INA219 I2C power monitor
 */

#ifndef INA219_EMUL_H_
#define INA219_EMUL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* INA219 register addresses */
#define INA219_EMUL_REG_CONF      0x00
#define INA219_EMUL_REG_V_SHUNT   0x01
#define INA219_EMUL_REG_V_BUS     0x02
#define INA219_EMUL_REG_POWER     0x03
#define INA219_EMUL_REG_CURRENT   0x04
#define INA219_EMUL_REG_CALIB     0x05

int ina219_mock_set_register(void *data_ptr, int reg, uint16_t value);
int ina219_mock_get_register(void *data_ptr, int reg, uint16_t *value_ptr);

/* Forces every I2C transfer to fail with `error` until reset to 0. */
void ina219_mock_set_transfer_error(int error);

#ifdef __cplusplus
}
#endif

#endif /* INA219_EMUL_H_ */