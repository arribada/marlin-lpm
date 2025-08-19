#pragma once

#include <cstdint>
#include <expected>

namespace rtc
{

enum class Error : uint8_t
{
    error
};

template <typename T = void> using Result = std::expected<T, Error>;

constexpr uint32_t time_format_max_size = 20; // "YYYY-MM-DDTHH:MM:SS" + null

Result<> init();
Result<> current_time(char *buf);
Result<int64_t> current_epoch();
Result<> set_time_from_epoch(int64_t epoch_seconds);

} // namespace rtc