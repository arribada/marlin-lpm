#pragma once

#include <array>
#include <cstdint>

#include <zephyr/zbus/zbus.h>

#include <core.pb.h>

constexpr uint32_t sys_init_string_size = 64;

struct SysInitEvent
{
    uint32_t                               msg_id;
    std::array<char, sys_init_string_size> id;
    std::array<char, sys_init_string_size> addr;
    std::array<char, sys_init_string_size> sec_key;
    std::array<char, sys_init_string_size> rconf;
    std::array<char, sys_init_string_size> hostname;
    std::array<char, sys_init_string_size> wifi_network;
    int64_t                                time = 0;
};

ZBUS_CHAN_DECLARE(SysInitEventChan);

inline void publish_zbus_event(const SysInitEvent &event)
{
    int ret =
        zbus_chan_pub(&SysInitEventChan, static_cast<const void *>(&event), K_NO_WAIT);
    __ASSERT(ret == 0, "zbus_chan_pub should not fail, check usage");
}
