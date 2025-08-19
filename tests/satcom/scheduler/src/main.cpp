#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_error_hook.h>

#include "satcom.hpp"

K_MSGQ_DEFINE(test_msgq, sizeof(satcom::Events), 10, 4);

LOG_MODULE_REGISTER(satcom, LOG_LEVEL_DBG); // NOLINT

constexpr int64_t tx_delay_ms = CONFIG_SATCOM_SCHEDULER_TX_DELAY_MS;

static bool wait_for_signal(int64_t timeout_ms)
{
    satcom::Events event;
    return k_msgq_get(&test_msgq, &event, K_MSEC(timeout_ms)) == 0;
}

// Waits for the scheduler's timer to fire and checks it did not arrive early.
static bool verify_message(int64_t expected_delay_ms)
{
    int64_t start = k_uptime_get();
    if (!wait_for_signal(expected_delay_ms + 1000))
    {
        return false;
    }
    return (k_uptime_get() - start) >= expected_delay_ms;
}

ZTEST(scheduler, test_enroll_success)
{
    SatComMsgData data{};
    data.fill('A');     // NOLINT
    data.at(10) = '\0'; // NOLINT

    satcom::Scheduler scheduler(&test_msgq);

    zassert_true(scheduler.enroll(data).has_value());
}

ZTEST(scheduler, test_enroll_and_next)
{
    SatComMsgData data{};
    data.fill('A');     // NOLINT
    data.at(10) = '\0'; // NOLINT

    satcom::Scheduler scheduler(&test_msgq);

    zassert_true(scheduler.enroll(data).has_value());
    zassert_true(verify_message(tx_delay_ms), "Message should arrive after the delay");
    auto dtx = scheduler.next();
    zassert_true(dtx.has_value());
    zassert_equal(dtx.value().data(), data, "Next should return the enqueued item");
}

ZTEST(scheduler, test_enroll_and_next_multiple)
{
    SatComMsgData data1{};
    data1.fill('A');     // NOLINT
    data1.at(10) = '\0'; // NOLINT

    SatComMsgData data2{};
    data2.fill('B');     // NOLINT
    data2.at(10) = '\0'; // NOLINT

    satcom::Scheduler scheduler(&test_msgq);

    zassert_true(scheduler.enroll(data1).has_value());

    k_sleep(K_MSEC(tx_delay_ms / 2));

    zassert_true(scheduler.enroll(data2).has_value());

    zassert_true(wait_for_signal(tx_delay_ms * 2), "First message should fire");
    zassert_equal(scheduler.next().value().data(), data1, "Messages should come in a fifo manner");

    zassert_true(wait_for_signal(tx_delay_ms * 2), "Second message should fire");
    zassert_equal(scheduler.next().value().data(), data2, "Messages should come in a fifo manner");
}

ZTEST(scheduler, test_capacity)
{
    SatComMsgData data{};

    satcom::Scheduler scheduler(&test_msgq);

    for (size_t i = 0; i < 10; ++i)
    {
        auto res = scheduler.enroll(data);
        zassert_true(res.has_value(), "Enroll should succeed while under capacity");
    }

    auto res = scheduler.enroll(data);
    zassert_false(res.has_value(), "Enroll should fail when capacity is exceeded");
}

ZTEST(scheduler, test_next_empty)
{
    satcom::Scheduler scheduler(&test_msgq);

    auto item = scheduler.next();
    zassert_false(item.has_value(), "next() on an empty queue should fail");
    zassert_equal(item.error(), satcom::Error::error);
}

ZTEST(scheduler, test_finished_transmissions)
{
    SatComMsgData data{};

    satcom::Scheduler scheduler(&test_msgq);
    data.fill('A');    // NOLINT
    data.at(1) = '\0'; // NOLINT

    zassert_true(scheduler.enroll(data).has_value(), "Initial enroll failed");
    for (int i = 0; i < 29; ++i)
    {
        zassert_true(verify_message(tx_delay_ms), "Message should arrive after the delay");
        auto item = scheduler.next();
        zassert_true(item.has_value());
        zassert_true(scheduler.re_enroll(item.value()).has_value());
    }
    zassert_false(verify_message(tx_delay_ms),
                  "30 Transmissions is max, last re_enroll should have discarded item");
}

static void before(void * /*fixture*/)
{
    k_msgq_purge(&test_msgq);
}

ZTEST_SUITE(scheduler, NULL, NULL, before, NULL, NULL);
