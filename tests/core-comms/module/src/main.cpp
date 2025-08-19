#include <array>
#include <cstring>
#include <expected>
#include <optional>
#include <variant>

#include <vector>
#include <zephyr/device.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "core.pb.h"
#include <pb_decode.h>
#include <pb_encode.h>

#include "core_status_event.hpp"
#include "core_timeout_event.hpp"
#include "harbour_mode_event.hpp"
#include "init_event.hpp"
#include "lpm_response_event.hpp"
#include "reset_event.hpp"
#include "satcom_msg_event.hpp"
#include "state_event.hpp"
#include "sys_init_event.hpp"
#include "util.hpp"

#define UART_NODE DT_NODELABEL(uart0)

const static device *dev = DEVICE_DT_GET(UART_NODE);

static constexpr int msgq_alignment = 1;
static constexpr int msgq_size      = 10;

using Events = std::
    variant<CoreStatusEvent, CoreTimeoutEvent, SatComMsgEvent, LpmResponseEvent, HarbourModeEvent, ResetEvent, SysInitEvent>;

K_MSGQ_DEFINE(event_msgq, sizeof(Events), msgq_size, msgq_alignment);

static std::expected<Events, int> get_event_from_queue()
{
    Events event{};
    int    ret = k_msgq_get(&event_msgq, &event, K_MSEC(100));
    if (ret != 0)
    {
        return std::unexpected(ret);
    }
    return event;
}

static std::expected<sat_LpmReply, int> get_reply()
{
    std::array<uint8_t, sat_LpmReply_size> buffer{};

    uint32_t bytes_copied = uart_emul_get_tx_data(dev, buffer.data(), buffer.size());

    sat_LpmReply message{};
    pb_istream_t stream = pb_istream_from_buffer(buffer.data(), bytes_copied);

    bool status = pb_decode(&stream, sat_LpmReply_fields, &message);
    if (!status)
    {
        printk("Encoding failed: %s\n", PB_GET_ERROR(&stream));
        return std::unexpected(-1);
    }

    return message;
}

// Polls the UART TX buffer until a complete reply settles or the timeout
// elapses. Avoids fixed sleeps that race with core_comms' worker thread.
static std::expected<sat_LpmReply, int> get_reply_wait(int64_t timeout_ms)
{
    std::vector<uint8_t> accumulated;
    int64_t              start   = k_uptime_get();
    int                  idle_ms = 0;

    while (k_uptime_get() - start < timeout_ms)
    {
        std::array<uint8_t, sat_LpmReply_size> chunk{};
        uint32_t bytes = uart_emul_get_tx_data(dev, chunk.data(), chunk.size());
        if (bytes > 0)
        {
            accumulated.insert(accumulated.end(), chunk.begin(), chunk.begin() + bytes);
            idle_ms = 0;
        }
        else if (!accumulated.empty() && ++idle_ms >= 2)
        {
            break;
        }
        k_msleep(1);
    }

    if (accumulated.empty())
    {
        return std::unexpected(-1);
    }

    sat_LpmReply message{};
    pb_istream_t stream = pb_istream_from_buffer(accumulated.data(), accumulated.size());
    if (!pb_decode(&stream, sat_LpmReply_fields, &message))
    {
        printk("Encoding failed: %s\n", PB_GET_ERROR(&stream));
        return std::unexpected(-1);
    }

    return message;
}

std::optional<std::vector<uint8_t>> encode_core_message(const sat_CoreMessage &message)
{
    std::array<uint8_t, sat_CoreMessage_size> buffer{};
    pb_ostream_t stream = pb_ostream_from_buffer(buffer.data(), buffer.size());

    if (!pb_encode(&stream, sat_CoreMessage_fields, &message))
    {
        return {};
    }

    std::vector<uint8_t> out(buffer.begin(), buffer.begin() + stream.bytes_written);
    return {out};
}

void *init()
{
    publish_zbus_event(InitEvent{});
    k_sleep(K_MSEC(10));

    zassert_true(device_is_ready(dev));
    uart_emul_flush_rx_data(dev);
    uart_emul_flush_tx_data(dev);
    uart_emul_set_release_buffer_on_timeout(dev, true);

    return nullptr;
}

ZTEST(core_comms, test_no_timeout_in_harbour_mode)
{
    StateEvent state_event{.state = State::harbour};
    publish_zbus_event(state_event);

    k_sleep(K_SECONDS(35));

    auto event = get_event_from_queue();
    zassert_false(event.has_value());

    state_event.state = State::monitoring;
    publish_zbus_event(state_event);
}

ZTEST(core_comms, test_timeout)
{
    k_sleep(K_SECONDS(35));

    auto event = get_event_from_queue();
    zassert_true(event.has_value());
    zassert_true(std::holds_alternative<CoreTimeoutEvent>(event.value()));
}

ZTEST(core_comms, test_response_timeout)
{
    k_msgq_purge(&event_msgq);

    // Drain any in-flight request from a previous test so its reply can't leak in
    uart_emul_flush_tx_data(dev);
    k_sleep(K_MSEC(6000));
    uart_emul_flush_tx_data(dev);

    constexpr uint32_t msg_id = 9;
    sat_CoreMessage    message{};
    message.message.mode.cmd = sat_HarbourMode_Command_ENTER; // NOLINT
    message.which_message    = sat_CoreMessage_mode_tag;
    message.msg_id           = msg_id;

    auto buffer = encode_core_message(message);
    zassert_true(buffer.has_value());

    uart_emul_put_rx_data(dev, buffer->data(), buffer->size());

    k_msleep(5200); // NOLINT wait for the 5s response timeout

    auto reply = get_reply();
    zassert_true(reply.has_value());
    zassert_equal(reply->msg_id, msg_id);
    zassert_equal(reply->response, sat_LpmReply_Response_ERROR);

    auto event = get_event_from_queue();
    zassert_true(event.has_value());
    zassert_true(std::holds_alternative<HarbourModeEvent>(event.value()));
}

ZTEST(core_comms, test_harbour_mode)
{
    sat_CoreMessage message{};
    message.message.mode.cmd = sat_HarbourMode_Command_ENTER; // NOLINT
    message.which_message    = sat_CoreMessage_mode_tag;
    message.msg_id           = 1;

    auto buffer = encode_core_message(message);
    zassert_true(buffer.has_value());

    uart_emul_put_rx_data(dev, buffer->data(), buffer->size());

    auto event = get_event_from_queue();
    zassert_true(event.has_value());
    zassert_true(std::holds_alternative<HarbourModeEvent>(event.value()));

    // Respond so core_comms does not fall through to its 5s timeout reply,
    // which would otherwise leak into the next test.
    publish_zbus_event(LpmResponseEvent{
        .msg_id   = 1,
        .response = sat_LpmReply_Response_OK,
        .battery  = std::nullopt,
    });

    auto reply = get_reply_wait(1000);
    zassert_true(reply.has_value());
    zassert_equal(reply->response, sat_LpmReply_Response_OK);

    StateEvent state_event{.state = State::monitoring};
    publish_zbus_event(state_event);
}

ZTEST(core_comms, test_good_coms)
{
    constexpr uint32_t msg_id = 2;
    sat_CoreMessage    message{};
    message.message.status.camera = static_cast<sat_CoreStatus_Status>(Status::online); // NOLINT
    message.which_message         = sat_CoreMessage_status_tag;
    message.msg_id                = msg_id;

    auto buffer = encode_core_message(message);
    zassert_true(buffer.has_value());
    uart_emul_flush_tx_data(dev);

    uart_emul_put_rx_data(dev, buffer->data(), buffer->size());

    auto event = get_event_from_queue();
    zassert_true(event.has_value());
    zassert_true(std::holds_alternative<CoreStatusEvent>(event.value()));
    zassert_equal(get<CoreStatusEvent>(event.value()).camera, Status::online);

    // The monitor module answers status requests, so wait for its reply.
    auto reply = get_reply_wait(1000);
    zassert_true(reply.has_value());
    zassert_equal(reply->msg_id, msg_id);
    zassert_equal(reply->response, sat_LpmReply_Response_OK);
}

ZTEST(core_comms, test_status_reply)
{
    constexpr uint32_t msg_id = 5;
    sat_CoreMessage    message{};
    message.msg_id                = msg_id;
    message.message.status.camera = static_cast<sat_CoreStatus_Status>(Status::online); // NOLINT
    message.which_message         = sat_CoreMessage_status_tag;

    auto buffer = encode_core_message(message);
    zassert_true(buffer.has_value());
    uart_emul_flush_tx_data(dev);

    uart_emul_put_rx_data(dev, buffer->data(), buffer->size());

    auto event = get_event_from_queue();
    zassert_true(event.has_value());
    zassert_true(std::holds_alternative<CoreStatusEvent>(event.value()));
    zassert_equal(get<CoreStatusEvent>(event.value()).camera, Status::online);

    auto reply = get_reply_wait(1000);
    zassert_true(reply.has_value());
    zassert_equal(reply->msg_id, msg_id);
    zassert_equal(reply->response, sat_LpmReply_Response_OK);
    zassert_true(reply->has_batteryStatus, "Status reply should include battery data");
}

ZTEST(core_comms, test_sat_reply)
{
    constexpr uint32_t msg_id = 5;
    sat_CoreMessage    message{};

    message.msg_id        = msg_id;
    message.which_message = sat_CoreMessage_sat_tag;
    std::strcpy(message.message.sat.message, "MYDATA"); // NOLINT

    auto buffer = encode_core_message(message);
    zassert_true(buffer.has_value());
    uart_emul_flush_tx_data(dev);

    uart_emul_put_rx_data(dev, buffer->data(), buffer->size());

    auto event = get_event_from_queue();
    zassert_true(event.has_value());
    zassert_true(std::holds_alternative<SatComMsgEvent>(event.value()));
    std::array<char, sat_comm_msg_max_size> expected{};
    std::strcpy(expected.data(), message.message.sat.message);        // NOLINT
    zassert_equal(get<SatComMsgEvent>(event.value()).data, expected); // NOLINT

    LpmResponseEvent resp{
        .msg_id   = msg_id,
        .response = sat_LpmReply_Response_OK,
        .battery  = std::nullopt,
    };

    publish_zbus_event(resp);

    auto reply = get_reply_wait(1000);
    zassert_true(reply.has_value());
    zassert_equal(reply->msg_id, msg_id);
    zassert_equal(reply->response, sat_LpmReply_Response_OK);
}

ZTEST(core_comms, test_sat_bad_parse)
{
    std::array<uint8_t, 10> buffer{}; // NOLINT
    uart_emul_flush_tx_data(dev);

    uart_emul_put_rx_data(dev, buffer.data(), buffer.size());

    auto reply = get_reply_wait(1000);
    zassert_true(reply.has_value());
    zassert_equal(reply->msg_id, 0);
    zassert_equal(reply->response, sat_LpmReply_Response_ERROR);
}

ZTEST(core_comms, test_invalid_msg_id)
{
    sat_CoreMessage message{};
    message.message.status.camera = static_cast<sat_CoreStatus_Status>(Status::online); // NOLINT
    message.which_message         = sat_CoreMessage_status_tag;
    message.msg_id                = 0;

    auto buffer = encode_core_message(message);
    zassert_true(buffer.has_value());
    uart_emul_flush_tx_data(dev);

    uart_emul_put_rx_data(dev, buffer->data(), buffer->size());

    auto reply = get_reply_wait(1000);
    zassert_true(reply.has_value());
    zassert_equal(reply->msg_id, 0);
    zassert_equal(reply->response, sat_LpmReply_Response_ERROR);
}

ZTEST(core_comms, test_reset_tag)
{
    constexpr uint32_t msg_id = 8;
    sat_CoreMessage    message{};
    message.which_message = sat_CoreMessage_reset_tag;
    message.msg_id        = msg_id;

    auto buffer = encode_core_message(message);
    zassert_true(buffer.has_value());
    uart_emul_flush_tx_data(dev);

    uart_emul_put_rx_data(dev, buffer->data(), buffer->size());

    auto event = get_event_from_queue();
    zassert_true(event.has_value());
    zassert_true(std::holds_alternative<ResetEvent>(event.value()));
    zassert_equal(std::get<ResetEvent>(event.value()).msg_id, msg_id);

    publish_zbus_event(LpmResponseEvent{
        .msg_id   = msg_id,
        .response = sat_LpmReply_Response_OK,
        .battery  = std::nullopt,
    });

    auto reply = get_reply_wait(1000);
    zassert_true(reply.has_value());
    zassert_equal(reply->msg_id, msg_id);
    zassert_equal(reply->response, sat_LpmReply_Response_OK);
}

ZTEST(core_comms, test_sys_init_tag)
{
    constexpr uint32_t msg_id = 11;
    sat_CoreMessage    message{};
    message.which_message = sat_CoreMessage_init_tag;
    message.msg_id        = msg_id;
    std::strcpy(message.message.init.id, "ID");         // NOLINT
    std::strcpy(message.message.init.hostname, "HOST"); // NOLINT

    auto buffer = encode_core_message(message);
    zassert_true(buffer.has_value());
    uart_emul_flush_tx_data(dev);

    uart_emul_put_rx_data(dev, buffer->data(), buffer->size());

    auto event = get_event_from_queue();
    zassert_true(event.has_value());
    zassert_true(std::holds_alternative<SysInitEvent>(event.value()));
    zassert_equal(std::get<SysInitEvent>(event.value()).msg_id, msg_id);
    zassert_equal(std::strcmp(std::get<SysInitEvent>(event.value()).hostname.data(), "HOST"), 0);

    publish_zbus_event(LpmResponseEvent{
        .msg_id   = msg_id,
        .response = sat_LpmReply_Response_OK,
        .battery  = std::nullopt,
    });

    auto reply = get_reply_wait(1000);
    zassert_true(reply.has_value());
    zassert_equal(reply->msg_id, msg_id);
    zassert_equal(reply->response, sat_LpmReply_Response_OK);
}

ZTEST_SUITE(core_comms, NULL, init, NULL, NULL, NULL);

template <typename TEvent> void push_event_to_queue(const zbus_channel *channel)
{
    const auto *event_ptr = get_zbus_event<TEvent>(channel);
    Events      event     = *event_ptr;
    k_msgq_put(&event_msgq, &event, K_NO_WAIT);
};

static void listener_callback(const struct zbus_channel *channel) // NOLINT
{
    if (channel == &CoreStatusEventChan)
    {
        push_event_to_queue<CoreStatusEvent>(channel);
    }
    else if (channel == &CoreTimeoutEventChan)
    {
        push_event_to_queue<CoreTimeoutEvent>(channel);
    }
    else if (channel == &SatComMsgEventChan)
    {
        push_event_to_queue<SatComMsgEvent>(channel);
    }
    else if (channel == &HarbourModeEventChan)
    {
        push_event_to_queue<HarbourModeEvent>(channel);
    }
    else if (channel == &ResetEventChan)
    {
        push_event_to_queue<ResetEvent>(channel);
    }
    else if (channel == &SysInitEventChan)
    {
        push_event_to_queue<SysInitEvent>(channel);
    }
    else
    {
        __ASSERT(false, "Channel %s is being listened to but not processed", channel->name);
    }
}

ZBUS_LISTENER_DEFINE(test_listener, listener_callback);
ZBUS_CHAN_ADD_OBS(CoreStatusEventChan, test_listener, zbus_default_priority);
ZBUS_CHAN_ADD_OBS(CoreTimeoutEventChan, test_listener, zbus_default_priority);
ZBUS_CHAN_ADD_OBS(SatComMsgEventChan, test_listener, zbus_default_priority);
ZBUS_CHAN_ADD_OBS(HarbourModeEventChan, test_listener, zbus_default_priority);
ZBUS_CHAN_ADD_OBS(ResetEventChan, test_listener, zbus_default_priority);
ZBUS_CHAN_ADD_OBS(SysInitEventChan, test_listener, zbus_default_priority);
