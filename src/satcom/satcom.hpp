#pragma once

#include <cstdint>
#include <expected>
#include <variant>

#include <etl/vector.h>
#include <zephyr/kernel.h>

#include "satcom_msg_event.hpp"
#include "state_event.hpp"
#include "sys_init_event.hpp"

namespace satcom
{

struct RepeatedTxSignal
{
};

using Events = std::variant<SatComMsgEvent, SysInitEvent, StateEvent, RepeatedTxSignal>;

enum class Error : uint8_t
{
    error
};

template <typename T = void> using Result = std::expected<T, Error>;

class RepeatedTx
{
  public:
    [[nodiscard]] SatComMsgData data() const
    {
        return _data;
    }

  private:
    friend class Scheduler;
    SatComMsgData _data{};
    int64_t       _transmit_time{};
    int           _transmissions = 1;
};

class Scheduler
{
  public:
    explicit Scheduler(k_msgq *msgq);
    Result<>           enroll(SatComMsgData data);
    Result<RepeatedTx> next();
    Result<>           re_enroll(RepeatedTx dtx);
    void               clear();
    ~Scheduler();

    Scheduler(const Scheduler &)            = delete;
    Scheduler(Scheduler &&)                 = delete;
    Scheduler &operator=(const Scheduler &) = delete;
    Scheduler &operator=(Scheduler &&)      = delete;

  private:
    static constexpr size_t  _max_ongoing_tx    = 10;
    static constexpr int     _max_transmissions = 30;
    static constexpr int64_t _tx_delay_ms       = CONFIG_SATCOM_SCHEDULER_TX_DELAY_MS;

    static void timer_expiry(struct k_timer *timer);
    void        schedule();

    k_msgq                                  *_msgq = nullptr;
    k_timer                                  _timer{};
    etl::vector<RepeatedTx, _max_ongoing_tx> _on_going_tx;
};

} // namespace satcom
