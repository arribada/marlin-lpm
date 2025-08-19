#pragma once

#include <array>
#include <cstdint>

#include <zephyr/zbus/zbus.h>

#include <core.pb.h>

struct ResetEvent
{
    uint32_t msg_id = 0;
};

ZBUS_CHAN_DECLARE(ResetEventChan);

inline void publish_zbus_event(const ResetEvent &event)
{
    int ret =
        zbus_chan_pub(&ResetEventChan, static_cast<const void *>(&event), K_NO_WAIT);
    __ASSERT(ret == 0, "zbus_chan_pub should not fail, check usage");
}
