#define MODULE supervisor

#include <cstring>
#include <variant>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zbus/zbus.h>

#include "util.hpp"

#include "harbour_mode_event.hpp"
#include "init_event.hpp"
#include "reset_event.hpp"
#include "state_event.hpp"

#include "state_machine.hpp"
#include "supervisor.hpp"

LOG_MODULE_REGISTER(MODULE, CONFIG_SUPERVISOR_MODULE_LOG_LEVEL); // NOLINT

namespace supervisor
{

using Events = std::variant<CoreStatusEvent,
                            CoreTimeoutEvent,
                            HarbourModeEvent,
                            ResetEvent,
                            SysInitEvent>;
K_MSGQ_DEFINE(event_msgq, sizeof(Events), 10, 4);

static const struct gpio_dt_spec core_reset =
    GPIO_DT_SPEC_GET(DT_ALIAS(core_reset), gpios);

static constexpr int supervisor_stack_size = CONFIG_SUPERVISOR_MODULE_STACK_SIZE;
static constexpr int supervisor_priority   = CONFIG_SUPERVISOR_MODULE_PRIORITY;

K_THREAD_STACK_DEFINE(supervisor_stack_area, supervisor_stack_size);

static struct k_thread supervisor_thread;
static k_tid_t         supervisor_tid;

// The core reset sequence blocks for ~30s, so it is performed on its own
// low-priority thread rather than stalling the supervisor event loop.
static constexpr int reset_stack_size = CONFIG_SUPERVISOR_MODULE_STACK_SIZE;
static constexpr int reset_priority   = CONFIG_NUM_PREEMPT_PRIORITIES - 2;
static K_THREAD_STACK_DEFINE(reset_stack_area, reset_stack_size);
static struct k_thread reset_thread{};
static k_tid_t         reset_tid;

// The reset thread waits on these event bits. While the system remains in the
// SystemError state the timer re-posts PULSE every retry interval; leaving the
// state posts STOP, which makes the thread stop pulsing.
static constexpr int core_reset_pulse_event = BIT(0);
static constexpr int core_reset_stop_event  = BIT(1);

static struct k_event core_reset_event = {};
static struct k_timer core_reset_timer = {};

static void core_reset_timer_handler(struct k_timer * /*timer*/) // NOLINT
{
    k_event_post(&core_reset_event, core_reset_pulse_event);
}

void supervisor_process(void * /*unused*/, void * /*unused*/, void * /*unused*/);
void reset_core_thread_entry(void * /*unused*/, void * /*unused*/, void * /*unused*/);

struct Logger
{
    template <class SM, class TEvent> void log_process_event(const TEvent & /* unused */)
    {
    }

    template <class SM, class TGuard, class TEvent>
    void log_guard(const TGuard & /* unused */,
                   const TEvent & /* unused */,
                   bool /* unused */)
    {
    }

    template <class SM, class TAction, class TEvent>
    void log_action(const TAction & /* unused */, const TEvent & /* unused */)
    {
    }

    template <class SM, class TSrcState, class TDstState>
    void log_state_change(const TSrcState &src, const TDstState &dst)
    {
        LOG_INF("%s -> %s", src.c_str(), dst.c_str());
        State state{};
        if (std::strcmp(dst.c_str(), "Monitoring") == 0)
        {
            state = State::monitoring;
        }

        else if (std::strcmp(dst.c_str(), "SystemError") == 0)
        {
            state = State::system_error;
        }

        else if (std::strcmp(dst.c_str(), "Harbour") == 0)
        {
            state = State::harbour;
        }

        else if (std::strcmp(dst.c_str(), "Offline") == 0)
        {
            state = State::offline;
        }

        else
        {
            LOG_ERR("Unknown State %s", dst.c_str());
            return;
        }

        if (state != State::system_error)
        {
            k_timer_stop(&core_reset_timer);
            k_event_post(&core_reset_event, core_reset_stop_event);
        }

        publish_zbus_event(StateEvent{state});
    }
};

static Result<> supervisor_init()
{
    LOG_INF("Initalsing Supervisor Module");

    supervisor_tid =
        k_thread_create(&supervisor_thread,
                        static_cast<z_thread_stack_element *>(supervisor_stack_area),
                        K_THREAD_STACK_SIZEOF(supervisor_stack_area),
                        supervisor_process,
                        nullptr,
                        nullptr,
                        nullptr,
                        supervisor_priority,
                        0,
                        K_NO_WAIT);

    k_event_init(&core_reset_event);
    k_timer_init(&core_reset_timer, core_reset_timer_handler, nullptr);

    reset_tid = k_thread_create(&reset_thread,
                                static_cast<z_thread_stack_element *>(reset_stack_area),
                                K_THREAD_STACK_SIZEOF(reset_stack_area),
                                reset_core_thread_entry,
                                nullptr,
                                nullptr,
                                nullptr,
                                reset_priority,
                                0,
                                K_NO_WAIT);

    return {};
}

void supervisor_process(void * /*unused*/, void * /*unused*/, void * /*unused*/)
{
    LOG_INF("Entering Supervisor Process");

    if (!gpio_is_ready_dt(&core_reset))
    {
        LOG_ERR("Core reset GPIO is not ready");
    }

    if (gpio_pin_configure_dt(&core_reset, GPIO_OUTPUT_INACTIVE) != 0)
    {
        LOG_ERR("Failed to configure Core reset GPIO");
    }

    SM local_sm{};

    Logger                           logger;
    sml::sm<SM, sml::logger<Logger>> state_machine{local_sm, logger};

    // Initial state isn't published through logger so we do it manually
    publish_zbus_event(StateEvent{State::offline});

    while (true)
    {
        LOG_INF("Supervisor Now Waiting for Events");

        Events incoming_event{};
        k_msgq_get(&event_msgq, &incoming_event, K_FOREVER);

        auto process_event_from_variant = [&state_machine](auto &&event)
        { state_machine.process_event(event); };

        std::visit(process_event_from_variant, incoming_event);
    }
}

void reset_core()
{
    LOG_WRN("System Error, scheduling Core Pi reset");
    k_event_post(&core_reset_event, core_reset_pulse_event);
    k_timer_start(&core_reset_timer,
                  K_SECONDS(CONFIG_SUPERVISOR_CORE_RESET_RETRY_SECONDS),
                  K_SECONDS(CONFIG_SUPERVISOR_CORE_RESET_RETRY_SECONDS));
}

void reset_core_thread_entry(void * /*unused*/, void * /*unused*/, void * /*unused*/)
{
    LOG_INF("Core Reset thread started");

    while (true)
    {
        uint32_t events = k_event_wait(&core_reset_event,
                                       core_reset_pulse_event | core_reset_stop_event,
                                       true,
                                       K_FOREVER);

        if (events == 0 || (events & core_reset_stop_event))
        {
            LOG_INF("Core reset cancelled, returning to idle");
            continue;
        }

        if (!gpio_is_ready_dt(&core_reset))
        {
            LOG_ERR("Core reset GPIO is not ready");
            continue;
        }

        LOG_INF("Pulsing Core Reset Line");
        // if (gpio_pin_configure_dt(&core_reset, GPIO_OUTPUT_ACTIVE) != 0)
        // {
        //     LOG_ERR("Failed to drive Core reset line active");
        //     continue;
        // }

        k_sleep(K_MSEC(CONFIG_SUPERVISOR_CORE_RESET_PULSE_MS));

        // if (gpio_pin_configure_dt(&core_reset, GPIO_OUTPUT_INACTIVE) != 0)
        // {
        //     LOG_ERR("Failed to release Core reset line");
        //     continue;
        // }
    }
}

} // namespace supervisor

template <typename TEvent> static void push_event_to_queue(const zbus_channel *channel)
{
    using namespace supervisor;

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
        if (!supervisor::supervisor_init())
        {
            LOG_ERR("Failed to supervisor module");
        }
    }

    else if (channel == &CoreStatusEventChan)
    {
        push_event_to_queue<CoreStatusEvent>(channel);
    }

    else if (channel == &CoreTimeoutEventChan)
    {
        push_event_to_queue<CoreTimeoutEvent>(channel);
    }

    else if (channel == &HarbourModeEventChan)
    {
        push_event_to_queue<HarbourModeEvent>(channel);
    }

    else if (channel == &SysInitEventChan)
    {
        push_event_to_queue<SysInitEvent>(channel);
    }

    else if (channel == &ResetEventChan)
    {
        push_event_to_queue<ResetEvent>(channel);
    }

    else
    {
        __ASSERT(
            false, "Channel %s is being listened to but not processed", channel->name);
    }
}

ZBUS_LISTENER_DEFINE(supervisor_listener, listener_callback);                   // NOLINT
ZBUS_CHAN_ADD_OBS(init_event_chan, supervisor_listener, zbus_default_priority); // NOLINT
ZBUS_CHAN_ADD_OBS(CoreStatusEventChan,
                  supervisor_listener,
                  zbus_default_priority); // NOLINT
ZBUS_CHAN_ADD_OBS(CoreTimeoutEventChan,
                  supervisor_listener,
                  zbus_default_priority); // NOLINT
ZBUS_CHAN_ADD_OBS(HarbourModeEventChan,
                  supervisor_listener,
                  zbus_default_priority); // NOLINT
ZBUS_CHAN_ADD_OBS(SysInitEventChan, supervisor_listener,
                  zbus_default_priority); // NOLINT
ZBUS_CHAN_ADD_OBS(ResetEventChan, supervisor_listener,
                  zbus_default_priority); // NOLINT
