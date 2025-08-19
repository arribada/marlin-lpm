#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "reset_event.hpp"
#include "state_machine.hpp"
#include "sys_init_event.hpp"
#include "util.hpp"

namespace sml = boost::sml; // NOLINT
using namespace sml;

K_MSGQ_DEFINE(lpm_response_msgq, sizeof(LpmResponseEvent), 4, 4);

static void push_lpm_response(const zbus_channel *channel)
{
    const auto      *event = get_zbus_event<LpmResponseEvent>(channel);
    LpmResponseEvent resp  = *event;
    k_msgq_put(&lpm_response_msgq, &resp, K_NO_WAIT);
}

static void lpm_listener_callback(const struct zbus_channel *channel) // NOLINT
{
    if (channel == &LpmResponseEventChan)
    {
        push_lpm_response(channel);
    }
    else
    {
        __ASSERT(
            false, "Channel %s is being listened to but not processed", channel->name);
    }
}

ZBUS_LISTENER_DEFINE(supervisor_lpm_listener, lpm_listener_callback);
ZBUS_CHAN_ADD_OBS(LpmResponseEventChan, supervisor_lpm_listener, zbus_default_priority);

static LpmResponseEvent get_lpm_response()
{
    LpmResponseEvent resp{};
    int              ret = k_msgq_get(&lpm_response_msgq, &resp, K_SECONDS(1));
    zassert_equal(ret, 0, "Expected an LpmResponseEvent on the queue");
    return resp;
}

static sml::sm<SM, sml::testing> make_state_machine(SM &local_sm)
{
    sml::sm<SM, sml::testing> state_machine{local_sm};
    state_machine.process_event(SysInitEvent{});
    return state_machine;
}

ZTEST(supervisor, test_init)
{
    SM   local_sm{};
    auto state_machine = make_state_machine(local_sm);
    zassert_true(state_machine.is("Monitoring"_s));
}

ZTEST(supervisor, test_core_timeout)
{
    SM   local_sm{};
    auto state_machine = make_state_machine(local_sm);
    zassert_true(state_machine.is("Monitoring"_s));

    state_machine.process_event(CoreTimeoutEvent{});
    zassert_true(state_machine.is("SystemError"_s));
}

ZTEST(supervisor, test_core_status_does_not_enter_system_error)
{
    for (Status status : {Status::online, Status::error})
    {
        SM   local_sm{};
        auto state_machine = make_state_machine(local_sm);
        zassert_true(state_machine.is("Monitoring"_s));

        CoreStatusEvent event{};
        event.msg_id = 0, event.camera = status;
        state_machine.process_event(event);
        zassert_true(state_machine.is("Monitoring"_s));
    }
}

ZTEST(supervisor, test_core_status_exits_system_error)
{
    for (Status status : {Status::online, Status::error})
    {
        SM   local_sm{};
        auto state_machine = make_state_machine(local_sm);
        state_machine.set_current_states("SystemError"_s);

        CoreStatusEvent event{};
        event.msg_id = 0, event.camera = status;
        state_machine.process_event(event);
        zassert_true(state_machine.is("Monitoring"_s));
    }
}

ZTEST(supervisor, test_system_error_recovers_via_core_status)
{
    for (Status status : {Status::online, Status::error})
    {
        SM   local_sm{};
        auto state_machine = make_state_machine(local_sm);
        state_machine.set_current_states("SystemError"_s);

        CoreStatusEvent event{};
        event.msg_id = 0, event.camera = status;
        state_machine.process_event(event);
        zassert_true(state_machine.is("Monitoring"_s));
    }
}

ZTEST(supervisor, test_system_error_recovers_via_harbour)
{
    SM   local_sm{};
    auto state_machine = make_state_machine(local_sm);
    state_machine.set_current_states("SystemError"_s);

    state_machine.process_event(
        HarbourModeEvent{.msg_id = 41, .cmd = sat_HarbourMode_Command_EXIT});
    zassert_true(state_machine.is("Monitoring"_s));
}

ZTEST(supervisor, test_harbour)
{
    k_msgq_purge(&lpm_response_msgq);

    SM   local_sm{};
    auto state_machine = make_state_machine(local_sm);
    zassert_true(state_machine.is("Monitoring"_s));

    state_machine.process_event(
        HarbourModeEvent{.msg_id = 41, .cmd = sat_HarbourMode_Command_ENTER});
    zassert_true(state_machine.is("Harbour"_s));
    LpmResponseEvent enter_reply = get_lpm_response();
    zassert_equal(enter_reply.msg_id, 41);
    zassert_equal(enter_reply.response, sat_LpmReply_Response_OK);

    state_machine.process_event(
        HarbourModeEvent{.msg_id = 42, .cmd = sat_HarbourMode_Command_EXIT});
    zassert_true(state_machine.is("Monitoring"_s));
    LpmResponseEvent exit_reply = get_lpm_response();
    zassert_equal(exit_reply.msg_id, 42);
    zassert_equal(exit_reply.response, sat_LpmReply_Response_OK);

    state_machine.process_event(
        HarbourModeEvent{.msg_id = 43, .cmd = sat_HarbourMode_Command_EXIT});
    zassert_true(state_machine.is("Monitoring"_s));
}

ZTEST(supervisor, test_reset_event)
{
    k_msgq_purge(&lpm_response_msgq);

    SM   local_sm{};
    auto state_machine = make_state_machine(local_sm);
    zassert_true(state_machine.is("Monitoring"_s));

    state_machine.process_event(ResetEvent{.msg_id = 99});
    zassert_true(state_machine.is("SystemError"_s));

    LpmResponseEvent reply = get_lpm_response();
    zassert_equal(reply.msg_id, 99);
    zassert_equal(reply.response, sat_LpmReply_Response_OK);
}

ZTEST(supervisor, test_offline_core_timeout)
{
    SM   local_sm{};
    auto state_machine = make_state_machine(local_sm);
    state_machine.set_current_states("Offline"_s);

    state_machine.process_event(CoreTimeoutEvent{});
    zassert_true(state_machine.is("SystemError"_s));
}

ZTEST(supervisor, test_sys_init_ignored_when_monitoring)
{
    SM   local_sm{};
    auto state_machine = make_state_machine(local_sm);

    state_machine.process_event(SysInitEvent{});
    zassert_true(state_machine.is("Monitoring"_s));
}

ZTEST(supervisor, test_harbour_ignores_non_exit)
{
    k_msgq_purge(&lpm_response_msgq);

    SM   local_sm{};
    auto state_machine = make_state_machine(local_sm);

    state_machine.process_event(
        HarbourModeEvent{.msg_id = 1, .cmd = sat_HarbourMode_Command_ENTER});
    zassert_true(state_machine.is("Harbour"_s));
    (void)get_lpm_response();

    state_machine.process_event(
        HarbourModeEvent{.msg_id = 2, .cmd = sat_HarbourMode_Command_ENTER});
    zassert_true(state_machine.is("Harbour"_s));
}

ZTEST(supervisor, test_harbour_ignores_core_timeout)
{
    k_msgq_purge(&lpm_response_msgq);

    SM   local_sm{};
    auto state_machine = make_state_machine(local_sm);

    state_machine.process_event(
        HarbourModeEvent{.msg_id = 1, .cmd = sat_HarbourMode_Command_ENTER});
    zassert_true(state_machine.is("Harbour"_s));
    (void)get_lpm_response();

    state_machine.process_event(CoreTimeoutEvent{});
    zassert_true(state_machine.is("Harbour"_s));
}

ZTEST_SUITE(supervisor, NULL, NULL, NULL, NULL, NULL);
