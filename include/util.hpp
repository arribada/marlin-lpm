#pragma once

#include <type_traits>
#include <zephyr/zbus/zbus.h>

#include <core.pb.h>

// Must follow Status enum in core.proto
enum class Status : uint8_t
{
    online  = 0,
    offline = 1,
    error   = 2,
    unknown = 3,
};

static_assert(static_cast<int>(Status::online) ==
              _sat_CoreStatus_Status::sat_CoreStatus_Status_ONLINE);
static_assert(static_cast<int>(Status::offline) ==
              _sat_CoreStatus_Status::sat_CoreStatus_Status_OFFLINE);
static_assert(static_cast<int>(Status::error) ==
              _sat_CoreStatus_Status::sat_CoreStatus_Status_ERROR);
static_assert(static_cast<int>(Status::unknown) ==
              _sat_CoreStatus_Status::sat_CoreStatus_Status_UNKNOWN);

template <typename TEvent> inline const TEvent *get_zbus_event(const zbus_channel *channel)
{
    return static_cast<const TEvent *>(zbus_chan_const_msg(channel));
}

template <typename E> constexpr std::underlying_type_t<E> to_underlying(E value)
{
    return static_cast<std::underlying_type_t<E>>(value);
}

template <typename E, typename T> constexpr E to_enum(T value)
{
    return static_cast<E>(value);
}

template <class... Ts> struct overload : Ts...
{
    using Ts::operator()...;
};

static constexpr int zbus_high_priority    = 1;
static constexpr int zbus_default_priority = 2;
