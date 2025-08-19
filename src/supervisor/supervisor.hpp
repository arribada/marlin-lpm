#pragma once

#include <cstdint>
#include <expected>

namespace supervisor
{

enum class Error : uint8_t
{
    error
};

template <typename T = void> using Result = std::expected<T, Error>;

void reset_core();

} // namespace supervisor
