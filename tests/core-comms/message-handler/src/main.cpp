#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "core.pb.h"
#include <pb_decode.h>
#include <pb_encode.h>

#include "message-handler.hpp"

LOG_MODULE_REGISTER(core_comms); // NOLINT

std::optional<std::vector<uint8_t>> encode_core_message(const sat_CoreMessage &message)
{
    std::array<uint8_t, SAT_CORE_PB_H_MAX_SIZE> buffer{};
    pb_ostream_t stream = pb_ostream_from_buffer(buffer.data(), buffer.size());

    if (!pb_encode(&stream, sat_CoreMessage_fields, &message))
    {
        printk("Encoding failed: %s\n", PB_GET_ERROR(&stream));
        return {};
    }

    std::vector<uint8_t> out(buffer.begin(), buffer.begin() + stream.bytes_written);
    return {out};
}

ZTEST(message_handler, test_status)
{
    sat_CoreMessage message{};
    message.message.status.camera = static_cast<sat_CoreStatus_Status>(Status::offline); // NOLINT
    message.which_message         = sat_CoreMessage_status_tag;

    auto buffer = encode_core_message(message);
    zassert_true(buffer.has_value());

    auto out = core_comms::parse_request(*buffer);
    zassert_true(out.has_value());
    zassert_true(std::holds_alternative<CoreStatusEvent>(out.value()));

    zassert_equal(get<CoreStatusEvent>(out.value()).camera, Status::offline);
}

ZTEST(message_handler, test_satmsg)
{
    sat_CoreMessage message{};
    std::strcpy(message.message.sat.message, "MYDATA"); // NOLINT
    message.which_message = sat_CoreMessage_sat_tag;

    auto buffer = encode_core_message(message);
    zassert_true(buffer.has_value());

    auto out = core_comms::parse_request(*buffer);
    zassert_true(out.has_value());
    zassert_true(std::holds_alternative<SatComMsgEvent>(out.value()));

    std::array<char, sat_comm_msg_max_size> expected{};
    std::strcpy(expected.data(), message.message.sat.message); // NOLINT
    zassert_equal(get<SatComMsgEvent>(out.value()).data, expected);
}

ZTEST(message_handler, test_bad_comms)
{
    // NOLINTNEXTLINE
    std::array<uint8_t, 1> values = {'A'};
    auto                   out    = core_comms::parse_request(values);
    zassert_false(out.has_value());
    zassert_equal(out.error(), core_comms::Error::decode_failure);
}

ZTEST(message_handler, test_sys_init)
{
    sat_CoreMessage message{};
    message.which_message = sat_CoreMessage_init_tag;
    message.msg_id        = 7;
    std::strcpy(message.message.init.id, "ID-1");               // NOLINT
    std::strcpy(message.message.init.addr, "ADDR-1");           // NOLINT
    std::strcpy(message.message.init.sec_key, "SECRET");        // NOLINT
    std::strcpy(message.message.init.rconf, "RCONF-1");         // NOLINT
    std::strcpy(message.message.init.hostname, "marlin");       // NOLINT
    std::strcpy(message.message.init.wifi_network, "wifi-net"); // NOLINT
    message.message.init.time = 1787236205;                     // NOLINT

    auto buffer = encode_core_message(message);
    zassert_true(buffer.has_value());

    auto out = core_comms::parse_request(*buffer);
    zassert_true(out.has_value());
    zassert_true(std::holds_alternative<SysInitEvent>(out.value()));

    const auto &event = std::get<SysInitEvent>(out.value());
    zassert_equal(event.msg_id, 7);
    zassert_equal(std::strcmp(event.id.data(), "ID-1"), 0);
    zassert_equal(std::strcmp(event.addr.data(), "ADDR-1"), 0);
    zassert_equal(std::strcmp(event.sec_key.data(), "SECRET"), 0);
    zassert_equal(std::strcmp(event.rconf.data(), "RCONF-1"), 0);
    zassert_equal(std::strcmp(event.hostname.data(), "marlin"), 0);
    zassert_equal(std::strcmp(event.wifi_network.data(), "wifi-net"), 0);
    zassert_equal(event.time, 1787236205);
}

ZTEST(message_handler, test_reset)
{
    sat_CoreMessage message{};
    message.which_message = sat_CoreMessage_reset_tag;
    message.msg_id        = 12;

    auto buffer = encode_core_message(message);
    zassert_true(buffer.has_value());

    auto out = core_comms::parse_request(*buffer);
    zassert_true(out.has_value());
    zassert_true(std::holds_alternative<ResetEvent>(out.value()));
    zassert_equal(std::get<ResetEvent>(out.value()).msg_id, 12);
}

ZTEST(message_handler, test_generate_response_with_battery)
{
    constexpr uint32_t msg_id = 3;
    sat_BatteryStatus  battery{
         .set     = true,
         .voltage = 12.5F,
         .current = -1.25F,
    };

    auto response = core_comms::generate_response(
        msg_id, sat_LpmReply_Response_OK, battery);
    zassert_true(response.has_value());

    sat_LpmReply reply{};
    pb_istream_t stream =
        pb_istream_from_buffer(response->data(), response->size());
    zassert_true(pb_decode(&stream, sat_LpmReply_fields, &reply));

    zassert_equal(reply.msg_id, msg_id);
    zassert_equal(reply.response, sat_LpmReply_Response_OK);
    zassert_true(reply.has_batteryStatus);
    zassert_within(12.5, static_cast<double>(reply.batteryStatus.voltage), 0.001);
    zassert_within(-1.25, static_cast<double>(reply.batteryStatus.current), 0.001);
}

ZTEST(message_handler, test_generate_response_without_battery)
{
    constexpr uint32_t msg_id = 4;

    auto response = core_comms::generate_response(msg_id, sat_LpmReply_Response_ERROR);
    zassert_true(response.has_value());

    sat_LpmReply reply{};
    pb_istream_t stream =
        pb_istream_from_buffer(response->data(), response->size());
    zassert_true(pb_decode(&stream, sat_LpmReply_fields, &reply));

    zassert_equal(reply.msg_id, msg_id);
    zassert_equal(reply.response, sat_LpmReply_Response_ERROR);
    zassert_false(reply.has_batteryStatus);
}

ZTEST_SUITE(message_handler, NULL, NULL, NULL, NULL, NULL);
