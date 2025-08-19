#define MODULE monitor

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zbus/zbus.h>

#include "monitor.hpp"

#include "battery_info_event.hpp"
#include "core_status_event.hpp"
#include "init_event.hpp"
#include "lpm_response_event.hpp"
#include "util.hpp"

LOG_MODULE_REGISTER(MODULE, CONFIG_MONITOR_MODULE_LOG_LEVEL); // NOLINT

namespace monitor
{

static constexpr int monitor_stack_size = CONFIG_MONITOR_MODULE_STACK_SIZE;
static constexpr int monitor_priority   = CONFIG_MONITOR_MODULE_PRIORITY;

K_THREAD_STACK_DEFINE(monitor_stack_area, monitor_stack_size);

static struct k_thread monitor_thread;
static k_tid_t         monitor_tid;

K_MSGQ_DEFINE(status_request_msgq, sizeof(uint32_t), 4, 4);

void monitor_process(void * /*unused*/, void * /*unused*/, void * /*unused*/);

#if CONFIG_MONITOR_USE_FAKE_BATTERY

static constexpr int64_t CYCLE_PERIOD_MS = 60000;

static sat_BatteryStatus read_fake_battery()
{
    int64_t uptime = k_uptime_get();
    auto    t      = static_cast<double>(uptime % CYCLE_PERIOD_MS) /
             static_cast<double>(CYCLE_PERIOD_MS);

    float sine = std::sin(t * 2.0 * std::numbers::pi);

    float current = sine * 2.0F;
    float voltage = 11.6F + (sine * 1.0F);

    return sat_BatteryStatus{
        .set     = true,
        .voltage = voltage,
        .current = current,
    };
}

#else

static const device *ina219_dev = nullptr;

sat_BatteryStatus read_ina219()
{
    struct sensor_value voltage_val{};
    struct sensor_value current_val{};

    int ret = sensor_sample_fetch(ina219_dev);
    if (ret != 0)
    {
        LOG_ERR("Failed to fetch INA219 sample: %d", ret);
        return sat_BatteryStatus_init_zero;
    }

    ret = sensor_channel_get(ina219_dev, SENSOR_CHAN_VOLTAGE, &voltage_val);
    if (ret != 0)
    {
        LOG_ERR("Failed to get INA219 voltage: %d", ret);
        return sat_BatteryStatus_init_zero;
    }

    ret = sensor_channel_get(ina219_dev, SENSOR_CHAN_CURRENT, &current_val);
    if (ret != 0)
    {
        LOG_ERR("Failed to get INA219 current: %d", ret);
        return sat_BatteryStatus_init_zero;
    }

    return sat_BatteryStatus{
        .set     = true,
        .voltage = static_cast<float>(sensor_value_to_double(&voltage_val)),
        .current = static_cast<float>(sensor_value_to_double(&current_val)),
    };
}

#endif

static sat_BatteryStatus read_battery()
{
#if CONFIG_MONITOR_USE_FAKE_BATTERY
    return read_fake_battery();
#else
    return read_ina219();
#endif
}

Result<> monitor_init()
{
    LOG_INF("Initialising Monitor Module");

#if !CONFIG_MONITOR_USE_FAKE_BATTERY
    ina219_dev = DEVICE_DT_GET(DT_ALIAS(battery_sensor));
    if (!device_is_ready(ina219_dev))
    {
        LOG_ERR("INA219 battery sensor not ready. Check device tree / wiring.");
    }
    else
    {
        LOG_INF("INA219 battery sensor found");
    }
#endif

    monitor_tid =
        k_thread_create(&monitor_thread,
                        static_cast<z_thread_stack_element *>(monitor_stack_area),
                        K_THREAD_STACK_SIZEOF(monitor_stack_area),
                        monitor_process,
                        nullptr,
                        nullptr,
                        nullptr,
                        monitor_priority,
                        0,
                        K_NO_WAIT);

    return {};
}

void monitor_process(void * /*unused*/, void * /*unused*/, void * /*unused*/)
{
    LOG_INF("Entering Monitor Process");

    publish_battery_sample();

    while (true)
    {
        uint32_t requested_msg_id = 0;
        bool     is_request =
            k_msgq_get(&status_request_msgq, &requested_msg_id, K_SECONDS(10)) == 0;

        sat_BatteryStatus sample = read_battery();

        if (is_request)
        {
            publish_zbus_event(LpmResponseEvent{
                .msg_id   = requested_msg_id,
                .response = sat_LpmReply_Response_OK,
                .battery  = sample,
            });
            LOG_DBG("Responded to status request msg_id=%d with V=%.2f, I=%.2f",
                    requested_msg_id,
                    static_cast<double>(sample.voltage),
                    static_cast<double>(sample.current));
            continue;
        }

        publish_battery_sample();
    }
}

void publish_battery_sample()
{

    sat_BatteryStatus sample = read_battery();

    publish_zbus_event(BatteryInfoEvent{
        .voltage = sample.voltage,
        .current = sample.current,
        .power   = sample.voltage * sample.current,
        .valid   = sample.set,
    });
    LOG_DBG("Battery: V=%.2f, I=%.2f, P=%.2f",
            static_cast<double>(sample.voltage),
            static_cast<double>(sample.current),
            static_cast<double>(sample.voltage * sample.current));
}

} // namespace monitor

static void listener_callback(const struct zbus_channel *channel) // NOLINT
{
    if (channel == &init_event_chan)
    {
        if (!monitor::monitor_init())
        {
            LOG_ERR("Failed to initialise monitor module");
        }
    }
    else if (channel == &CoreStatusEventChan)
    {
        const auto *event = get_zbus_event<CoreStatusEvent>(channel);
        const int   ret =
            k_msgq_put(&monitor::status_request_msgq, &event->msg_id, K_NO_WAIT);
        if (ret != 0)
        {
            LOG_ERR("Failed to add status request to the queue");
        }
    }
    else
    {
#if defined(CONFIG_ZBUS_CHANNEL_NAME)
        __ASSERT(
            false, "Channel %s is being listened to but not processed", channel->name);
#else
        __ASSERT(false, "Channel is being listened to but not processed");
#endif
    }
}

ZBUS_LISTENER_DEFINE(monitor_listener, listener_callback);                       // NOLINT
ZBUS_CHAN_ADD_OBS(init_event_chan, monitor_listener, zbus_default_priority);     // NOLINT
ZBUS_CHAN_ADD_OBS(CoreStatusEventChan, monitor_listener, zbus_default_priority); // NOLINT
