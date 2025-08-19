#pragma once

#include <zephyr/zbus/zbus.h>

#include <core.pb.h>

struct HarbourModeEvent
{
    uint32_t                msg_id;
    sat_HarbourMode_Command cmd;
};

ZBUS_CHAN_DECLARE(HarbourModeEventChan);

inline void publish_zbus_event(const HarbourModeEvent &event)
{
    int ret = zbus_chan_pub(&HarbourModeEventChan, static_cast<const void *>(&event), K_NO_WAIT);
    __ASSERT(ret == 0, "zbus_chan_pub should not fail, check usage");
}
