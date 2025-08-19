#define MODULE rtc

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/timeutil.h>

#include "rtc.hpp"

LOG_MODULE_REGISTER(MODULE, CONFIG_RTC_MODULE_LOG_LEVEL); // NOLINT

namespace rtc
{

static const struct device *rtc_dev = nullptr;

Result<> init()
{
    if (rtc_dev == nullptr)
    {
        rtc_dev = DEVICE_DT_GET(DT_ALIAS(rtc));
    }

    if (!device_is_ready(rtc_dev))
    {
        LOG_ERR("RTC device is not ready");
        return std::unexpected(Error::error);
    }

    return {};
}

Result<> current_time(char *buf)
{
    struct rtc_time rtc_instant{};

    int err = rtc_get_time(rtc_dev, &rtc_instant);
    if (err < 0)
    {
        LOG_ERR("Failed to read RTC time");
        return std::unexpected(Error::error);
    }

    // verified to fit time_format_max_size.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    (void)std::snprintf(buf,
                        time_format_max_size,
                        "%04d-%02d-%02dT%02d:%02d:%02d",
                        rtc_instant.tm_year + 1900,
                        rtc_instant.tm_mon + 1,
                        rtc_instant.tm_mday,
                        rtc_instant.tm_hour,
                        rtc_instant.tm_min,
                        rtc_instant.tm_sec);
#pragma GCC diagnostic pop

    return {};
}

Result<int64_t> current_epoch()
{
    struct rtc_time rtc_instant{};

    int err = rtc_get_time(rtc_dev, &rtc_instant);
    if (err < 0)
    {
        LOG_ERR("Failed to read RTC time");
        return std::unexpected(Error::error);
    }

    std::tm instant{};
    instant.tm_year = rtc_instant.tm_year;
    instant.tm_mon  = rtc_instant.tm_mon;
    instant.tm_mday = rtc_instant.tm_mday;
    instant.tm_hour = rtc_instant.tm_hour;
    instant.tm_min  = rtc_instant.tm_min;
    instant.tm_sec  = rtc_instant.tm_sec;

    return timeutil_timegm(&instant);
}

Result<> set_time_from_epoch(int64_t epoch_seconds)
{
    auto    provided_time = static_cast<std::time_t>(epoch_seconds);
    std::tm rtc_instant{};
    if (::gmtime_r(&provided_time, &rtc_instant) == nullptr)
    {
        LOG_ERR("Failed to convert epoch to time");
        return std::unexpected(Error::error);
    }

    struct rtc_time rtc{};
    rtc.tm_year = rtc_instant.tm_year;
    rtc.tm_mon  = rtc_instant.tm_mon; // 0-indexed (0..11)
    rtc.tm_mday = rtc_instant.tm_mday;
    rtc.tm_hour = rtc_instant.tm_hour;
    rtc.tm_min  = rtc_instant.tm_min;
    rtc.tm_sec  = rtc_instant.tm_sec;

    if (rtc_set_time(rtc_dev, &rtc) != 0)
    {
        LOG_ERR("Failed to set RTC time from epoch");
        return std::unexpected(Error::error);
    }

    char time_str[rtc::time_format_max_size]{}; // NOLINT
    auto ret = rtc::current_time(time_str);     // NOLINT
    if (!ret)
    {
        return ret;
    }

    LOG_INF("RTC Set to: %s", time_str); // NOLINT

    return {};
}

} // namespace rtc
