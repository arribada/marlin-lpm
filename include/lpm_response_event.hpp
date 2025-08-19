#pragma once

#include <optional>

#include <zephyr/zbus/zbus.h>

#include <core.pb.h>

struct LpmResponseEvent
{
    uint32_t                        msg_id;
    sat_LpmReply_Response           response;
    std::optional<sat_BatteryStatus> battery;
    bool                            operator==(const LpmResponseEvent &) const = default;
};

ZBUS_CHAN_DECLARE(LpmResponseEventChan);

inline void publish_zbus_event(const LpmResponseEvent &event)
{
    int ret = zbus_chan_pub(&LpmResponseEventChan, static_cast<const void *>(&event), K_NO_WAIT);
    __ASSERT(ret == 0, "zbus_chan_pub should not fail, check usage");
}
