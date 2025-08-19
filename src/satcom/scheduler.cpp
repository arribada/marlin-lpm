#define MODULE satcom

#include <algorithm>
#include <chrono>
#include <zephyr/logging/log.h>

#include "satcom.hpp"

LOG_MODULE_DECLARE(MODULE); // NOLINT

namespace satcom
{

std::array<char, 64> format_uptime(int64_t uptime_ms) // NOLINT
{
    std::array<char, 64> buffer{}; // NOLINT
    buffer.fill('\0');

    using namespace std::chrono;
    auto mills = milliseconds(uptime_ms);
    auto hms   = hh_mm_ss(mills);

    (void)std::sprintf(buffer.data(),
                       "[%02lld:%02lld:%02lld.%03lld]",
                       static_cast<long long>(hms.hours().count()),
                       static_cast<long long>(hms.minutes().count()),
                       static_cast<long long>(hms.seconds().count()),
                       static_cast<long long>(mills.count() % 1000)); // NOLINT

    return buffer;
}

void Scheduler::timer_expiry(struct k_timer *timer)
{
    auto *self = static_cast<Scheduler *>(k_timer_user_data_get(timer));
    if (self == nullptr)
    {
        LOG_ERR("Scheduler timer fired before self ptr was set");
        return;
    }

    Events event = RepeatedTxSignal{};
    if (k_msgq_put(self->_msgq, &event, K_NO_WAIT) != 0)
    {
        LOG_ERR("Failed to add RepeatedTx to Satcom Msgq");
        return;
    };
}

Scheduler::Scheduler(k_msgq *msgq) : _msgq(msgq)
{
    k_timer_init(&_timer, Scheduler::timer_expiry, nullptr);
    k_timer_user_data_set(&_timer, static_cast<void *>(this));
}

Scheduler::~Scheduler()
{
    k_timer_stop(&_timer);
}

Result<> Scheduler::enroll(SatComMsgData data)
{
    int64_t now = k_uptime_get();

    RepeatedTx dtx;
    dtx._data          = data;
    dtx._transmit_time = now + _tx_delay_ms;

    if (!_on_going_tx.full())
    {
        _on_going_tx.push_back(dtx);
        auto uptime_str = format_uptime(dtx._transmit_time);
        LOG_INF("Enrolling RepeatedTx: %s at: %s", dtx.data().data(), uptime_str.data());
        schedule();
    }
    else
    {
        LOG_ERR("Scheduler queue full");
        return std::unexpected(Error::error);
    }

    return {};
}

void Scheduler::schedule()
{
    if (_on_going_tx.empty())
    {
        return;
    }

    if (k_timer_remaining_get(&_timer) != 0)
    {
        return;
    }

    int64_t now      = k_uptime_get();
    auto    min_item = _on_going_tx.front();
    int64_t duration = std::max(min_item._transmit_time - now, 1LL);
    k_timer_start(&_timer, K_MSEC(duration), K_NO_WAIT);

    LOG_INF("Scheduling Next RepeatedTx: %s in: %lldms", min_item.data().data(), duration);
}

Result<RepeatedTx> Scheduler::next()
{
    if (_on_going_tx.empty())
    {
        LOG_ERR("next() called with an empty scheduler queue");
        return std::unexpected(Error::error);
    }

    RepeatedTx item = _on_going_tx.front();
    _on_going_tx.erase(_on_going_tx.begin());

    LOG_INF("Popping RepeatedTx: %s", item.data().data());

    schedule();

    return item;
}

Result<> Scheduler::re_enroll(RepeatedTx dtx)
{
    dtx._transmissions += 1;
    if (dtx._transmissions >= _max_transmissions)
    {
        LOG_INF(
            "Message: %s has completed %d transmissions", dtx.data().data(), dtx._transmissions);
        return {};
    }

    dtx._transmit_time = k_uptime_get() + _tx_delay_ms;

    auto uptime_str = format_uptime(dtx._transmit_time);
    LOG_INF("Re-Enrolling RepeatedTx: %s at: %s", dtx.data().data(), uptime_str.data());
    _on_going_tx.push_back(dtx);
    schedule();

    return {};
}

void Scheduler::clear()
{
    k_timer_stop(&_timer);
    _on_going_tx.clear();
}

} // namespace satcom
