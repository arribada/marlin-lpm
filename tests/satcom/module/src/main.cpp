#include <array>
#include <cstring>
#include <variant>

#include <etl/string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "core.pb.h"
#include <pb_decode.h>
#include <pb_encode.h>

#include "satcom/satcom.hpp"

#include "init_event.hpp"
#include "lpm_response_event.hpp"
#include "satcom_msg_event.hpp"
#include "state_event.hpp"
#include "sys_init_event.hpp"
#include "util.hpp"

#include <zephyr/drivers/rtc/rtc_fake.h>

DEFINE_FFF_GLOBALS;

#define UART_NODE DT_NODELABEL(uart0)

const static device *dev = DEVICE_DT_GET(UART_NODE);

static constexpr int msgq_alignment = 1;
static constexpr int msgq_size      = 10;

using Events      = std::variant<LpmResponseEvent>;
using SatResponse = etl::string<sat_comm_msg_max_size>;

K_MSGQ_DEFINE(event_msgq, sizeof(Events), msgq_size, msgq_alignment);

static std::string get_request()
{
    std::string buf{};
    buf.resize(256); // NOLINT
    auto bytes = uart_emul_get_tx_data(dev, (uint8_t *)buf.data(), buf.size());
    if (bytes <= 1)
    {
        return {};
    }

    buf.resize(bytes - 1);
    return buf;
}

static void push_reply(SatResponse resp)
{
    zassert_equal(uart_emul_put_rx_data(dev, (uint8_t *)resp.data(), resp.size()),
                  resp.size());
}

static struct rtc_time mock_rtc_time;

static int rtc_fake_get_time_mock(const struct device *rtc_d, struct rtc_time *time)
{
    ARG_UNUSED(rtc_d);

    *time = mock_rtc_time;
    return 0;
}

static int rtc_fake_set_time_mock(const struct device *rtc_d, const struct rtc_time *time)
{
    ARG_UNUSED(rtc_d);
    ARG_UNUSED(time);

    return 0;
}

static void set_rtc_mock_time(int year, int month, int day, int hour, int minute, int sec)
{
    mock_rtc_time.tm_year = year - 1900;
    mock_rtc_time.tm_mon  = month - 1;
    mock_rtc_time.tm_mday = day;
    mock_rtc_time.tm_hour = hour;
    mock_rtc_time.tm_min  = minute;
    mock_rtc_time.tm_sec  = sec;

    rtc_fake_get_time_fake.custom_fake = rtc_fake_get_time_mock;
    rtc_fake_set_time_fake.custom_fake = rtc_fake_set_time_mock;
}

static void wait_for_setup_responses()
{
    zassert_equal(get_request(), "AT+ID=");
    push_reply("+OK\n");
    k_msleep(1);

    zassert_equal(get_request(), "AT+ADDR=");
    push_reply("+OK\n");
    k_msleep(1);

    zassert_equal(get_request(), "AT+SECKEY=");
    push_reply("+OK\n");
    k_msleep(1);

    zassert_equal(get_request(), "AT+RCONF=");
    push_reply("+OK\n");
    k_msleep(1);

    zassert_equal(get_request(), "AT+KMAC=1");
    push_reply("+OK\n");
    k_msleep(1);

    k_msgq_purge(&event_msgq);
}

ZTEST(satcom, test_info_set)
{
    SysInitEvent init{
        .msg_id       = 1,
        .id           = {"ID"},
        .addr         = {"ADDR"},
        .sec_key      = {"SECKEY"},
        .rconf        = {"RCONF"},
        .hostname     = {"HOSTNAME"},
        .wifi_network = {"WIFI_NETWORK"},
    };

    publish_zbus_event(init);

    zassert_equal(get_request(), "AT+ID=ID");
    push_reply("+OK\n");
    k_msleep(1);

    zassert_equal(get_request(), "AT+ADDR=ADDR");
    push_reply("+OK\n");
    k_msleep(1);

    zassert_equal(get_request(), "AT+SECKEY=SECKEY");
    push_reply("+OK\n");
    k_msleep(1);
    zassert_equal(get_request(), "AT+RCONF=RCONF");
    push_reply("+OK\n");
    k_msleep(1);

    zassert_equal(get_request(), "AT+KMAC=1");
    push_reply("+OK\n");
    k_msleep(1);

    Events event;
    zassert_ok(k_msgq_get(&event_msgq, &event, K_FOREVER));

    LpmResponseEvent response = std::get<LpmResponseEvent>(event);

    zassert_equal(response.response, sat_LpmReply_Response_OK);
    zassert_equal(response.msg_id, 1);
}

ZTEST(satcom, test_good_response)
{
    SatComMsgEvent msg{};
    msg.data   = {'a'};
    msg.msg_id = 1;
    publish_zbus_event(msg);

    zassert_equal(get_request(), "a");
    SatResponse resp = "+OK\n";
    push_reply(resp);

    Events event;
    zassert_ok(k_msgq_get(&event_msgq, &event, K_FOREVER));

    LpmResponseEvent response = std::get<LpmResponseEvent>(event);

    zassert_equal(response.response, sat_LpmReply_Response_OK);
    zassert_equal(response.msg_id, 1);
}

ZTEST(satcom, test_error_response)
{
    SatComMsgEvent msg{};
    msg.data   = {'a'};
    msg.msg_id = 1;
    publish_zbus_event(msg);

    zassert_equal(get_request(), "a");

    SatResponse resp = "+ERROR\n";
    push_reply(resp);

    Events event;
    zassert_ok(k_msgq_get(&event_msgq, &event, K_FOREVER));

    LpmResponseEvent response = std::get<LpmResponseEvent>(event);

    zassert_equal(response.response, sat_LpmReply_Response_ERROR);
    zassert_equal(response.msg_id, 1);
}

ZTEST(satcom, test_no_response)
{
    SatComMsgEvent msg{};
    msg.data   = {'a'};
    msg.msg_id = 1;
    publish_zbus_event(msg);

    zassert_equal(get_request(), "a");

    Events event;
    zassert_ok(k_msgq_get(&event_msgq, &event, K_FOREVER));

    LpmResponseEvent response = std::get<LpmResponseEvent>(event);

    zassert_equal(response.response, sat_LpmReply_Response_ERROR);
    zassert_equal(response.msg_id, 1);
}

namespace satcom
{
extern struct k_msgq event_msgq;
extern Scheduler    *test_scheduler;
} // namespace satcom

static SysInitEvent make_init()
{
    SysInitEvent init{};
    init.msg_id       = 1;
    init.id           = {"ID"};
    init.addr         = {"ADDR"};
    init.sec_key      = {"SECKEY"};
    init.rconf        = {"RCONF"};
    init.hostname     = {"HOSTNAME"};
    init.wifi_network = {"WIFI"};
    return init;
}

static void expect_command(const char *command, const char *reply)
{
    zassert_equal(get_request(), command);
    push_reply(reply);
    k_msleep(1);
}

static LpmResponseEvent next_response()
{
    Events event{};
    zassert_ok(k_msgq_get(&event_msgq, &event, K_SECONDS(2)),
               "Expected an LpmResponseEvent");
    return std::get<LpmResponseEvent>(event);
}

static void expect_error_response(uint32_t msg_id)
{
    auto response = next_response();
    zassert_equal(response.msg_id, msg_id);
    zassert_equal(response.response, sat_LpmReply_Response_ERROR);
}

static void expect_ok_response(uint32_t msg_id)
{
    auto response = next_response();
    zassert_equal(response.msg_id, msg_id);
    zassert_equal(response.response, sat_LpmReply_Response_OK);
}

static void inject_repeated_tx()
{
    satcom::Events event = satcom::RepeatedTxSignal{};
    zassert_ok(k_msgq_put(&satcom::event_msgq, &event, K_NO_WAIT));
}

static std::string wait_for_tx(int64_t timeout_ms)
{
    int64_t     start = k_uptime_get();
    std::string request;
    while (k_uptime_get() - start < timeout_ms)
    {
        request = get_request();
        if (!request.empty() && request[0] != '\0')
        {
            return request;
        }
        k_msleep(5);
    }
    return request;
}

ZTEST(satcom, test_info_set_id_rejected)
{
    k_msgq_purge(&event_msgq);

    publish_zbus_event(make_init());
    expect_command("AT+ID=ID", "+ERROR\n");

    expect_error_response(1);
    zassert_true(get_request().empty(), "later commands should not be sent");
}

ZTEST(satcom, test_info_set_verification_failure)
{
    k_msgq_purge(&event_msgq);

    publish_zbus_event(make_init());
    expect_command("AT+ID=ID", "+BOGUS\n");

    expect_error_response(1);
}

ZTEST(satcom, test_info_set_addr_rejected)
{
    k_msgq_purge(&event_msgq);

    publish_zbus_event(make_init());
    expect_command("AT+ID=ID", "+OK\n");
    expect_command("AT+ADDR=ADDR", "+ERROR\n");

    expect_error_response(1);
}

ZTEST(satcom, test_info_set_seckey_rejected)
{
    k_msgq_purge(&event_msgq);

    publish_zbus_event(make_init());
    expect_command("AT+ID=ID", "+OK\n");
    expect_command("AT+ADDR=ADDR", "+OK\n");
    expect_command("AT+SECKEY=SECKEY", "+ERROR\n");

    expect_error_response(1);
}

ZTEST(satcom, test_info_set_rconf_rejected)
{
    k_msgq_purge(&event_msgq);

    publish_zbus_event(make_init());
    expect_command("AT+ID=ID", "+OK\n");
    expect_command("AT+ADDR=ADDR", "+OK\n");
    expect_command("AT+SECKEY=SECKEY", "+OK\n");
    expect_command("AT+RCONF=RCONF", "+ERROR\n");

    expect_error_response(1);
}

ZTEST(satcom, test_info_set_kmac_rejected)
{
    k_msgq_purge(&event_msgq);

    publish_zbus_event(make_init());
    expect_command("AT+ID=ID", "+OK\n");
    expect_command("AT+ADDR=ADDR", "+OK\n");
    expect_command("AT+SECKEY=SECKEY", "+OK\n");
    expect_command("AT+RCONF=RCONF", "+OK\n");
    expect_command("AT+KMAC=1", "+ERROR\n");

    expect_error_response(1);
}

ZTEST(satcom, test_info_set_with_time)
{
    k_msgq_purge(&event_msgq);
    set_rtc_mock_time(2026, 8, 20, 14, 30, 5);
    rtc_fake_set_time_fake.call_count = 0;

    SysInitEvent init{};
    init.msg_id   = 1;
    init.hostname = {"HOSTNAME"};
    init.time     = 1787236205; // NOLINT
    publish_zbus_event(init);
    k_msleep(1);

    wait_for_setup_responses();

    zassert_true(rtc_fake_set_time_fake.call_count >= 1,
                 "RTC should be set from the init payload");
}

ZTEST(satcom, test_offline_rejects_message)
{
    k_msgq_purge(&event_msgq);

    publish_zbus_event(StateEvent{.state = State::offline});
    k_msleep(5);

    SatComMsgEvent msg{};
    msg.data   = {'a'};
    msg.msg_id = 1;
    publish_zbus_event(msg);

    expect_error_response(1);
    zassert_true(get_request().empty(), "nothing should be transmitted offline");
}

// The scheduler is cleared in before(), so the injected signal retransmits the
// payload enrolled below rather than a stale one from a previous test.
ZTEST(satcom, test_repeated_tx)
{
    k_msgq_purge(&event_msgq);

    SatComMsgEvent msg{};
    msg.data   = {'a'};
    msg.msg_id = 1;
    publish_zbus_event(msg);

    expect_command("a", "+OK\n");
    expect_ok_response(1);

    inject_repeated_tx();

    // The scheduler re-sends the enrolled payload without waiting for a reply.
    auto request = wait_for_tx(1000);
    zassert_equal(request, "a", "expected the enrolled payload to be retransmitted");
}

static void setup_sys_init(const char *hostname)
{
    SysInitEvent init{};
    init.msg_id       = 0;
    init.hostname     = {};
    init.wifi_network = {};

    if (hostname != nullptr)
    {
        std::strncpy(init.hostname.data(), hostname, init.hostname.size());
    }

    publish_zbus_event(init);
    k_msleep(1);

    wait_for_setup_responses();
}

static void drive_into_system_error()
{
    publish_zbus_event(StateEvent{.state = State::system_error});
}

ZTEST(satcom, test_system_error)
{
    setup_sys_init("HOSTNAME");

    set_rtc_mock_time(2026, 8, 20, 14, 30, 5);

    drive_into_system_error();

    zassert_equal(get_request(),
                  "AT+TX=455252484F53544E414D4531373837323336323035000000");
    push_reply("+OK\n");
}

ZTEST(satcom, test_system_error_no_hostname)
{
    setup_sys_init(nullptr);

    set_rtc_mock_time(2026, 8, 20, 14, 30, 5);

    drive_into_system_error();

    zassert_equal(get_request(),
                  "AT+TX=455252756E6B6E6F776E3137383732333632303500000000");
    push_reply("+OK\n");
}

static int rtc_fake_get_time_fail(const struct device * /*rtc_d*/, struct rtc_time *time)
{
    ARG_UNUSED(time);
    return -1;
}

ZTEST(satcom, test_system_error_rtc_failure)
{
    setup_sys_init("HOSTNAME");

    rtc_fake_get_time_fake.custom_fake = rtc_fake_get_time_fail;

    drive_into_system_error();

    zassert_equal(get_request(),
                  "AT+TX=455252484F53544E414D4530000000000000000000000000");
    push_reply("+OK\n");
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

void before(void * /*unused*/)
{
    // Let any processing left over from the previous test settle before
    // flushing, otherwise late UART writes bleed into this test.
    k_msleep(50);
    uart_emul_flush_rx_data(dev);
    uart_emul_flush_tx_data(dev);
    k_msgq_purge(&event_msgq);
    if (satcom::test_scheduler != nullptr)
    {
        satcom::test_scheduler->clear();
    }
    publish_zbus_event(StateEvent{State::monitoring});
}

ZTEST_SUITE(satcom, NULL, init, before, NULL, NULL);

template <typename TEvent> void push_event_to_queue(const zbus_channel *channel)
{
    const auto *event_ptr = get_zbus_event<TEvent>(channel);
    Events      event     = *event_ptr;
    k_msgq_put(&event_msgq, &event, K_NO_WAIT);
};

static void listener_callback(const struct zbus_channel *channel) // NOLINT
{
    if (channel == &LpmResponseEventChan)
    {
        push_event_to_queue<LpmResponseEvent>(channel);
    }
    else
    {
        __ASSERT(
            false, "Channel %s is being listened to but not processed", channel->name);
    }
}

ZBUS_LISTENER_DEFINE(test_listener, listener_callback);
ZBUS_CHAN_ADD_OBS(LpmResponseEventChan, test_listener, zbus_default_priority);
