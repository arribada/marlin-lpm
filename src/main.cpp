#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/zbus/zbus.h>

#include <init_event.hpp>
#include <version.hpp>

LOG_MODULE_REGISTER(main);

// CONFIG_NUM_PREEMPT_PRIORITIES - 1 is the lowest priority allowed in the system.
// The Zephyr idle thread is at this priority. Therefore our idle watchdog thread
// is second lowest at CONFIG_NUM_PREEMPT_PRIORITIES - 2.
static constexpr int low_watchdog_priority   = CONFIG_NUM_PREEMPT_PRIORITIES - 2;
static constexpr int low_watchdog_stack_size = 4096;
K_THREAD_STACK_DEFINE(low_watchdog_stack_area, low_watchdog_stack_size);

static void watchdog_entry(void * /*unused*/, void * /*unused*/, void * /*unused*/);

static k_tid_t         watchdog_tid;
static struct k_thread low_watchdog_thread_data;

void enable_task_watchdog();

int main()
{
    printk(R"EOF(
    ▗▖  ▗▖ ▗▄▖ ▗▄▄▖ ▗▖   ▗▄▄▄▖▗▖  ▗▖        ▗▖   ▗▖  ▗▖▗▄▄▖ 
    ▐▛▚▞▜▌▐▌ ▐▌▐▌ ▐▌▐▌     █  ▐▛▚▖▐▌        ▐▌   ▐▛▚▞▜▌▐▌ ▐▌
    ▐▌  ▐▌▐▛▀▜▌▐▛▀▚▖▐▌     █  ▐▌ ▝▜▌        ▐▌   ▐▌  ▐▌▐▛▀▘ 
    ▐▌  ▐▌▐▌ ▐▌▐▌ ▐▌▐▙▄▄▖▗▄█▄▖▐▌  ▐▌        ▐▙▄▄▖▐▌  ▐▌▐▌   
    %.*s-%.*s-%.*s-%.*s
    )EOF",
           static_cast<int>(version::TAG.size()),
           version::TAG.data(),
           static_cast<int>(version::BRANCH.size()),
           version::BRANCH.data(),
           static_cast<int>(version::GIT_HASH.size()),
           version::GIT_HASH.data(),
           static_cast<int>(version::GIT_DIRTY_S.size()),
           version::GIT_DIRTY_S.data());

    enable_task_watchdog();

    LOG_INF("Initialising Modules");

    publish_zbus_event(InitEvent{});

    LOG_INF("Modules Initialised, Main Thread Sleeping...");

    k_sleep(K_FOREVER);

    return 0;
}

void k_sys_fatal_error_handler(unsigned int reason, const arch_esf *esf)
{
    ARG_UNUSED(esf);
    ARG_UNUSED(reason);

    LOG_PANIC();

    k_busy_wait(50000); // NOLINT
}

void enable_task_watchdog()
{
#if DT_NODE_EXISTS(DT_NODELABEL(wdt0))
    const struct device *const wdt = DEVICE_DT_GET(DT_NODELABEL(wdt0));

    if (!device_is_ready(wdt))
    {
        LOG_ERR("%s: device not ready.", wdt->name);
        return;
    }

    int watchdog_ret = task_wdt_init(wdt);
    if (watchdog_ret != 0)
    {
        LOG_ERR("task wdt init failure: %d", watchdog_ret);
    }

    watchdog_tid = k_thread_create(&low_watchdog_thread_data,
                                   low_watchdog_stack_area, // NOLINT
                                   K_THREAD_STACK_SIZEOF(low_watchdog_stack_area),
                                   watchdog_entry,
                                   nullptr,
                                   nullptr,
                                   nullptr,
                                   low_watchdog_priority,
                                   0,
                                   K_NO_WAIT);
#else
    LOG_ERR("wdt0 node not found in devicetree, watchdog disabled");
#endif
}

static void watchdog_call_back(int channel_id, void * /*user_data*/)
{
    ARG_UNUSED(channel_id);

    LOG_ERR("Watchdog Task has been triggered");
    LOG_ERR("Resetting System ...");

    k_oops();
}

static void watchdog_entry(void * /*unused*/, void * /*unused*/, void * /*unused*/)
{
    k_thread_name_set(watchdog_tid, "Watchdog");

    constexpr uint32_t reload_period_ms = CONFIG_TASK_WDT_MIN_TIMEOUT;

    int task_wdt_id = task_wdt_add(reload_period_ms, watchdog_call_back, nullptr);

    if (task_wdt_id < 0)
    {
        LOG_ERR("task wdt watchdog failure: %d", task_wdt_id);
    }

    LOG_INF("Watchdog Task WD Started");

    while (true)
    {
        int ret = task_wdt_feed(task_wdt_id);
        if (ret != 0)
        {
            LOG_ERR("Watchdog Feed Failed");
        }
        const int32_t wtd_watchdog_sleep_ms = CONFIG_TASK_WDT_MIN_TIMEOUT / 10;
        k_msleep(wtd_watchdog_sleep_ms);
    }
}
