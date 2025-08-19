#include <zephyr/zbus/zbus.h>

#include "battery_info_event.hpp"
#include "core_status_event.hpp"
#include "core_timeout_event.hpp"
#include "harbour_mode_event.hpp"
#include "init_event.hpp"
#include "lpm_response_event.hpp"
#include "reset_event.hpp"
#include "satcom_msg_event.hpp"
#include "state_event.hpp"
#include "sys_init_event.hpp"

// NOLINTNEXTLINE
ZBUS_CHAN_DEFINE(init_event_chan,      /* Name */
                 InitEvent,            /* Message type */
                 nullptr,              /* Validator */
                 nullptr,              /* User data */
                 ZBUS_OBSERVERS_EMPTY, /* observers */
                 ZBUS_MSG_INIT()       /* Initial value */
);

// NOLINTNEXTLINE
ZBUS_CHAN_DEFINE(CoreStatusEventChan,  /* Name */
                 CoreStatusEvent,      /* Message type */
                 nullptr,              /* Validator */
                 nullptr,              /* User data */
                 ZBUS_OBSERVERS_EMPTY, /* observers */
                 ZBUS_MSG_INIT()       /* Initial value */
);

// NOLINTNEXTLINE
ZBUS_CHAN_DEFINE(CoreTimeoutEventChan, /* Name */
                 CoreTimeoutEvent,     /* Message type */
                 nullptr,              /* Validator */
                 nullptr,              /* User data */
                 ZBUS_OBSERVERS_EMPTY, /* observers */
                 ZBUS_MSG_INIT()       /* Initial value */
);

// NOLINTNEXTLINE
ZBUS_CHAN_DEFINE(StateEventChan,       /* Name */
                 StateEvent,           /* Message type */
                 nullptr,              /* Validator */
                 nullptr,              /* User data */
                 ZBUS_OBSERVERS_EMPTY, /* observers */
                 ZBUS_MSG_INIT()       /* Initial value */
);

// NOLINTNEXTLINE
ZBUS_CHAN_DEFINE(SatComMsgEventChan,   /* Name */
                 SatComMsgEvent,       /* Message type */
                 nullptr,              /* Validator */
                 nullptr,              /* User data */
                 ZBUS_OBSERVERS_EMPTY, /* observers */
                 ZBUS_MSG_INIT()       /* Initial value */
);

ZBUS_CHAN_DEFINE(BatteryInfoEventChan, /* Name */
                 BatteryInfoEvent,     /* Message type */
                 nullptr,              /* Validator */
                 nullptr,              /* User data */
                 ZBUS_OBSERVERS_EMPTY, /* observers */
                 ZBUS_MSG_INIT()       /* Initial value */
);

// NOLINTNEXTLINE
ZBUS_CHAN_DEFINE(LpmResponseEventChan, /* Name */
                 LpmResponseEvent,     /* Message type */
                 nullptr,              /* Validator */
                 nullptr,              /* User data */
                 ZBUS_OBSERVERS_EMPTY, /* observers */
                 ZBUS_MSG_INIT()       /* Initial value */
);

// NOLINTNEXTLINE
ZBUS_CHAN_DEFINE(HarbourModeEventChan, /* Name */
                 HarbourModeEvent,     /* Message type */
                 nullptr,              /* Validator */
                 nullptr,              /* User data */
                 ZBUS_OBSERVERS_EMPTY, /* observers */
                 ZBUS_MSG_INIT()       /* Initial value */
);

// NOLINTNEXTLINE
ZBUS_CHAN_DEFINE(SysInitEventChan,     /* Name */
                 SysInitEvent,         /* Message type */
                 nullptr,              /* Validator */
                 nullptr,              /* User data */
                 ZBUS_OBSERVERS_EMPTY, /* observers */
                 ZBUS_MSG_INIT()       /* Initial value */
);

// NOLINTNEXTLINE
ZBUS_CHAN_DEFINE(ResetEventChan, /* Name */
                 ResetEvent,     /* Message type */
                 nullptr,        /* Validator */
                 nullptr,        /* User data */
                 ZBUS_OBSERVERS_EMPTY, /* observers */
                 ZBUS_MSG_INIT()       /* Initial value */
);
