#define MODULE satcom

#include <cstring>
#include <optional>
#include <variant>

#include <etl/string.h>
#include <etl/vector.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zbus/zbus.h>

#include "util.hpp"

#include "init_event.hpp"
#include "lpm_response_event.hpp"
#include "satcom_msg_event.hpp"
#include "state_event.hpp"
#include "sys_init_event.hpp"

#include "rtc/rtc.hpp"

#include "satcom.hpp"

#include "argos-smd/argos_smd.h"

LOG_MODULE_REGISTER(MODULE, CONFIG_SATCOM_MODULE_LOG_LEVEL); // NOLINT

namespace satcom
{

K_MSGQ_DEFINE(event_msgq, sizeof(Events), 10, 4);

#ifdef UNIT_TESTING
// Test seam: lets tests reset the scheduler between shuffled test cases.
Scheduler *test_scheduler = nullptr;
#endif

static constexpr int satcom_stack_size = CONFIG_SATCOM_MODULE_STACK_SIZE;
static constexpr int satcom_priority   = CONFIG_SATCOM_MODULE_PRIORITY;

K_THREAD_STACK_DEFINE(satcom_stack_area, satcom_stack_size);

static struct k_thread satcom_thread;
static k_tid_t         satcom_tid;

void satcom_process(void * /*unused*/, void * /*unused*/, void * /*unused*/);

using SatResponse = etl::string<sat_comm_msg_max_size>;
K_MSGQ_DEFINE(sat_resp_msgq, sizeof(SatResponse), 10, 4);

static State system_state = State::offline;

static std::array<char, sys_init_string_size> hostname{};

static void argos_callback(const char *response, void *user_data)
{
    ARG_UNUSED(user_data);
    LOG_INF("Received response: %s, adding to queue", response);

    SatResponse resp = response;

    k_msgq_put(&sat_resp_msgq, &resp, K_NO_WAIT);
}

static std::optional<SatResponse> get_smd_response()
{
    LOG_INF("Waiting for response");

    SatResponse resp{};
    int         ret = k_msgq_get(&sat_resp_msgq, &resp, K_SECONDS(5)); // NOLINT
    if (ret != 0)
    {
        LOG_ERR("Failed to get a response");
        return {};
    }

    resp.repair(); // Required due to bytewise copy

    if (resp.find("+ERROR") != std::string::npos)
    {
        LOG_ERR("SMD Replied with Error");
        return {};
    }

    return resp;
}

static void hex_encode(const char *in, size_t len, char *out)
{
    static constexpr char hexdigits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < len; ++i)
    {
        auto byte        = static_cast<uint8_t>(in[i]);
        out[(i * 2)]     = hexdigits[(byte >> 4) & 0xF];
        out[(i * 2) + 1] = hexdigits[byte & 0xF];
    }
}

static sat_LpmReply_Response set_and_check(const device      *smd,
                                           const at_command_t cmd,
                                           char              *data)
{
    k_msgq_purge(&sat_resp_msgq);
    if (argos_set_command(smd, cmd, data) != 0)
    {
        LOG_ERR("Failed to construct messa&ge");
        return sat_LpmReply_Response_ERROR;
    }

    auto resp = get_smd_response();
    if (!resp.has_value())
    {
        return sat_LpmReply_Response_ERROR;
    }

    if (resp->find("+OK") == std::string::npos)
    {
        LOG_ERR("Failed verify return data: %s", resp.value().data());
        return sat_LpmReply_Response_ERROR;
    }

    // TODO: Add verification

    return sat_LpmReply_Response_OK;
}

static void handle_info_event(const device *smd, SysInitEvent &init)
{
    LOG_INF("New Init");

    hostname = init.hostname;

    if (init.time != 0)
    {
        if (!rtc::set_time_from_epoch(init.time))
        {
            LOG_ERR("Failed to set RTC time from init payload");
        }
    }

    LpmResponseEvent event{};
    event.msg_id   = init.msg_id;
    event.response = sat_LpmReply_Response_OK;

    LOG_INF("Setting ID to %s", init.id.data());
    if (set_and_check(smd, AT_ID, init.id.data()) == sat_LpmReply_Response_ERROR)
    {
        event.response = sat_LpmReply_Response_ERROR;
        publish_zbus_event(event);
        return;
    }

    LOG_INF("Setting ADDR to %s", init.addr.data());
    if (set_and_check(smd, AT_ADDR, init.addr.data()) == sat_LpmReply_Response_ERROR)
    {
        event.response = sat_LpmReply_Response_ERROR;
        publish_zbus_event(event);
        return;
    }

    LOG_INF("Setting SECKEY to %s", init.sec_key.data());
    if (set_and_check(smd, AT_SECKEY, init.sec_key.data()) == sat_LpmReply_Response_ERROR)
    {
        event.response = sat_LpmReply_Response_ERROR;
        publish_zbus_event(event);
        return;
    }

    LOG_INF("Setting RCONF to %s", init.rconf.data());
    if (set_and_check(smd, AT_RCONF, init.rconf.data()) == sat_LpmReply_Response_ERROR)
    {
        event.response = sat_LpmReply_Response_ERROR;
        publish_zbus_event(event);
        return;
    }

    k_msgq_purge(&sat_resp_msgq);
    if (argos_set_command(smd, AT_KMAC, "1") != 0)
    {
        event.response = sat_LpmReply_Response_ERROR;
        publish_zbus_event(event);
        return;
    }

    auto resp = get_smd_response();
    if (!resp.has_value())
    {
        event.response = sat_LpmReply_Response_ERROR;
        publish_zbus_event(event);
        return;
    }

    publish_zbus_event(event);
}

static bool transmit(const device *smd, const char *data)
{
    LOG_INF("Purging Response");
    k_msgq_purge(&sat_resp_msgq);

    LOG_INF("Sending Msg");
    if (argos_send_raw(smd, data) != 0)
    {
        LOG_ERR("Failed to construct message");
        return false;
    }

    if (!get_smd_response().has_value())
    {
        LOG_ERR("Failed get response");
        return false;
    }

    return true;
}

static void handle_msg_event(const device *smd, SatComMsgEvent &msg, Scheduler &scheduler)
{
    LOG_INF("New Message: %s", msg.data.data());

    LpmResponseEvent event{};
    event.msg_id = msg.msg_id;

    if (system_state == State::offline)
    {
        LOG_ERR("System is offline, rejecting message");
        event.response = sat_LpmReply_Response_ERROR;
        publish_zbus_event(event);
        return;
    }

    if (!transmit(smd, msg.data.data()))
    {
        event.response = sat_LpmReply_Response_ERROR;
        publish_zbus_event(event);
        return;
    }

    if (!scheduler.enroll(msg.data))
    {
        LOG_ERR("Failed to add to TX Queue");
        event.response = sat_LpmReply_Response_ERROR;
        publish_zbus_event(event);
        return;
    }

    event.response = sat_LpmReply_Response_OK;
    publish_zbus_event(event);
}

static void handle_state_event(const device *smd, StateEvent &event, Scheduler &scheduler)
{
    system_state = event.state;

    if (event.state != State::system_error)
    {
        return;
    }

    LOG_INF("Entering System Error, sending Error SatCom payload");

    const bool has_hostname = hostname[0] != '\0';

    int64_t epoch = 0;
    auto    epoch_result = rtc::current_epoch();
    if (epoch_result)
    {
        epoch = *epoch_result;
    }
    else
    {
        LOG_ERR("Failed to read RTC epoch, error payload date will be 0");
    }

    // The satellite payload is limited to 24 bytes, encoded as 48 hex chars.
    constexpr size_t sat_error_msg_size = 24;
    char             raw[sat_error_msg_size + 1]{};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    (void)std::snprintf(raw,
                        sizeof(raw),
                        "ERR%s%lld",
                        has_hostname ? hostname.data() : "unknown",
                        static_cast<long long>(epoch)); // NOLINT
#pragma GCC diagnostic pop

    char    encoded[(sat_error_msg_size * 2) + 1]{};
    hex_encode(raw, sat_error_msg_size, encoded);

    SatComMsgData data{};
    data.fill('\0');
    (void)std::snprintf(data.data(), data.size(), "AT+TX=%s", encoded); // NOLINT

    LOG_INF("Sending Error Payload: %s", data.data());

    if (!transmit(smd, data.data()))
    {
        LOG_ERR("Failed to send system error message");
        return;
    }

    if (!scheduler.enroll(data))
    {
        LOG_ERR("Failed to enroll system error tx");
    }
}

static void handle_repeatedtx_signal(const device *smd, Scheduler &scheduler)
{
    auto dtx_result = scheduler.next();
    if (!dtx_result.has_value())
    {
        LOG_ERR("RepeatedTx signal received but scheduler queue is empty");
        return;
    }

    auto dtx = dtx_result.value();

    LOG_INF("Sending Msg");
    if (argos_send_raw(smd, dtx.data().data()) != 0)
    {
        LOG_ERR("Failed to construct message");
        return;
    }

    if (!scheduler.re_enroll(dtx))
    {
        LOG_ERR("Failed to rescular repeat tx");
        return;
    }
}

static Result<> satcom_init()
{
    LOG_INF("Initalsing SatCom Module");

    satcom_tid = k_thread_create(&satcom_thread,
                                 static_cast<z_thread_stack_element *>(satcom_stack_area),
                                 K_THREAD_STACK_SIZEOF(satcom_stack_area),
                                 satcom_process,
                                 nullptr,
                                 nullptr,
                                 nullptr,
                                 satcom_priority,
                                 0,
                                 K_NO_WAIT);

    return {};
}

void satcom_process(void * /*unused*/, void * /*unused*/, void * /*unused*/) // NOLINT
{
    const struct device *smd = DEVICE_DT_GET_ONE(arribada_argossmd);

    LOG_INF("Entering satcom Process");

    if (!device_is_ready(smd))
    {
        LOG_ERR("SMD is not ready");
        return;
    }

    argos_smd_set_callback(smd, argos_callback, nullptr);

    if (!rtc::init())
    {
        LOG_ERR("Failed to init RTC, error payloads will lack timestamps");
    }

    Scheduler scheduler(&event_msgq);

#ifdef UNIT_TESTING
    test_scheduler = &scheduler;
#endif

    while (true)
    {
        Events incoming_event{};
        k_msgq_get(&event_msgq, &incoming_event, K_FOREVER);

        const auto visitor = overload{
            [&](SysInitEvent &init) { handle_info_event(smd, init); },
            [&](SatComMsgEvent &msg) { handle_msg_event(smd, msg, scheduler); },
            [&](StateEvent &state) { handle_state_event(smd, state, scheduler); },
            [&](RepeatedTxSignal /*unused*/)
            { handle_repeatedtx_signal(smd, scheduler); },
        };

        std::visit(visitor, incoming_event);
    }
}

} // namespace satcom

template <typename TEvent> static void push_event_to_queue(const zbus_channel *channel)
{
    using namespace satcom;

    const auto  *event_ptr = get_zbus_event<TEvent>(channel);
    const Events event     = *event_ptr;
    const int    ret       = k_msgq_put(&event_msgq, &event, K_NO_WAIT);
    if (ret != 0)
    {
        LOG_ERR("Failed to add event to the queue");
    }
};

static void listener_callback(const struct zbus_channel *channel) // NOLINT
{
    if (channel == &init_event_chan)
    {
        if (!satcom::satcom_init())
        {
            LOG_ERR("Failed to satcom module");
        }
    }

    else if (channel == &SatComMsgEventChan)
    {
        push_event_to_queue<SatComMsgEvent>(channel);
    }

    else if (channel == &SysInitEventChan)
    {
        push_event_to_queue<SysInitEvent>(channel);
    }

    else if (channel == &StateEventChan)
    {
        push_event_to_queue<StateEvent>(channel);
    }

    else
    {
        __ASSERT(
            false, "Channel %s is being listened to but not processed", channel->name);
    }
}

ZBUS_LISTENER_DEFINE(satcom_listener, listener_callback);                      // NOLINT
ZBUS_CHAN_ADD_OBS(init_event_chan, satcom_listener, zbus_default_priority);    // NOLINT
ZBUS_CHAN_ADD_OBS(SatComMsgEventChan, satcom_listener, zbus_default_priority); // NOLINT
ZBUS_CHAN_ADD_OBS(SysInitEventChan, satcom_listener, zbus_default_priority);   // NOLINT
ZBUS_CHAN_ADD_OBS(StateEventChan, satcom_listener, zbus_default_priority);     // NOLINT
