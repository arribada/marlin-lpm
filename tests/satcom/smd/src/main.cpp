#include <string_view>

#include <zephyr/device.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "util.hpp"

#include <argos-smd/argos_smd.h>

#define UART_NODE DT_NODELABEL(uart0)

const static device *dev     = DEVICE_DT_GET(UART_NODE);
const struct device *dev_smd = DEVICE_DT_GET_ONE(arribada_argossmd);

extern void uart_emul_reply(struct k_timer * /*timer_id*/)
{
    std::string_view input = "+\n";
    uart_emul_put_rx_data(dev, (uint8_t *)input.data(), input.size());
}

K_TIMER_DEFINE(uart_emul_reply_timer, uart_emul_reply, NULL);

void *init()
{
    zassert_true(device_is_ready(dev));

    uart_emul_flush_rx_data(dev);
    uart_emul_flush_tx_data(dev);
    uart_emul_set_release_buffer_on_timeout(dev, true);

    return nullptr;
}

ZTEST(comms, test_basic)
{
    k_timer_start(&uart_emul_reply_timer, K_MSEC(10), K_NO_WAIT);

    char msg[9] = "FFFFFFFF";                     // NOLINT
    zassert_ok(argos_send_payload(dev_smd, msg)); // NOLINT

    char out[20]{}; // NOLINT
    uart_emul_get_tx_data(dev, (uint8_t *)out, sizeof(out));

    char expected[] = "AT+TX=FFFFFFFF";
    zassert_ok(memcmp(expected, out, sizeof(expected) - 1)); // NOLINT
}

ZTEST_SUITE(comms, NULL, init, NULL, NULL, NULL);
