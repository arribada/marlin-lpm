#define MODULE core_comms

#include <cassert>
#include <cstring>
#include <expected>

#include <zephyr/logging/log.h>

#include <etl/vector.h>

#include <magic_enum.hpp>

#include "core.pb.h"
#include <pb_decode.h>
#include <pb_encode.h>

#include "lpm_response_event.hpp"
#include "message-handler.hpp"
#include "reset_event.hpp"

LOG_MODULE_DECLARE(MODULE, CONFIG_CORE_COMMS_MODULE_LOG_LEVEL); // NOLINT

namespace core_comms
{

Result<IncomingEvent> parse_request(const std::span<const uint8_t> incoming_packet)
{
    sat_CoreMessage message = sat_CoreMessage_init_zero;
    pb_istream_t    stream =
        pb_istream_from_buffer(incoming_packet.data(), incoming_packet.size());

    bool status = pb_decode(&stream, sat_CoreMessage_fields, &message);
    if (!status)
    {
        return std::unexpected(Error::decode_failure);
    }

    Result<IncomingEvent> ret = std::unexpected(Error::unknown_message);

    switch (message.which_message)
    {
    case sat_CoreMessage_sat_tag:
    {
        SatComMsgEvent event{.msg_id = message.msg_id,
                             .data =
                                 std::to_array(message.message.sat.message)}; // NOLINT
        LOG_DBG("Parsed - SatComMessageEvent");
        LOG_DBG("msg_id: %d", event.msg_id);
        LOG_DBG("message: %s", event.data.data());
        ret = event;
        break;
    }
    case sat_CoreMessage_status_tag:
    {
        CoreStatusEvent event{
            .msg_id     = message.msg_id,
            .camera     = static_cast<Status>(message.message.status.camera),
            .controller = static_cast<Status>(message.message.status.controller),
            .cloud_connection =
                static_cast<Status>(message.message.status.cloud_connection),
            .cellular      = static_cast<Status>(message.message.status.cellular),
            .wifi          = static_cast<Status>(message.message.status.wifi),
            .gps           = static_cast<Status>(message.message.status.gps),
            .tailscale     = static_cast<Status>(message.message.status.tailscale),
            .local_dash    = static_cast<Status>(message.message.status.local_dash),
            .storage_usage = message.message.status.storage_usage,
            .tailscale_ip  = std::to_array(message.message.status.tailscale_ip),
        };

        // NOLINTBEGIN
        LOG_DBG("Parsed - CoreStatusEvent:");
        LOG_DBG("msg_id: %d", event.msg_id);
        LOG_DBG("camera: %s", magic_enum::enum_name(event.camera).data());
        LOG_DBG("controller: %s", magic_enum::enum_name(event.controller).data());
        LOG_DBG("cloud_connection: %s",
                magic_enum::enum_name(event.cloud_connection).data());
        LOG_DBG("cellular: %s", magic_enum::enum_name(event.cellular).data());
        LOG_DBG("wifi: %s", magic_enum::enum_name(event.wifi).data());
        LOG_DBG("gps: %s", magic_enum::enum_name(event.gps).data());
        LOG_DBG("tailscale: %s", magic_enum::enum_name(event.tailscale).data());
        LOG_DBG("local_dash: %s", magic_enum::enum_name(event.local_dash).data());
        LOG_DBG("storage: %f", static_cast<double>(event.storage_usage));
        LOG_DBG("tailscale_ip: %s", event.tailscale_ip.data());
        // NOLINTEND
        ret = event;
        break;
    }
    case sat_CoreMessage_mode_tag:
    {
        HarbourModeEvent event{
            .msg_id = message.msg_id,
            .cmd    = message.message.mode.cmd, // NOLINT
        };
        LOG_DBG("Parsed - HabourModeEvent:");
        LOG_DBG("msg_id: %d", event.msg_id);
        LOG_DBG("cmd: %s", magic_enum::enum_name(event.cmd).data()); // NOLINT
        ret = event;
        break;
    }
    case sat_CoreMessage_init_tag:
    {
        SysInitEvent event = {
            .msg_id       = message.msg_id,
            .id           = std::to_array(message.message.init.id),           // NOLINT
            .addr         = std::to_array(message.message.init.addr),         // NOLINT
            .sec_key      = std::to_array(message.message.init.sec_key),      // NOLINT
            .rconf        = std::to_array(message.message.init.rconf),        // NOLINT
            .hostname     = std::to_array(message.message.init.hostname),     // NOLINT
            .wifi_network = std::to_array(message.message.init.wifi_network), // NOLINT
            .time         = message.message.init.time,
        };
        // NOLINTBEGIN
        LOG_DBG("Parsed - SysInitEvent:");
        LOG_DBG("msg_id: %d", event.msg_id);
        LOG_DBG("addr: %s", event.addr.data());
        LOG_DBG("sec_key: %s", event.sec_key.data());
        LOG_DBG("rconf: %s", event.rconf.data());
        LOG_DBG("hostname: %s", event.hostname.data());
        LOG_DBG("wifi_network: %s", event.wifi_network.data());
        // NOLINTEND

        ret = event;
        break;
    }
    case sat_CoreMessage_reset_tag:
    {
        ResetEvent event{
            .msg_id = message.msg_id,
        };
        LOG_DBG("Parsed - ResetMessageEvent");
        LOG_DBG("msg_id: %d", event.msg_id);
        ret = event;
        break;
    }
    default:
    {
        k_oops();
    }
    }

    return ret;
}

Result<etl::vector<uint8_t, sat_LpmReply_size>> generate_response(
    const uint32_t                   msg_id,
    sat_LpmReply_Response            response,
    std::optional<sat_BatteryStatus> battery)
{
    LOG_INF("LPM Responding - msg_id: %d - Response %s",
            msg_id,
            response == sat_LpmReply_Response_OK ? "Ok" : "Error");

    etl::vector<uint8_t, sat_LpmReply_size> buffer;

    pb_ostream_t stream = pb_ostream_from_buffer(buffer.data(), buffer.available());

    sat_LpmReply reply{};
    reply.msg_id            = msg_id;
    reply.response          = response;
    reply.has_batteryStatus = battery.has_value();
    if (battery.has_value())
    {
        reply.batteryStatus = battery.value();
    }

    if (!pb_encode(&stream, sat_LpmReply_fields, &reply))
    {
        printk("Encoding failed: %s\n", PB_GET_ERROR(&stream));
        return std::unexpected(Error::encode_failure);
    }

    buffer.uninitialized_resize(stream.bytes_written);
    return buffer;
}

} // namespace core_comms
