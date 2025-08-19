#pragma once

#include <array>

#include <zephyr/zbus/zbus.h>

#include "util.hpp"

constexpr uint32_t satcom_status_string_size = 64;

struct CoreStatusEvent
{
    uint32_t                                    msg_id;
    Status                                      camera;
    Status                                      controller;
    Status                                      cloud_connection;
    Status                                      cellular;
    Status                                      wifi;
    Status                                      gps;
    Status                                      tailscale;
    Status                                      local_dash;
    float                                       storage_usage;
    std::array<char, satcom_status_string_size> tailscale_ip;
    bool                                        operator==(const CoreStatusEvent &) const = default;
};

ZBUS_CHAN_DECLARE(CoreStatusEventChan);

inline void publish_zbus_event(const CoreStatusEvent &event)
{
    int ret = zbus_chan_pub(&CoreStatusEventChan, static_cast<const void *>(&event), K_NO_WAIT);
    __ASSERT(ret == 0, "zbus_chan_pub should not fail, check usage");
}
