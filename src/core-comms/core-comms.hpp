#pragma once

#include <cstdint>
#include <expected>

namespace core_comms
{

enum class Error : uint8_t
{
    packet_too_small,
    decode_failure,
    unknown_message,
    encode_failure
};

template <typename T = void> using Result = std::expected<T, Error>;

} // namespace core_comms
