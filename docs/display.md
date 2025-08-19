# Display Behaviour

The display renders system status on an ILI9341 TFT via LVGL. Behaviour is
driven entirely by zbus events — no polling or internal state logic.

## Main Status Text

| Condition | Text | Colour |
|-----------|------|--------|
| `StateEvent{offline}` (boot) | OFFLINE | Grey |
| `StateEvent{monitoring}` (after SysInit) | MONITORING | Green |
| `StateEvent{system_error}` (timeout / bad status) | ERROR | Red |
| `StateEvent{harbour}` (harbour mode) | HARBOUR | Blue |

## Component Dots

8 dots (CAM, CTRL, CLOUD, CELL, WIFI, GPS, TS, DASH) show the latest
`CoreStatusEvent` value. When a `CoreTimeoutEvent` fires (30 s UART idle),
`_is_stale` is set and all dots turn grey (`Status::offline`) until the next
`CoreStatusEvent` resets them.

## Bottom Info

The bottom bar is always visible and shows every field, rendered as `N/A`
until its data arrives:

- `TS`: Tailscale IP from the latest `CoreStatusEvent`.
- `STORAGE`: storage usage (%) from the latest `CoreStatusEvent`.
- `V` / `I` / `P`: battery voltage, current and power (watts, V×I) from the
  latest `BatteryInfoEvent`. Only shown as values when the event is `valid`.