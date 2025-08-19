#define MODULE display

#include <variant>

#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <lvgl.h>

#include "battery_info_event.hpp"
#include "core_status_event.hpp"
#include "core_timeout_event.hpp"
#include "display.hpp"
#include "init_event.hpp"
#include "satcom_msg_event.hpp"
#include "state_event.hpp"
#include "sys_init_event.hpp"
#include "util.hpp"

LOG_MODULE_REGISTER(MODULE, CONFIG_DISPLAY_MODULE_LOG_LEVEL); // NOLINT

namespace display
{

using Events = std::variant<CoreStatusEvent,
                            SysInitEvent,
                            StateEvent,
                            CoreTimeoutEvent,
                            BatteryInfoEvent,
                            SatComMsgEvent>;
K_MSGQ_DEFINE(event_msgq, sizeof(Events), 10, 4);

static constexpr int display_stack_size = CONFIG_DISPLAY_MODULE_STACK_SIZE;
static constexpr int display_priority   = CONFIG_DISPLAY_MODULE_PRIORITY;

K_THREAD_STACK_DEFINE(display_stack_area, display_stack_size);

static struct k_thread display_thread;
static k_tid_t         display_tid;

void display_process(void * /*unused*/, void * /*unused*/, void * /*unused*/);

static Result<> display_init()
{
    LOG_INF("Initalsing Display Module");

    display_tid =
        k_thread_create(&display_thread,
                        static_cast<z_thread_stack_element *>(display_stack_area),
                        K_THREAD_STACK_SIZEOF(display_stack_area),
                        display_process,
                        nullptr,
                        nullptr,
                        nullptr,
                        display_priority,
                        0,
                        K_NO_WAIT);

    return {};
}

void display_process(void * /*unused*/, void * /*unused*/, void * /*unused*/)
{
    LOG_INF("Entering Display Process");

    constexpr int     frames_per_second   = 60;
    const k_timeout_t time_between_frames = K_MSEC(1000 / frames_per_second);

    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

    Display display(display_dev);

    int64_t last_tick = k_uptime_get();

    while (true)
    {
        Events incoming_event{};
        int    ret = k_msgq_get(&event_msgq, &incoming_event, time_between_frames);
        if (ret == 0)
        {
            const auto visitor = overload{
                [&display](CoreStatusEvent &event) { display.on_core_status(event); },
                [&display](SysInitEvent &event) { display.on_sys_init(event); },
                [&display](StateEvent &event) { display.on_state_event(event); },
                [&display](CoreTimeoutEvent &event) { display.on_timeout_event(event); },
                [&display](BatteryInfoEvent &event) { display.on_battery_info(event); },
                [&display](SatComMsgEvent &event) { display.on_sat_com_msg(event); }};
            std::visit(visitor, incoming_event);

            LOG_INF("Display event received");
        };

        const int64_t now      = k_uptime_get();
        int           delta_ms = static_cast<int>(now - last_tick);
        last_tick              = now;

        display.update(delta_ms);
    }
}

} // namespace display

template <typename TEvent> static void push_event_to_queue(const zbus_channel *channel)
{
    using namespace display;

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
        if (!display::display_init())
        {
            LOG_ERR("Failed to initialise display module");
        }
    }

    else if (channel == &CoreStatusEventChan)
    {
        push_event_to_queue<CoreStatusEvent>(channel);
    }

    else if (channel == &SysInitEventChan)
    {
        push_event_to_queue<SysInitEvent>(channel);
    }

    else if (channel == &StateEventChan)
    {
        push_event_to_queue<StateEvent>(channel);
    }

    else if (channel == &CoreTimeoutEventChan)
    {
        push_event_to_queue<CoreTimeoutEvent>(channel);
    }

    else if (channel == &BatteryInfoEventChan)
    {
        push_event_to_queue<BatteryInfoEvent>(channel);
    }

    else if (channel == &SatComMsgEventChan)
    {
        push_event_to_queue<SatComMsgEvent>(channel);
    }

    else
    {
        __ASSERT(
            false, "Channel %s is being listened to but not processed", channel->name);
    }
}

ZBUS_LISTENER_DEFINE(display_listener, listener_callback);                       // NOLINT
ZBUS_CHAN_ADD_OBS(init_event_chan, display_listener, zbus_default_priority);     // NOLINT
ZBUS_CHAN_ADD_OBS(CoreStatusEventChan, display_listener, zbus_default_priority); // NOLINT
ZBUS_CHAN_ADD_OBS(SysInitEventChan, display_listener, zbus_default_priority);    // NOLINT
ZBUS_CHAN_ADD_OBS(StateEventChan, display_listener, zbus_default_priority);      // NOLINT
ZBUS_CHAN_ADD_OBS(CoreTimeoutEventChan,
                  display_listener,
                  zbus_default_priority); // NOLINT
ZBUS_CHAN_ADD_OBS(BatteryInfoEventChan,
                  display_listener,
                  zbus_default_priority); // NOLINT
ZBUS_CHAN_ADD_OBS(SatComMsgEventChan, display_listener,
                  zbus_default_priority); // NOLINT
