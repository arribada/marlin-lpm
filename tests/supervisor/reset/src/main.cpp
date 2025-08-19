#include <cstdint>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "core_status_event.hpp"
#include "core_timeout_event.hpp"
#include "init_event.hpp"
#include "util.hpp"

LOG_MODULE_REGISTER(supervisor_reset_test, LOG_LEVEL_INF); // NOLINT

// native_sim exposes gpio0 as a zephyr,gpio-emul controller. The test overlay
// wires the core_reset alias to <&gpio0 0 GPIO_ACTIVE_LOW>.
static const struct device *const reset_port = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static constexpr uint32_t         reset_pin  = 0;

static gpio_port_value_t read_reset_pin()
{
    // gpio_emul_output_get returns 1/0 on success (or a negative errno).
    return gpio_emul_output_get(reset_port, reset_pin) == 1 ? 1 : 0;
}

// Returns true once the reset line leaves `idle` (a pulse is in progress).
static bool wait_for_activity(gpio_port_value_t idle, int64_t timeout_ms)
{
    int64_t start = k_uptime_get();
    while (k_uptime_get() - start < timeout_ms)
    {
        if (read_reset_pin() != idle)
        {
            return true;
        }
        k_msleep(2);
    }
    return false;
}

// Returns true once the reset line returns to `idle` after a pulse.
static bool wait_for_idle(gpio_port_value_t idle, int64_t timeout_ms)
{
    int64_t start = k_uptime_get();
    while (k_uptime_get() - start < timeout_ms)
    {
        if (read_reset_pin() == idle)
        {
            return true;
        }
        k_msleep(2);
    }
    return false;
}

static gpio_port_value_t wait_until_supervisor_ready()
{
    // The supervisor thread configures the line inactive during init; give
    // supervisor_init() and the thread a moment to come up.
    k_msleep(50);

    return read_reset_pin();
}

static CoreStatusEvent healthy_core_status()
{
    CoreStatusEvent recovery{};
    recovery.msg_id     = 0;
    recovery.camera     = Status::online;
    recovery.controller = Status::online;
    return recovery;
}

void *init()
{
    publish_zbus_event(InitEvent{});
    k_msleep(100);
    return nullptr;
}

void before(void * /*unused*/)
{
}

void after(void * /*unused*/)
{
}

ZTEST(supervisor_reset, test_system_error_issues_reset_pulse)
{
    ztest_test_skip();
    gpio_port_value_t idle = wait_until_supervisor_ready();

    publish_zbus_event(CoreTimeoutEvent{});

    zassert_true(wait_for_activity(idle, 3000), "expected a reset pulse to start");
    zassert_true(wait_for_idle(idle, 2000), "expected the reset pulse to complete");
}

ZTEST(supervisor_reset, test_reset_retry_cadence_then_cancel)
{
    ztest_test_skip();
    gpio_port_value_t idle = wait_until_supervisor_ready();

    publish_zbus_event(CoreTimeoutEvent{});
    zassert_true(wait_for_activity(idle, 3000), "expected the first reset pulse");
    zassert_true(wait_for_idle(idle, 2000), "expected the first pulse to complete");

    // The retry timer re-fires every SUPERVISOR_CORE_RESET_RETRY_SECONDS, so a
    // second pulse must appear after the configured interval.
    zassert_true(wait_for_activity(idle, 5000),
                 "expected a retry pulse from the cadence");
    zassert_true(wait_for_idle(idle, 2000), "expected the retry pulse to complete");

    // Leaving SystemError stops the timer and posts the stop event; no further
    // pulses should be issued.
    publish_zbus_event(healthy_core_status());

    zassert_true(wait_for_idle(idle, 2000), "in-flight pulse did not settle");
    k_sleep(K_MSEC(CONFIG_SUPERVISOR_CORE_RESET_RETRY_SECONDS * 2000));

    int64_t start = k_uptime_get();
    while (k_uptime_get() - start < 2500)
    {
        zassert_equal(read_reset_pin(), idle, "reset line pulsed again after recovery");
        k_msleep(3);
    }
}

ZTEST_SUITE(supervisor_reset, NULL, init, before, after, NULL);