#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "battery_info_event.hpp"
#include "core_status_event.hpp"
#include "ina219_emul.h"
#include "lpm_response_event.hpp"
#include "monitor/monitor.hpp"
#include "util.hpp"

#define INA219_NODE DT_ALIAS(battery_sensor)

static const struct device *ina219_dev;
static const struct emul *mock;

K_MSGQ_DEFINE(lpm_response_msgq, sizeof(LpmResponseEvent), 4, 4);
K_MSGQ_DEFINE(battery_info_msgq, sizeof(BatteryInfoEvent), 4, 4);

template <typename TEvent> void push_lpm_response(const zbus_channel *channel)
{
    const auto *event = get_zbus_event<TEvent>(channel);
    LpmResponseEvent resp = *event;
    k_msgq_put(&lpm_response_msgq, &resp, K_NO_WAIT);
}

static void lpm_listener_callback(const struct zbus_channel *channel) // NOLINT
{
    if (channel == &LpmResponseEventChan)
    {
        push_lpm_response<LpmResponseEvent>(channel);
    }
    else if (channel == &BatteryInfoEventChan)
    {
        const auto       *event = get_zbus_event<BatteryInfoEvent>(channel);
        BatteryInfoEvent info   = *event;
        k_msgq_put(&battery_info_msgq, &info, K_NO_WAIT);
    }
    else
    {
        __ASSERT(false, "Channel %s is being listened to but not processed", channel->name);
    }
}

ZBUS_LISTENER_DEFINE(monitor_lpm_listener, lpm_listener_callback);
ZBUS_CHAN_ADD_OBS(LpmResponseEventChan, monitor_lpm_listener, zbus_default_priority);
ZBUS_CHAN_ADD_OBS(BatteryInfoEventChan, monitor_lpm_listener, zbus_default_priority);

static void *setup(void)
{
    ina219_dev = DEVICE_DT_GET(INA219_NODE);
    zassert_not_null(ina219_dev);

    mock = EMUL_DT_GET(DT_NODELABEL(ina219));
    zassert_not_null(mock);

    int ret = monitor::monitor_init().has_value() ? 0 : -1;
    zassert_equal(ret, 0, "monitor_init() should succeed");

    return NULL;
}

ZTEST(monitor_ina219, test_bus_voltage)
{
    /* Set bus voltage register for 12.0V reading.
     * VBUS_GET(x) = (x >> 3) & 0x3FFF, voltage = VBUS_GET * 0.004
     * For 12.0V: VBUS_GET = 3000, register = (3000 << 3) | 0x02(CNVR) = 0x5DC2
     */
    uint16_t vbus_reg = 0x5DC2;
    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_V_BUS, vbus_reg));

    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_CURRENT, 1000));

    sat_BatteryStatus result = monitor::read_ina219();
    zassert_true(result.set, "Battery should be set");

    zassert_within(12.0F, static_cast<double>(result.voltage), 0.01F,
                   "Expected ~12.0V, got %.2fV", static_cast<double>(result.voltage));
}

ZTEST(monitor_ina219, test_positive_current)
{
    uint16_t raw_voltage = (uint16_t)(11.5F / 0.004F) << 3 | 0x02;
    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_V_BUS, raw_voltage));

    /* Set current register: 1.0A = 1000 (with current_lsb=100, 1000*100*1e-5 = 1.0A) */
    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_CURRENT, 1000));

    sat_BatteryStatus result = monitor::read_ina219();
    zassert_true(result.set);

    zassert_within(1.0F, static_cast<double>(result.current), 0.01F,
                   "Expected ~1.0A, got %.2fA", static_cast<double>(result.current));
}

ZTEST(monitor_ina219, test_negative_current_discharge)
{
    uint16_t raw_voltage = (uint16_t)(11.0F / 0.004F) << 3 | 0x02;
    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_V_BUS, raw_voltage));

    /* Set current register: -1.0A = two's complement of 1000 = 0xFC18 */
    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_CURRENT, 0xFC18));

    sat_BatteryStatus result = monitor::read_ina219();
    zassert_true(result.set);

    zassert_within(-1.0F, static_cast<double>(result.current), 0.01F,
                   "Expected ~-1.0A, got %.2fA", static_cast<double>(result.current));
}

ZTEST(monitor_ina219, test_zero_current)
{
    uint16_t raw_voltage = (uint16_t)(12.6F / 0.004F) << 3 | 0x02;
    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_V_BUS, raw_voltage));

    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_CURRENT, 0));

    sat_BatteryStatus result = monitor::read_ina219();
    zassert_true(result.set);

    zassert_within(0.0F, static_cast<double>(result.current), 0.001F,
                   "Expected ~0.0A, got %.2fA", static_cast<double>(result.current));
    zassert_within(12.6F, static_cast<double>(result.voltage), 0.01F,
                   "Expected ~12.6V, got %.2fV", static_cast<double>(result.voltage));
}

ZTEST(monitor_ina219, test_multiple_reads)
{
    uint16_t vbus_1 = (uint16_t)(11.0F / 0.004F) << 3 | 0x02;
    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_V_BUS, vbus_1));
    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_CURRENT, 500));

    sat_BatteryStatus r1 = monitor::read_ina219();
    zassert_true(r1.set);
    zassert_within(11.0F, static_cast<double>(r1.voltage), 0.01F, "First read: expected 11.0V, got %.2fV", static_cast<double>(r1.voltage));
    zassert_within(0.5F, static_cast<double>(r1.current), 0.01F, "First read: expected 0.5A, got %.2fA", static_cast<double>(r1.current));

    uint16_t vbus_2 = (uint16_t)(12.6F / 0.004F) << 3 | 0x02;
    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_V_BUS, vbus_2));
    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_CURRENT, 0xFC18));

    sat_BatteryStatus r2 = monitor::read_ina219();
    zassert_true(r2.set);
    zassert_within(12.6F, static_cast<double>(r2.voltage), 0.01F, "Second read: expected 12.6V, got %.2fV", static_cast<double>(r2.voltage));
    zassert_within(-1.0F, static_cast<double>(r2.current), 0.01F, "Second read: expected -1.0A, got %.2fA", static_cast<double>(r2.current));
}

ZTEST(monitor_ina219, test_status_request_reply)
{
    k_msgq_purge(&lpm_response_msgq);

    uint16_t raw_voltage = (uint16_t)(12.0F / 0.004F) << 3 | 0x02;
    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_V_BUS, raw_voltage));
    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_CURRENT, 1000));

    constexpr uint32_t msg_id = 7;
    CoreStatusEvent    event{};
    event.msg_id = msg_id, event.camera = Status::online;
    publish_zbus_event(event);

    LpmResponseEvent resp{};
    int              ret = k_msgq_get(&lpm_response_msgq, &resp, K_SECONDS(2));
    zassert_equal(ret, 0, "Monitor should respond to a status request");

    zassert_equal(resp.msg_id, msg_id);
    zassert_equal(resp.response, sat_LpmReply_Response_OK);
    zassert_true(resp.battery.has_value(), "Status reply should include battery data");
    zassert_within(12.0F, static_cast<double>(resp.battery->voltage), 0.01F,
                   "Expected ~12.0V, got %.2fV", static_cast<double>(resp.battery->voltage));
    zassert_within(1.0F, static_cast<double>(resp.battery->current), 0.01F,
                   "Expected ~1.0A, got %.2fA", static_cast<double>(resp.battery->current));
}

ZTEST(monitor_ina219, test_sample_fetch_failure)
{
    ina219_mock_set_transfer_error(-EIO);

    sat_BatteryStatus result = monitor::read_ina219();

    zassert_false(result.set, "A fetch failure should yield an invalid sample");
    zassert_equal(result.voltage, 0.0F);
    zassert_equal(result.current, 0.0F);
}

ZTEST(monitor_ina219, test_battery_info_published)
{
    uint16_t raw_voltage = (uint16_t)(12.0F / 0.004F) << 3 | 0x02;
    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_V_BUS, raw_voltage));
    zassert_ok(ina219_mock_set_register(mock->data, INA219_EMUL_REG_CURRENT, 1000));

    k_msgq_purge(&battery_info_msgq);
    monitor::publish_battery_sample();

    BatteryInfoEvent info{};
    zassert_ok(k_msgq_get(&battery_info_msgq, &info, K_SECONDS(1)),
               "Expected a BatteryInfoEvent");

    zassert_true(info.valid, "Battery sample should be valid");
    zassert_within(12.0F, static_cast<double>(info.voltage), 0.01F,
                   "Expected ~12.0V, got %.2fV", static_cast<double>(info.voltage));
    zassert_within(1.0F, static_cast<double>(info.current), 0.01F,
                   "Expected ~1.0A, got %.2fA", static_cast<double>(info.current));
    zassert_within(12.0F, static_cast<double>(info.power), 0.1F,
                   "Expected ~12.0W, got %.2fW", static_cast<double>(info.power));
}

static void after(void * /*unused*/)
{
    ina219_mock_set_transfer_error(0);
}

ZTEST_SUITE(monitor_ina219, NULL, setup, NULL, after, NULL);
