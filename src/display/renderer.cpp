#define MODULE display

#include <array>
#include <cstdio>
#include <cstring>

#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <lvgl.h>

#include "battery_info_event.hpp"
#include "core_status_event.hpp"
#include "display.hpp"
#include "state_event.hpp"
#include "sys_init_event.hpp"

LOG_MODULE_DECLARE(MODULE, CONFIG_DISPLAY_MODULE_LOG_LEVEL); // NOLINT

namespace display
{

namespace
{

struct Layout
{
    int title_x;
    int title_y;
    int title_right_y;
    int status_y;

    int grid_y_start;
    int grid_x_left;
    int grid_x_right;
    int grid_row_h;
    int dot_size;
    int label_x_off;

    int bottom_y;
    int bottom_battery_y;

    int flash_dot_size;
    int flash_dot_margin;
};

constexpr int sat_msg_dot_flashing_duration_ms = 2000;

constexpr Layout layout{
    .title_x          = 5,
    .title_y          = 5,
    .title_right_y    = 10,
    .status_y         = 45,
    .grid_y_start     = 90,
    .grid_x_left      = 50,
    .grid_x_right     = 200,
    .grid_row_h       = 24,
    .dot_size         = 10,
    .label_x_off      = 6,
    .bottom_y         = 190,
    .bottom_battery_y = 215,
    .flash_dot_size   = 15,
    .flash_dot_margin = 5,
};

const lv_color_t colour_green   = lv_color_make(0, 255, 0);
const lv_color_t colour_grey    = lv_color_make(128, 128, 128);
const lv_color_t colour_red     = lv_color_make(255, 0, 0);
const lv_color_t colour_pink    = lv_color_make(255, 105, 180);
const lv_color_t colour_lt_grey = lv_color_make(180, 180, 180);

constexpr std::size_t display_buf_size = 160;

template <std::size_t OutSize, std::size_t SrcSize>
const char *string_or_na(std::array<char, OutSize>       &out,
                         const std::array<char, SrcSize> &src)
{
    const std::size_t len = strnlen(src.data(), src.size());
    if (len > 0)
    {
        (void)snprintk(out.data(), out.size(), "%.*s", static_cast<int>(len), src.data());
    }
    else
    {
        (void)snprintk(out.data(), out.size(), "N/A");
    }
    return out.data();
}

} // namespace

lv_color_t Display::status_to_colour(Status status)
{
    static const std::array<lv_color_t, 4> colours{
        colour_green, // online
        colour_grey,  // offline
        colour_red,   // error
        colour_pink,  // unknown
    };

    const auto idx = static_cast<std::size_t>(to_underlying(status));
    return colours.at(idx < colours.size() ? idx : colours.size() - 1);
}

Display::Display(const device *display_dev) : _dev(display_dev)
{
    _core_status.camera           = Status::offline;
    _core_status.controller       = Status::offline;
    _core_status.cloud_connection = Status::offline;
    _core_status.cellular         = Status::offline;
    _core_status.wifi             = Status::offline;
    _core_status.gps              = Status::offline;
    _core_status.tailscale        = Status::offline;
    _core_status.local_dash       = Status::offline;

    if (!device_is_ready(display_dev))
    {
        LOG_ERR("Display device not ready");
        return;
    }

    create_status_screen();
    display_blanking_off(_dev);
    lv_timer_handler();
    refresh_labels();

    _initialized = true;
}

void Display::create_status_screen()
{
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, lv_color_black(), 0);
    lv_obj_set_scrollbar_mode(_screen, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title_left = lv_label_create(_screen);
    lv_label_set_text(title_left, "MARLIN");
    lv_obj_set_style_text_font(title_left, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title_left, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(title_left, LV_ALIGN_TOP_LEFT, layout.title_x, layout.title_y);

    _title_right = lv_label_create(_screen);
    lv_obj_set_style_text_font(_title_right, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_title_right, lv_color_white(), 0);
    lv_obj_align(_title_right, LV_ALIGN_TOP_RIGHT, 0, layout.title_right_y);

    _status_main = lv_label_create(_screen);
    lv_label_set_text(_status_main, "OFFLINE");
    lv_obj_set_style_text_font(_status_main, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_status_main, colour_grey, 0);
    lv_obj_align(_status_main, LV_ALIGN_TOP_MID, 0, layout.status_y);

    const std::size_t mid = components.size() / 2;
    for (std::size_t i = 0; i < components.size(); ++i)
    {
        const bool left = i < mid;
        const int  col  = left ? layout.grid_x_left : layout.grid_x_right;
        const int  row  = left ? static_cast<int>(i) : static_cast<int>(i - mid);

        lv_obj_t *dot = lv_obj_create(_screen);
        lv_obj_set_size(dot, layout.dot_size, layout.dot_size);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, colour_grey, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_align(
            dot, LV_ALIGN_TOP_LEFT, col, layout.grid_y_start + (row * layout.grid_row_h));
        _dots.at(i) = dot;

        lv_obj_t *lbl = lv_label_create(_screen);
        lv_label_set_text(lbl, components.at(i).label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_align_to(lbl, dot, LV_ALIGN_OUT_RIGHT_MID, layout.label_x_off, 0);
    }

    _bottom_info = lv_label_create(_screen);
    lv_obj_set_style_text_font(_bottom_info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_bottom_info, colour_lt_grey, 0);
    lv_obj_align(_bottom_info, LV_ALIGN_TOP_MID, 0, layout.bottom_y);

    _bottom_battery = lv_label_create(_screen);
    lv_obj_set_style_text_font(_bottom_battery, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_bottom_battery, colour_lt_grey, 0);
    lv_obj_align(_bottom_battery, LV_ALIGN_TOP_MID, 0, layout.bottom_battery_y);

    _flash_dot = lv_obj_create(_screen);
    lv_obj_set_size(_flash_dot, layout.flash_dot_size, layout.flash_dot_size);
    lv_obj_set_style_radius(_flash_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_flash_dot, colour_red, 0);
    lv_obj_set_style_border_width(_flash_dot, 0, 0);
    lv_obj_align(_flash_dot,
                 LV_ALIGN_BOTTOM_LEFT,
                 layout.flash_dot_margin,
                 -layout.flash_dot_margin);
    lv_obj_add_flag(_flash_dot, LV_OBJ_FLAG_HIDDEN);

    lv_scr_load(_screen);
}

void Display::refresh_labels()
{
    if (!_initialized)
    {
        return;
    }

    update_title();
    update_status_main();
    update_dots();
    update_bottom_info();
    update_battery();
}

void Display::update_title()
{
    std::array<char, display_buf_size> buf{};
    lv_label_set_text(_title_right, string_or_na(buf, _sys_init.hostname));
}

void Display::update_status_main()
{
    struct StatePresentation
    {
        const char *text;
        lv_color_t  colour;
    };

    static const std::array<StatePresentation, 4> states{
        StatePresentation{.text = "MONITORING", .colour = colour_green},
        StatePresentation{.text = "ERROR", .colour = colour_red},
        StatePresentation{.text = "HARBOUR", .colour = lv_palette_main(LV_PALETTE_BLUE)},
        StatePresentation{.text = "OFFLINE", .colour = colour_grey}};

    const auto idx     = static_cast<std::size_t>(to_underlying(_state));
    const auto present = states.at(idx < states.size() ? idx : states.size() - 1);

    lv_label_set_text(_status_main, present.text);
    lv_obj_set_style_text_color(_status_main, present.colour, 0);
}

void Display::update_dots()
{
    for (std::size_t i = 0; i < components.size(); ++i)
    {
        if (_dots.at(i) != nullptr)
        {
            lv_obj_set_style_bg_color(
                _dots.at(i), status_to_colour(get_status(components.at(i).field)), 0);
        }
    }
}

void Display::update_bottom_info()
{
    std::array<char, display_buf_size> buf{};
    std::array<char, display_buf_size> ts_buf{};
    std::array<char, 16>               storage_buf{};

    const char *ts = string_or_na(ts_buf, _core_status.tailscale_ip);

    const bool storage_avail = _received_core && _core_status.storage_usage >= 0.0F;
    if (storage_avail)
    {
        (void)snprintk(storage_buf.data(),
                       storage_buf.size(),
                       "%d%%",
                       static_cast<int>(_core_status.storage_usage));
    }
    else
    {
        (void)snprintk(storage_buf.data(), storage_buf.size(), "N/A");
    }

    (void)snprintk(
        buf.data(), buf.size(), "TS: %s  |  STORAGE: %s", ts, storage_buf.data());
    lv_label_set_text(_bottom_info, buf.data());
}

void Display::update_battery()
{
    std::array<char, display_buf_size> buf{};
    std::array<char, 16>               v_buf{};
    std::array<char, 16>               i_buf{};
    std::array<char, 16>               p_buf{};

    const bool has_batt = _battery_info.valid;
    if (has_batt)
    {
        (void)snprintk(v_buf.data(),
                       v_buf.size(),
                       "%.2fV",
                       static_cast<double>(_battery_info.voltage));
        (void)snprintk(i_buf.data(),
                       i_buf.size(),
                       "%.2fA",
                       static_cast<double>(_battery_info.current));
        (void)snprintk(p_buf.data(),
                       p_buf.size(),
                       "%.2fW",
                       static_cast<double>(_battery_info.power));
    }
    else
    {
        (void)snprintk(v_buf.data(), v_buf.size(), "N/A");
        (void)snprintk(i_buf.data(), i_buf.size(), "N/A");
        (void)snprintk(p_buf.data(), p_buf.size(), "N/A");
    }

    (void)snprintk(buf.data(),
                   buf.size(),
                   "%s  |  %s  |  %s",
                   v_buf.data(),
                   i_buf.data(),
                   p_buf.data());
    lv_label_set_text(_bottom_battery, buf.data());
}

void Display::on_battery_info(BatteryInfoEvent &event)
{
    if (_battery_info != event)
    {
        _battery_info = event;
        _dirty        = true;
    }
}

void Display::on_core_status(CoreStatusEvent &event)
{
    if (!_received_core || _core_status != event)
    {
        _core_status   = event;
        _received_core = true;
        _dirty         = true;
        _is_stale      = false;
    }
}

void Display::on_sys_init(SysInitEvent &event)
{
    _sys_init = event;
    _dirty    = true;
}

void Display::on_state_event(StateEvent &event)
{
    if (_state != event.state)
    {
        _state = event.state;
        _dirty = true;
    }
}

void Display::on_timeout_event(CoreTimeoutEvent &event)
{
    ARG_UNUSED(event);
    _is_stale = true;
    _dirty    = true;
}

void Display::on_sat_com_msg(SatComMsgEvent &event)
{
    ARG_UNUSED(event);
    _flash_remaining_ms = sat_msg_dot_flashing_duration_ms;
}

void Display::update_flash_dot(int delta_ms)
{
    if (_flash_dot == nullptr)
    {
        return;
    }

    constexpr int flash_duration_ms = sat_msg_dot_flashing_duration_ms / 8;

    if (_flash_remaining_ms > 0)
    {
        if (_flash_remaining_ms <= delta_ms)
        {
            _flash_remaining_ms = 0;
        }
        else
        {
            _flash_remaining_ms -= delta_ms;
        }
    }

    if (_flash_remaining_ms > 0)
    {

        if ((_flash_remaining_ms / flash_duration_ms) % 2 == 0)
        {
            lv_obj_clear_flag(_flash_dot, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(_flash_dot, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
        lv_obj_add_flag(_flash_dot, LV_OBJ_FLAG_HIDDEN);
    }
}

void Display::update(int delta_ms)
{
    if (_dirty)
    {
        refresh_labels();
        _dirty = false;
    }

    update_flash_dot(delta_ms);

    lv_timer_handler();
}

Status Display::get_status(StatusField field) const
{
    if (_is_stale)
    {
        return Status::offline;
    }

    using Member = Status CoreStatusEvent::*;

    static constexpr std::array<Member, 8> fields{
        &CoreStatusEvent::camera,
        &CoreStatusEvent::controller,
        &CoreStatusEvent::cloud_connection,
        &CoreStatusEvent::cellular,
        &CoreStatusEvent::wifi,
        &CoreStatusEvent::gps,
        &CoreStatusEvent::tailscale,
        &CoreStatusEvent::local_dash,
    };

    const auto idx = static_cast<std::size_t>(to_underlying(field));
    if (idx >= fields.size())
    {
        return Status::unknown;
    }
    return _core_status.*fields.at(idx);
}

} // namespace display
