#include <cstring>

#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/drivers/rtc/rtc_fake.h>

DEFINE_FFF_GLOBALS;
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "rtc/rtc.hpp"

static struct rtc_time mock_get_time;
static int             mock_get_ret    = 0;
static int             mock_set_ret    = 0;
static bool            set_time_called = false;

static int rtc_fake_get_time_mock(const struct device *rtc_device, struct rtc_time *time)
{
    ARG_UNUSED(rtc_device);

    *time = mock_get_time;
    return mock_get_ret;
}

static int rtc_fake_set_time_mock(const struct device   *rtc_device,
                                  const struct rtc_time *time)
{
    ARG_UNUSED(rtc_device);
    ARG_UNUSED(time);

    set_time_called = true;
    return mock_set_ret;
}

static void configure_get_mock(int ret,
                               int year,
                               int month,
                               int day,
                               int hour,
                               int minute,
                               int sec)
{
    mock_get_ret          = ret;
    mock_get_time.tm_year = year - 1900;
    mock_get_time.tm_mon  = month - 1;
    mock_get_time.tm_mday = day;
    mock_get_time.tm_hour = hour;
    mock_get_time.tm_min  = minute;
    mock_get_time.tm_sec  = sec;

    rtc_fake_get_time_fake.custom_fake = rtc_fake_get_time_mock;
}

static void configure_set_mock(int ret)
{
    mock_set_ret    = ret;
    set_time_called = false;

    rtc_fake_set_time_fake.custom_fake = rtc_fake_set_time_mock;
}

constexpr int64_t test_epoch = 1787236205; // 2026-08-20T14:30:05Z

ZTEST(rtc, test_init_time_set)
{
    configure_set_mock(0);
    configure_get_mock(0, 2026, 8, 20, 14, 30, 5);

    auto ret = rtc::init();

    zassert_true(ret.has_value(), "init failed");
    zassert_true(!set_time_called, "set_time should not be called when time is set");
}

ZTEST(rtc, test_current_time)
{
    configure_get_mock(0, 2026, 8, 20, 14, 30, 5);

    char buf[rtc::time_format_max_size]{};
    auto ret = rtc::current_time(buf);

    zassert_true(ret.has_value(), "current_time failed");
    zassert_true(std::strcmp(buf, "2026-08-20T14:30:05") == 0, "formatted time mismatch");
}

ZTEST(rtc, test_current_time_failure)
{
    configure_get_mock(-EIO, 2026, 8, 20, 14, 30, 5);

    char buf[rtc::time_format_max_size]{};
    auto ret = rtc::current_time(buf);

    zassert_false(ret.has_value(), "current_time should fail when get_time errors");
    zassert_equal(ret.error(), rtc::Error::error);
}

ZTEST(rtc, test_current_epoch)
{
    configure_get_mock(0, 2026, 8, 20, 14, 30, 5);

    auto ret = rtc::current_epoch();

    zassert_true(ret.has_value(), "current_epoch failed");
    zassert_equal(ret.value(), test_epoch, "epoch mismatch");
}

ZTEST(rtc, test_current_epoch_failure)
{
    configure_get_mock(-EIO, 2026, 8, 20, 14, 30, 5);

    auto ret = rtc::current_epoch();

    zassert_false(ret.has_value(), "current_epoch should fail when get_time errors");
    zassert_equal(ret.error(), rtc::Error::error);
}

ZTEST(rtc, test_set_time_from_epoch)
{
    configure_set_mock(0);
    configure_get_mock(0, 2026, 8, 20, 14, 30, 5);

    auto ret = rtc::set_time_from_epoch(test_epoch);

    zassert_true(ret.has_value(), "set_time_from_epoch failed");
    zassert_true(set_time_called, "rtc_set_time should have been invoked");
}

ZTEST(rtc, test_set_time_from_epoch_set_failure)
{
    configure_set_mock(-EIO);
    configure_get_mock(0, 2026, 8, 20, 14, 30, 5);

    auto ret = rtc::set_time_from_epoch(test_epoch);

    zassert_false(ret.has_value(), "set_time_from_epoch should fail when set errors");
    zassert_equal(ret.error(), rtc::Error::error);
}

ZTEST(rtc, test_set_time_from_epoch_readback_failure)
{
    // The set succeeds but reading the new time back fails, so the call reports
    // an error even though the RTC was written.
    configure_set_mock(0);
    configure_get_mock(-EIO, 2026, 8, 20, 14, 30, 5);

    auto ret = rtc::set_time_from_epoch(test_epoch);

    zassert_false(ret.has_value(), "set_time_from_epoch should fail on read-back");
    zassert_equal(ret.error(), rtc::Error::error);
}

static void *setup()
{
    zassert_true(rtc::init().has_value(), "rtc init failed");
    return nullptr;
}

ZTEST_SUITE(rtc, NULL, setup, NULL, NULL, NULL);
