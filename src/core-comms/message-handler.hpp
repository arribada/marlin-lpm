#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <variant>

#include <etl/vector.h>

#include <core.pb.h>

#include "core-comms.hpp"
#include "core_status_event.hpp"
#include "harbour_mode_event.hpp"
#include "reset_event.hpp"
#include "satcom_msg_event.hpp"
#include "sys_init_event.hpp"

namespace core_comms
{

using IncomingEvent =
    std::variant<CoreStatusEvent, SatComMsgEvent, HarbourModeEvent, ResetEvent, SysInitEvent>;

[[nodiscard]] Result<IncomingEvent> parse_request(
    const std::span<const uint8_t> incoming_packet);

[[nodiscard]] Result<etl::vector<uint8_t, sat_LpmReply_size>> generate_response(
    const uint32_t                   msg_id,
    sat_LpmReply_Response            response,
    std::optional<sat_BatteryStatus> battery = {});

} // namespace core_comms
