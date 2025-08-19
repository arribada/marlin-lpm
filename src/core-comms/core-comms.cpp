#define MODULE core_comms

#include <array>
#include <cstdint>
#include <cstring>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zbus/zbus.h>

#include "core-comms.hpp"
#include "message-handler.hpp"

#include "core_status_event.hpp"
#include "core_timeout_event.hpp"
#include "init_event.hpp"
#include "lpm_response_event.hpp"
#include "state_event.hpp"
#include "util.hpp"

LOG_MODULE_REGISTER(MODULE, CONFIG_CORE_COMMS_MODULE_LOG_LEVEL); // NOLINT

#define UART_NODE DT_ALIAS(core_uart)

namespace core_comms
{

atomic_t state_atomic = ATOMIC_INIT(static_cast<atomic_t>(State::monitoring));

K_MSGQ_DEFINE(event_msgq, sizeof(LpmResponseEvent), 10, 4);

static constexpr int core_comms_stack_size = CONFIG_CORE_COMMS_MODULE_STACK_SIZE;
static constexpr int core_comms_priority   = CONFIG_CORE_COMMS_MODULE_PRIORITY;

K_THREAD_STACK_DEFINE(core_comms_stack_area, core_comms_stack_size);

static struct k_thread core_comms_thread;
static k_tid_t         core_comms_tid;

static const device *uart = DEVICE_DT_GET(UART_NODE);

static constexpr int MAX_MESSAGE_SIZE     = 256;
static constexpr int UART_IDLE_TIMEOUT_MS = 5;

static etl::vector<uint8_t, sat_CoreMessage_size> rx_buffer{};

static std::array<uint8_t, MAX_MESSAGE_SIZE> tx_buffer;
static struct ring_buf                       tx_ring_buffer;

K_MSGQ_DEFINE(uart_msgq, sizeof(rx_buffer), 10, 4);

static struct k_timer rx_timeout_timer;

void core_comms_process(void * /*unused*/, void * /*unused*/, void * /*unused*/);

void rx_timeout_handler(struct k_timer * /*timer_id*/)
{
    if (!rx_buffer.empty())
    {
        LOG_INF("Complete message received: %zu bytes", rx_buffer.size());
        k_msgq_put(&uart_msgq, (void *)&rx_buffer, K_NO_WAIT);
        rx_buffer.clear();
    }
}

void uart_isr(const struct device *dev, void * /*user_data*/)
{
    if (uart_irq_update(dev) == 0)
    {
        return;
    }

    if (uart_irq_rx_ready(dev) == 1)
    {
        k_timer_start(&rx_timeout_timer, K_MSEC(UART_IDLE_TIMEOUT_MS), K_NO_WAIT);

        if (!rx_buffer.full())
        {
            int bytes_read =
                uart_fifo_read(dev, rx_buffer.end(), (int)rx_buffer.available());

            size_t new_size = rx_buffer.size() + bytes_read;
            rx_buffer.uninitialized_resize(new_size);
        }
        else
        {
            LOG_ERR("Message too large, discarding (%zu bytes)", rx_buffer.size());
            rx_buffer.clear();
            k_timer_stop(&rx_timeout_timer);
        }
    }

    while ((uart_irq_tx_ready(dev) != 0) && !ring_buf_is_empty(&tx_ring_buffer))
    {
        uint8_t byte{};
        ring_buf_get(&tx_ring_buffer, &byte, 1);
        uart_fifo_fill(dev, &byte, 1);
    }

    if (ring_buf_is_empty(&tx_ring_buffer))
    {
        uart_irq_tx_disable(dev);
    }
}

void send_response(const std::span<const uint8_t> response)
{
    uint32_t wrote = ring_buf_put(&tx_ring_buffer, response.data(), response.size());
    if (wrote != response.size())
    {
        LOG_ERR("Failed to add all bytes to ring_buf");
        ring_buf_reset(&tx_ring_buffer);
        return;
    }
    uart_irq_tx_enable(uart);
}

static Result<> core_comms_init()
{
    LOG_INF("Initialising Core Comms Module");
    LOG_DBG("Initialising Core Comms Module");

    core_comms_tid =
        k_thread_create(&core_comms_thread,
                        static_cast<z_thread_stack_element *>(core_comms_stack_area),
                        K_THREAD_STACK_SIZEOF(core_comms_stack_area),
                        core_comms_process,
                        nullptr,
                        nullptr,
                        nullptr,
                        core_comms_priority,
                        0,
                        K_NO_WAIT);

    return {};
}

void core_comms_process(void * /*unused*/, void * /*unused*/, void * /*unused*/) // NOLINT
{
    LOG_INF("Entering Core Comms Process");

    if (!device_is_ready(uart))
    {
        LOG_ERR("UART is not ready");
        return;
    }

    k_timer_init(&rx_timeout_timer, rx_timeout_handler, nullptr);

    ring_buf_init(&tx_ring_buffer, tx_buffer.size(), tx_buffer.data());

    int ret_cb = uart_irq_callback_user_data_set(uart, uart_isr, nullptr);
    if (ret_cb < 0)
    {
        LOG_ERR("Failed to set UART callback: %d", ret_cb);
        return;
    }

    uart_irq_rx_enable(uart);
    int timeout_count = 0;

    while (true)
    {
        LOG_INF("Waiting for packet from Core");
        etl::vector<uint8_t, sat_CoreMessage_size> buf;
        int q_ret = k_msgq_get(&uart_msgq, &buf, K_SECONDS(30));

        if (q_ret == -EAGAIN)
        {
            auto state = static_cast<State>(atomic_get(&state_atomic));
            // Don't want to send timeout event if we are mid uart comm
            uint32_t timeout_ticks = k_timer_remaining_ticks(&rx_timeout_timer);

            if (state == State::monitoring && timeout_ticks == 0)
            {
                timeout_count++;
                LOG_WRN("UART read timed out, count at: %d", timeout_count);
                if (timeout_count == CONFIG_CORE_COMMS_CORE_RESET_THRESHOLD)
                {
                    LOG_WRN("Timeout Limited Reached (%d), sending event",
                            CONFIG_CORE_COMMS_CORE_RESET_THRESHOLD);
                    publish_zbus_event(CoreTimeoutEvent{});
                }
            }

            continue;
        }

        LOG_INF("Got new message");

        buf.repair(); // See ETL docs

        Result<IncomingEvent> data = parse_request(buf);

        if (!data.has_value())
        {

            LOG_ERR("Failed to parse incoming uart buffer, responding with error and "
                    "msg_id=0");
            auto response = generate_response(0, sat_LpmReply_Response_ERROR);
            if (response.has_value())
            {
                send_response(response.value());
            }
            else
            {
                LOG_ERR("Error! Failed to generate response");
            }
            continue;
        }

        const auto visitor = overload{
            [](auto &event)
            {
                if (event.msg_id == 0)
                {
                    LOG_ERR("msg_id=0 is reserved for responding to incoming request "
                            "that can not "
                            "be parsed");
                    return generate_response(0, sat_LpmReply_Response_ERROR);
                }
                LOG_INF("Dispatching Event");
                publish_zbus_event(event);

                LpmResponseEvent response{};
                int              ret = k_msgq_get(&event_msgq, &response, K_MSEC(5000));
                if (ret != 0)
                {
                    LOG_ERR("Timed out waiting for a response to msg_id=%d",
                            event.msg_id);
                    return generate_response(event.msg_id, sat_LpmReply_Response_ERROR);
                }
                return generate_response(
                    response.msg_id, response.response, response.battery);
            },
        };

        auto response = std::visit(visitor, data.value());
        if (response.has_value())
        {
            send_response(response.value());
        }
        else
        {
            LOG_ERR("Error! Failed to generate response");
        }
    }
}

} // namespace core_comms

static void listener_callback(const struct zbus_channel *channel) // NOLINT
{
    if (channel == &init_event_chan)
    {
        if (!core_comms::core_comms_init())
        {
            LOG_ERR("Failed to initialise supervisor module");
        }
    }
    else if (channel == &LpmResponseEventChan)
    {
        const auto *event = get_zbus_event<LpmResponseEvent>(channel);
        const int   ret   = k_msgq_put(&core_comms::event_msgq, event, K_NO_WAIT);
        if (ret != 0)
        {
            LOG_ERR("Failed to add event to the queue");
        }
    }

    else if (channel == &StateEventChan)
    {
        const auto *event = get_zbus_event<StateEvent>(channel);
        atomic_set(&core_comms::state_atomic, static_cast<atomic_t>(event->state));
    }
    else
    {
        __ASSERT(
            false, "Channel %s is being listened to but not processed", channel->name);
    }
}

ZBUS_LISTENER_DEFINE(core_comms_listener, listener_callback);                   // NOLINT
ZBUS_CHAN_ADD_OBS(init_event_chan, core_comms_listener, zbus_default_priority); // NOLINT
ZBUS_CHAN_ADD_OBS(LpmResponseEventChan,
                  core_comms_listener,
                  zbus_default_priority);                                      // NOLINT
ZBUS_CHAN_ADD_OBS(StateEventChan, core_comms_listener, zbus_default_priority); // NOLINT
