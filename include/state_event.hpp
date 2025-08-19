#pragma once

#include <zephyr/zbus/zbus.h>

enum class State : uint8_t
{
    monitoring,
    system_error,
    harbour,
    offline
};

struct StateEvent
{
    State state;
    bool  operator==(const StateEvent &) const = default;
};

ZBUS_CHAN_DECLARE(StateEventChan);

inline void publish_zbus_event(const StateEvent &event)
{
    int ret =
        zbus_chan_pub(&StateEventChan, static_cast<const void *>(&event), K_NO_WAIT);
    __ASSERT(ret == 0, "zbus_chan_pub should not fail, check usage");
}
