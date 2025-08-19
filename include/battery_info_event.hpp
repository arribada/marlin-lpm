#pragma once

#include <zephyr/zbus/zbus.h>

struct BatteryInfoEvent
{
    float   voltage;
    float   current;
    float   power;
    bool    valid;
    bool    operator==(const BatteryInfoEvent &) const = default;
};

ZBUS_CHAN_DECLARE(BatteryInfoEventChan);

inline void publish_zbus_event(const BatteryInfoEvent &event)
{
    int ret = zbus_chan_pub(&BatteryInfoEventChan, static_cast<const void *>(&event), K_NO_WAIT);
    __ASSERT(ret == 0, "zbus_chan_pub should not fail, check usage");
}
