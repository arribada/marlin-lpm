#pragma once

#include <cstdint>
#include <expected>

#include <core.pb.h>

namespace monitor
{

enum class Error : uint8_t
{
    error
};

template <typename T = void> using Result = std::expected<T, Error>;

sat_BatteryStatus read_ina219();
Result<> monitor_init();
void     publish_battery_sample();

} // namespace monitor
