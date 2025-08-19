#pragma once

#include <array>
#include <cstdint>
#include <expected>

#include <lvgl.h>
#include <zephyr/device.h>

#include <battery_info_event.hpp>
#include <core_status_event.hpp>
#include <core_timeout_event.hpp>
#include <satcom_msg_event.hpp>
#include <state_event.hpp>
#include <sys_init_event.hpp>

namespace display
{

enum class Error : uint8_t
{
    error
};

template <typename T = void> using Result = std::expected<T, Error>;

class Display
{

  public:
    explicit Display(const device *display_dev);

    void update(int delta_ms);

    void on_core_status(CoreStatusEvent &event);
    void on_sys_init(SysInitEvent &event);
    void on_state_event(StateEvent &event);
    void on_timeout_event(CoreTimeoutEvent &event);
    void on_battery_info(BatteryInfoEvent &event);
    void on_sat_com_msg(SatComMsgEvent &event);
    Display(const Display &)            = delete;
    Display(Display &&)                 = delete;
    Display &operator=(const Display &) = delete;
    Display &operator=(Display &&)      = delete;
    ~Display()                          = default;

  private:
    enum class StatusField : uint8_t
    {
        camera,
        controller,
        cloud_connection,
        cellular,
        wifi,
        gps,
        tailscale,
        local_dash,
    };

    struct Component
    {
        const char *label;
        StatusField field;
    };

    static constexpr std::array<Component, 8> components{
        Component{"CAM", StatusField::camera},
        Component{"CTRL", StatusField::controller},
        Component{"CLOUD", StatusField::cloud_connection},
        Component{"CELL", StatusField::cellular},
        Component{"WIFI", StatusField::wifi},
        Component{"GPS", StatusField::gps},
        Component{"TS", StatusField::tailscale},
        Component{"DASH", StatusField::local_dash},
    };

    void create_status_screen();
    void refresh_labels();
    void update_title();
    void update_status_main();
    void update_dots();
    void update_bottom_info();
    void update_battery();
    void update_flash_dot(int delta_ms);

    [[nodiscard]] Status get_status(StatusField field) const;

    static lv_color_t status_to_colour(Status status);

    const device    *_dev{};
    CoreStatusEvent  _core_status{};
    BatteryInfoEvent _battery_info{};
    SysInitEvent     _sys_init{};
    State            _state = State::offline;
    bool             _received_core{};
    bool             _dirty = true;
    bool             _initialized{};
    bool             _is_stale{};

    lv_obj_t                 *_screen         = nullptr;
    lv_obj_t                 *_title_right    = nullptr;
    lv_obj_t                 *_status_main    = nullptr;
    lv_obj_t                 *_bottom_info    = nullptr;
    lv_obj_t                 *_bottom_battery = nullptr;
    lv_obj_t                 *_flash_dot      = nullptr;
    std::array<lv_obj_t *, 8> _dots{}; // NOLINT
    int                       _flash_remaining_ms{};
};

} // namespace display
