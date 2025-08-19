#pragma once

#include <sys_init_event.hpp>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "sml.hpp"
#pragma GCC diagnostic pop

#include <zephyr/logging/log.h>

#include "core_status_event.hpp"
#include "core_timeout_event.hpp"
#include "harbour_mode_event.hpp"
#include "lpm_response_event.hpp"
#include "reset_event.hpp"
#include "supervisor/supervisor.hpp"

namespace sml = boost::sml; // NOLINT

struct SM
{

    bool enter_habour_mode(const HarbourModeEvent &event) // NOLINT
    {
        return event.cmd == sat_HarbourMode_Command_ENTER;
    };

    bool exit_habour_mode(const HarbourModeEvent &event) // NOLINT
    {
        return event.cmd == sat_HarbourMode_Command_EXIT;
    };

    void send_system_error() // NOLINT
    {
        supervisor::reset_core();
    }

    void respond_harbour_mode(const HarbourModeEvent &event) // NOLINT
    {
        publish_zbus_event(LpmResponseEvent{
            .msg_id   = event.msg_id,
            .response = sat_LpmReply_Response_OK,
            .battery  = std::nullopt,
        });
    };

    void respond_reset(const ResetEvent &event) // NOLINT
    {
        publish_zbus_event(LpmResponseEvent{
            .msg_id   = event.msg_id,
            .response = sat_LpmReply_Response_OK,
            .battery  = std::nullopt,
        });
    };

    auto operator()()
    {
        using namespace sml;

        // clang-format off
        return make_transition_table(
        *"Offline"_s + event<SysInitEvent> = "Monitoring"_s,
        "Offline"_s + event<CoreTimeoutEvent> = "SystemError"_s,
        "Monitoring"_s + event<CoreTimeoutEvent> = "SystemError"_s,
        "Monitoring"_s + event<HarbourModeEvent> [&SM::enter_habour_mode] / &SM::respond_harbour_mode = "Harbour"_s,
        "Monitoring"_s + event<ResetEvent> / &SM::respond_reset = "SystemError"_s,
        
        "Harbour"_s + event<HarbourModeEvent> [&SM::exit_habour_mode] / &SM::respond_harbour_mode = "Monitoring"_s,

        "SystemError"_s + sml::on_entry<_> / &SM::send_system_error,
        "SystemError"_s + event<CoreStatusEvent> = "Monitoring"_s,
        "SystemError"_s + event<HarbourModeEvent> = "Monitoring"_s

        );
        // clang-format on
    }
};
