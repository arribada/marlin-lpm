
#pragma once

#include <zephyr/zbus/zbus.h>

struct CoreTimeoutEvent
{
};

ZBUS_CHAN_DECLARE(CoreTimeoutEventChan);

inline void publish_zbus_event(const CoreTimeoutEvent &event)
{
    int ret = zbus_chan_pub(&CoreTimeoutEventChan, static_cast<const void *>(&event), K_NO_WAIT);
    __ASSERT(ret == 0, "zbus_chan_pub should not fail, check usage");
}
