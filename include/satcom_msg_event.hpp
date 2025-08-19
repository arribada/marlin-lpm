#pragma once

#include <array>
#include <cstdint>

#include <zephyr/zbus/zbus.h>

constexpr uint32_t sat_comm_msg_max_size = 256;

using SatComMsgData = std::array<char, sat_comm_msg_max_size>;

struct SatComMsgEvent
{
    uint32_t      msg_id;
    SatComMsgData data;
};

ZBUS_CHAN_DECLARE(SatComMsgEventChan);

inline void publish_zbus_event(const SatComMsgEvent &event)
{
    int ret = zbus_chan_pub(&SatComMsgEventChan, static_cast<const void *>(&event), K_NO_WAIT);
    __ASSERT(ret == 0, "zbus_chan_pub should not fail, check usage");
}
