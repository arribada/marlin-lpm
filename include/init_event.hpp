#pragma once

#include <zephyr/zbus/zbus.h>

struct InitEvent
{
};

ZBUS_CHAN_DECLARE(init_event_chan);

inline void publish_zbus_event(const InitEvent &event)
{
    int ret = zbus_chan_pub(&init_event_chan, static_cast<const void *>(&event), K_NO_WAIT);
    __ASSERT(ret == 0, "zbus_chan_pub should not fail, check usage");
}
