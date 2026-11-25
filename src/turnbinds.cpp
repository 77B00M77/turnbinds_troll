#include <cmath>
#include <optional>
#include <utility>
#include <pathcch.h>
#include "common.hpp"
#include <commctrl.h>
#include "resources.hpp"
#include "config.hpp"
#include "mouse_move_state.hpp"
#include "spin_state.hpp"

using namespace common;

enum class Bind
{
    LEFT,
    RIGHT,
    SPEED,
    CYCLE,
    SPIN,
    SLOWSPIN,
    COUNT
};

enum class Var
{
    RATE,
    SLEEP,
    SPIN_DURATION,
    SPIN_COUNT,
    SLOWSPIN_DURATION,
    SLOWSPIN_COUNT,
    COUNT
};

// Fork addition: spin variables don't affect the program's overall validity,
// so a bad spin value only disables spinning, never the turnbinds.
static constexpr auto is_spin_var(Var var)
{
    switch (var) {
    case Var::SPIN_DURATION:
    case Var::SPIN_COUNT:
    case Var::SLOWSPIN_DURATION:
    case Var::SLOWSPIN_COUNT:
        return true;
    default:
        return false;
    }
}

enum class CycleVar
{
    YAWSPEED,
    SENSITIVITY,
    ANGLESPEEDKEY,
    YAW,
    COUNT
};

enum class Switch
{
    ENABLED,
    SPIN_DIRECTION,
    COUNT
};

struct State
{
    bool developer;

    int binds[std::to_underlying(Bind::COUNT)];
    std::optional<double> vars[std::to_underlying(Var::COUNT)];
    std::vector<double> cycle_vars[std::to_underlying(CycleVar::COUNT)];
    bool switches[std::to_underlying(Switch::COUNT)];
    int current;

    // Fork addition: spin_count can hold multiple values, cycled by the cycle key
    // with its own index (independent of the cl_yawspeed/sensitivity/... cycle)
    std::vector<double> spin_counts;
    int spin_current;

    bool valid;
    int count;

    wchar_t vars_text[std::to_underlying(Var::COUNT)][128];
    wchar_t cycle_vars_text[std::to_underlying(CycleVar::COUNT)][128];
    int selected;

    std::optional<WINDOWPLACEMENT> placement;

    void on_config_loaded(Config &config);
    void on_config_save(Config &config);
    void on_var_changed(Var var);
    void on_cycle_vars_changed();
    void on_switch_changed(Switch switch_);
    void on_current_changed();
    void on_spin_current_changed();
    void on_valid_updated();

    void update_valid();
    void parse_var(Var var);
    void parse_cycle_vars();

    template<typename T>
    T current_cycle_var(CycleVar var) const
    {
        return cycle_vars[std::to_underlying(var)][current % cycle_vars[std::to_underlying(var)].size()];
    }
} g_state;

// Fork addition: settings window (replaces the original console screen).
// It edits exactly the same state the console screen did; turning/spin logic is unchanged.
struct Gui
{
    HWND hwnd = nullptr;
    bool capturing = false;

    void create(HINSTANCE instance, const wchar_t *class_name, const VersionInfo &version_info)
    {
        hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, class_name, version_info.title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr, nullptr, instance, nullptr);

        {
            auto dc = GetDC(nullptr);
            dpi = GetDeviceCaps(dc, LOGPIXELSY);
            ReleaseDC(nullptr, dc);
        }

        NONCLIENTMETRICSW metrics = {};
        metrics.cbSize = sizeof(metrics);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
        font = CreateFontIndirectW(&metrics.lfMessageFont);
        metrics.lfMessageFont.lfWeight = FW_SEMIBOLD;
        font_bold = CreateFontIndirectW(&metrics.lfMessageFont);

        tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, instance, nullptr);
        SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, s(340));
        SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 20000);

        this->instance = instance;
        loading = true;

        // Two columns of group boxes
        static constexpr int COL_W = 400;
        static constexpr int GAP = 10;
        int col_x[] = {GAP, GAP + COL_W + GAP};
        int col_y[] = {GAP, GAP};

        // Left column: binds + turning
        {
            Section sec(*this, col_x[0], col_y[0], COL_W, L"Keys");
            bind_row(sec, Bind::LEFT, L"+left", L"Hold to turn left.");
            bind_row(sec, Bind::RIGHT, L"+right", L"Hold to turn right.");
            bind_row(sec, Bind::SPEED, L"+speed", L"While held, turning speed is multiplied by cl_anglespeedkey.");
            bind_row(sec, Bind::CYCLE, L"cycle", L"Switches to the next value set (and the next spin_count value).");
            bind_row(sec, Bind::SPIN, L"spin", L"Fast spin. Click, then press the key you want. \"clear\" unbinds it.");
            bind_row(sec, Bind::SLOWSPIN, L"slow spin", L"Slow-motion spin. Click, then press the key you want. \"clear\" unbinds it.");
            col_y[0] = sec.end() + GAP;
        }
        {
            Section sec(*this, col_x[0], col_y[0], COL_W, L"Turning");
            cycle_row(sec, CycleVar::YAWSPEED, L"cl_yawspeed", L"Turning speed (degrees per second). Space-separate several values to switch between them with the cycle key.");
            cycle_row(sec, CycleVar::SENSITIVITY, L"sensitivity", L"Must match your in-game sensitivity. Space-separate several values to cycle them (then also cycle your in-game sensitivity).");
            cycle_row(sec, CycleVar::ANGLESPEEDKEY, L"cl_anglespeedkey", L"Factor applied to cl_yawspeed while +speed is held.");
            cycle_row(sec, CycleVar::YAW, L"m_yaw", L"Must match your in-game m_yaw (CS2 default: 0.022).");
            stepper_row(sec, L"active set", ID_CURRENT_PREV, ID_CURRENT_NEXT, current_text, L"Which value set is in use. The cycle key moves to the next one.");
            current_summary = sec.info_row();
            col_y[0] = sec.end() + GAP;
        }

        // Right column: spin + general
        {
            Section sec(*this, col_x[1], col_y[1], COL_W, L"Spin");
            var_row(sec, Var::SPIN_DURATION, L"spin_duration", L"How long a spin takes, in seconds.");
            var_row(sec, Var::SPIN_COUNT, L"spin_count", L"Rotations per spin. Space-separate several values (e.g. 1 2) to switch between them with the cycle key.");
            var_row(sec, Var::SLOWSPIN_DURATION, L"slowspin_duration", L"How long a slow spin takes, in seconds.");
            var_row(sec, Var::SLOWSPIN_COUNT, L"slowspin_count", L"Rotations per slow spin.");
            {
                auto y = sec.row();
                label(sec, y, L"spin_direction", L"Which way both spins turn.");
                direction_combo = control(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, sec.cx, y, 120, 200, ID_DIRECTION);
                SendMessageW(direction_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"left"));
                SendMessageW(direction_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"right"));
                add_tip(direction_combo, L"Which way both spins turn.");
            }
            stepper_row(sec, L"active spin_count", ID_SPIN_PREV, ID_SPIN_NEXT, spin_current_text, L"Which spin_count value the spin key uses. The cycle key moves to the next one.");
            spin_summary = sec.info_row();
            spin_summary2 = sec.info_row();
            col_y[1] = sec.end() + GAP;
        }
        {
            Section sec(*this, col_x[1], col_y[1], COL_W, L"General");
            {
                auto y = sec.row();
                enabled_check = control(0, WC_BUTTONW, L"enabled", BS_AUTOCHECKBOX | WS_TABSTOP, sec.lx, y, 200, ROW_H, ID_ENABLED);
                add_tip(enabled_check, L"Turns the whole program on or off.");
            }
            var_row(sec, Var::RATE, L"rate", L"Maximum simulated mouse inputs per second (default 1000). Lower = less CPU, less smooth.");
            var_row(sec, Var::SLEEP, L"sleep", L"Main loop sleep in 100ns units (default 3500 = 0.35 ms). Higher = less CPU, slower polling.");
            col_y[1] = sec.end() + GAP;
        }

        // Footer
        int y = std::max(col_y[0], col_y[1]);
        int width = col_x[1] + COL_W - GAP;
        status_text = control(0, WC_STATICW, L"", SS_LEFT | SS_NOPREFIX, GAP + 2, y, width, 20, 0);
        SendMessageW(status_text, WM_SETFONT, reinterpret_cast<WPARAM>(font_bold), true);
        y += 22;
        control(0, WC_STATICW, L"Hover over a setting for help. Changes apply instantly and are saved when you close this window.", SS_LEFT | SS_NOPREFIX, GAP + 2, y, width, 20, 0);
        y += 22;
        {
            wchar_t credit[256];
            std::swprintf(credit, std::size(credit), L"%s %s  ·  fork of %s (MIT)", version_info.title, version_info.version, version_info.copyright);
            auto c = control(0, WC_STATICW, credit, SS_LEFT | SS_NOPREFIX, GAP + 2, y, width, 20, 0);
            credit_text = c;
        }
        y += 22 + GAP;

        // Group boxes go to the bottom of the Z-order so they never paint over their contents
        for (auto group : groups) {
            SetWindowPos(group, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }

        // Size and place the window
        RECT rect = {0, 0, s(col_x[1] + COL_W + GAP), s(y)};
        AdjustWindowRectEx(&rect, GetWindowLongW(hwnd, GWL_STYLE), false, GetWindowLongW(hwnd, GWL_EXSTYLE));
        auto w = rect.right - rect.left;
        auto h = rect.bottom - rect.top;

        POINT pos;
        if (g_state.placement && MonitorFromPoint({g_state.placement->rcNormalPosition.left + 40, g_state.placement->rcNormalPosition.top + 20}, MONITOR_DEFAULTTONULL)) {
            pos = {g_state.placement->rcNormalPosition.left, g_state.placement->rcNormalPosition.top};
        } else {
            MONITORINFO info = {};
            info.cbSize = sizeof(info);
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &info);
            pos = {info.rcWork.left + (info.rcWork.right - info.rcWork.left - w) / 2, info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - h) / 2};
        }
        {
            // Keep the whole window on the screen it opens on
            MONITORINFO info = {};
            info.cbSize = sizeof(info);
            GetMonitorInfoW(MonitorFromPoint(pos, MONITOR_DEFAULTTONEAREST), &info);
            pos.x = std::max(info.rcWork.left, std::min(pos.x, info.rcWork.right - static_cast<LONG>(w)));
            pos.y = std::max(info.rcWork.top, std::min(pos.y, info.rcWork.bottom - static_cast<LONG>(h)));
        }
        SetWindowPos(hwnd, nullptr, pos.x, pos.y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);

        // Initial values
        for (int i = 0; i < std::to_underlying(Bind::COUNT); ++i) {
            update_bind_text(static_cast<Bind>(i));
        }
        for (int i = 0; i < std::to_underlying(Var::COUNT); ++i) {
            SetWindowTextW(var_edits[i], g_state.vars_text[i]);
        }
        for (int i = 0; i < std::to_underlying(CycleVar::COUNT); ++i) {
            SetWindowTextW(cycle_edits[i], g_state.cycle_vars_text[i]);
        }
        SendMessageW(direction_combo, CB_SETCURSEL, g_state.switches[std::to_underlying(Switch::SPIN_DIRECTION)] ? 1 : 0, 0);
        SendMessageW(enabled_check, BM_SETCHECK, g_state.switches[std::to_underlying(Switch::ENABLED)] ? BST_CHECKED : BST_UNCHECKED, 0);

        loading = false;
        refresh();
    }

    void destroy_resources()
    {
        DeleteObject(font);
        DeleteObject(font_bold);
    }

    // Called every main loop iteration: polls for a key press while a bind is being captured
    void run()
    {
        if (!capturing) {
            return;
        }

        auto captured = bind_capturer.run();
        if (captured) {
            if (*captured != VK_ESCAPE) {
                g_state.binds[std::to_underlying(capture_bind)] = *captured;
            }
            stop_capture();
        }
    }

    LRESULT handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void on_switch_changed(Switch switch_)
    {
        refresh();
    }

    void on_current_changed()
    {
        refresh();
    }

    void on_spin_current_changed()
    {
        refresh();
    }

    void on_valid_updated()
    {
        refresh();
    }

private:
    enum : int
    {
        ID_BIND = 100,
        ID_BIND_CLEAR = 150,
        ID_VAR = 200,
        ID_CYCLE_VAR = 300,
        ID_CURRENT_PREV = 400,
        ID_CURRENT_NEXT,
        ID_SPIN_PREV,
        ID_SPIN_NEXT,
        ID_DIRECTION,
        ID_ENABLED,
    };

    static constexpr int ROW_H = 23;
    static constexpr int ROW_STEP = 29;
    static constexpr UINT_PTR CAPTURE_TIMER = 1;

    struct Section
    {
        Gui &gui;
        int x, y, w;
        int lx, cx, cw;
        int next;
        HWND group;

        Section(Gui &gui, int x, int y, int w, const wchar_t *title) :
            gui(gui), x(x), y(y), w(w)
        {
            lx = x + 12;
            cx = x + 146;
            cw = w - 146 - 12;
            next = y + 24;
            group = gui.control(0, WC_BUTTONW, title, BS_GROUPBOX, x, y, w, 10, 0);
            gui.groups.push_back(group);
        }

        int row()
        {
            auto r = next;
            next += ROW_STEP;
            return r;
        }

        HWND info_row()
        {
            auto r = next;
            next += 21;
            return gui.control(0, WC_STATICW, L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, lx, r + 2, w - 24, 19, 0);
        }

        int end()
        {
            auto bottom = next + 6;
            SetWindowPos(group, nullptr, 0, 0, gui.s(w), gui.s(bottom - y), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            return bottom;
        }
    };

    struct {
        std::array<bool, win32::VIRTUAL_KEYS> down;

        void start()
        {
            for (int vk = 0; vk < std::size(down); ++vk) {
                down[vk] = win32::is_key_down(vk);
            }
        }

        std::optional<int> run()
        {
            for (int vk = 0; vk < std::size(down); ++vk) {
                if (vk == VK_RETURN) {
                    continue;
                }

                auto prev_down = down[vk];
                down[vk] = win32::is_key_down(vk);
                if (!prev_down && down[vk]) {
                    auto scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
                    if (scan != 0 && vk != MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX)) {
                        continue;
                    }
                    return vk;
                }
            }

            return std::nullopt;
        }
    } bind_capturer;

    HINSTANCE instance;
    int dpi = 96;
    HFONT font;
    HFONT font_bold;
    HWND tooltip;
    bool loading = false;
    Bind capture_bind = Bind::COUNT;

    std::vector<HWND> groups;
    HWND bind_buttons[std::to_underlying(Bind::COUNT)] = {};
    HWND bind_labels[std::to_underlying(Bind::COUNT)] = {};
    HWND var_edits[std::to_underlying(Var::COUNT)] = {};
    HWND var_labels[std::to_underlying(Var::COUNT)] = {};
    HWND cycle_edits[std::to_underlying(CycleVar::COUNT)] = {};
    HWND cycle_labels[std::to_underlying(CycleVar::COUNT)] = {};
    HWND current_text;
    HWND current_summary;
    HWND spin_current_text;
    HWND spin_summary;
    HWND spin_summary2;
    HWND direction_combo;
    HWND enabled_check;
    HWND status_text;
    HWND credit_text;

    int s(int v) const
    {
        return MulDiv(v, dpi, 96);
    }

    HWND control(DWORD ex_style, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id)
    {
        auto c = CreateWindowExW(ex_style, cls, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, s(x), s(y), s(w), s(h), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), false);
        return c;
    }

    void add_tip(HWND control, const wchar_t *text)
    {
        TTTOOLINFOW info = {};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = hwnd;
        info.uId = reinterpret_cast<UINT_PTR>(control);
        info.lpszText = const_cast<wchar_t *>(text);
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    }

    HWND label(Section &sec, int y, const wchar_t *text, const wchar_t *tip)
    {
        auto l = control(0, WC_STATICW, text, SS_LEFT | SS_NOPREFIX | SS_NOTIFY, sec.lx, y + 3, sec.cx - sec.lx - 4, 18, 0);
        add_tip(l, tip);
        return l;
    }

    HWND edit(Section &sec, int y, int id, const wchar_t *tip)
    {
        auto e = control(WS_EX_CLIENTEDGE, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, sec.cx, y, sec.cw, ROW_H, id);
        SendMessageW(e, EM_LIMITTEXT, 127, 0);
        add_tip(e, tip);
        return e;
    }

    void bind_row(Section &sec, Bind bind, const wchar_t *name, const wchar_t *tip)
    {
        auto y = sec.row();
        auto i = std::to_underlying(bind);
        bind_labels[i] = label(sec, y, name, tip);

        bool clearable = (bind == Bind::SPIN || bind == Bind::SLOWSPIN);
        auto w = clearable ? sec.cw - 66 : sec.cw;
        bind_buttons[i] = control(0, WC_BUTTONW, L"", BS_PUSHBUTTON | WS_TABSTOP, sec.cx, y, w, ROW_H, ID_BIND + i);
        add_tip(bind_buttons[i], tip);

        if (clearable) {
            auto c = control(0, WC_BUTTONW, L"clear", BS_PUSHBUTTON | WS_TABSTOP, sec.cx + w + 6, y, 60, ROW_H, ID_BIND_CLEAR + i);
            add_tip(c, L"Unbind this key.");
        }
    }

    void var_row(Section &sec, Var var, const wchar_t *name, const wchar_t *tip)
    {
        auto y = sec.row();
        auto i = std::to_underlying(var);
        var_labels[i] = label(sec, y, name, tip);
        var_edits[i] = edit(sec, y, ID_VAR + i, tip);
    }

    void cycle_row(Section &sec, CycleVar var, const wchar_t *name, const wchar_t *tip)
    {
        auto y = sec.row();
        auto i = std::to_underlying(var);
        cycle_labels[i] = label(sec, y, name, tip);
        cycle_edits[i] = edit(sec, y, ID_CYCLE_VAR + i, tip);
    }

    void stepper_row(Section &sec, const wchar_t *name, int prev_id, int next_id, HWND &text, const wchar_t *tip)
    {
        auto y = sec.row();
        label(sec, y, name, tip);
        auto prev = control(0, WC_BUTTONW, L"◀", BS_PUSHBUTTON | WS_TABSTOP, sec.cx, y, 30, ROW_H, prev_id);
        text = control(0, WC_STATICW, L"", SS_CENTER | SS_NOPREFIX, sec.cx + 34, y + 3, 70, 18, 0);
        auto next = control(0, WC_BUTTONW, L"▶", BS_PUSHBUTTON | WS_TABSTOP, sec.cx + 108, y, 30, ROW_H, next_id);
        add_tip(prev, tip);
        add_tip(next, tip);
    }

    void update_bind_text(Bind bind)
    {
        auto i = std::to_underlying(bind);
        wchar_t buffer[128];
        if (capturing && capture_bind == bind) {
            std::swprintf(buffer, std::size(buffer), L"press any key... (Esc = cancel)");
        } else if (g_state.binds[i] == 0) {
            std::swprintf(buffer, std::size(buffer), L"<unbound>");
        } else if (!win32::get_vk_string(g_state.binds[i], buffer, std::size(buffer))) {
            std::swprintf(buffer, std::size(buffer), L"key %d", g_state.binds[i]);
        }
        SetWindowTextW(bind_buttons[i], buffer);
    }

    void start_capture(Bind bind)
    {
        if (capturing) {
            return;
        }

        capturing = true;
        capture_bind = bind;
        update_bind_text(bind);

        // Keep the key/click used for binding away from the controls
        SetFocus(hwnd);
        SetCapture(hwnd);
        SetTimer(hwnd, CAPTURE_TIMER, 5, nullptr);
        bind_capturer.start();
    }

    void stop_capture()
    {
        auto bind = capture_bind;
        capturing = false;
        capture_bind = Bind::COUNT;
        KillTimer(hwnd, CAPTURE_TIMER);
        ReleaseCapture();
        update_bind_text(bind);
    }

    bool is_invalid_control(HWND control) const
    {
        for (int i = 0; i < std::to_underlying(Var::COUNT); ++i) {
            if (control == var_edits[i] || control == var_labels[i]) {
                return !g_state.vars[i];
            }
        }
        for (int i = 0; i < std::to_underlying(CycleVar::COUNT); ++i) {
            if (control == cycle_edits[i] || control == cycle_labels[i]) {
                return g_state.count == 0;
            }
        }
        return false;
    }

    void refresh()
    {
        wchar_t buffer[256];

        if (g_state.count > 0) {
            std::swprintf(buffer, std::size(buffer), L"%d of %d", g_state.current + 1, g_state.count);
            SetWindowTextW(current_text, buffer);
            std::swprintf(buffer, std::size(buffer), L"Using: %g°/s  ·  sens %g  ·  speedkey %g  ·  m_yaw %g",
                g_state.current_cycle_var<double>(CycleVar::YAWSPEED),
                g_state.current_cycle_var<double>(CycleVar::SENSITIVITY),
                g_state.current_cycle_var<double>(CycleVar::ANGLESPEEDKEY),
                g_state.current_cycle_var<double>(CycleVar::YAW));
            SetWindowTextW(current_summary, buffer);
        } else {
            SetWindowTextW(current_text, L"-");
            SetWindowTextW(current_summary, L"Fix the values in red (multiple values must have the same count).");
        }

        auto &spin_counts = g_state.spin_counts;
        if (!spin_counts.empty()) {
            std::swprintf(buffer, std::size(buffer), L"%d of %d", g_state.spin_current + 1, static_cast<int>(spin_counts.size()));
        } else {
            std::swprintf(buffer, std::size(buffer), L"-");
        }
        SetWindowTextW(spin_current_text, buffer);

        bool direction_right = g_state.switches[std::to_underlying(Switch::SPIN_DIRECTION)];
        auto spin_duration = g_state.vars[std::to_underlying(Var::SPIN_DURATION)];
        auto slowspin_duration = g_state.vars[std::to_underlying(Var::SLOWSPIN_DURATION)];
        auto slowspin_count = g_state.vars[std::to_underlying(Var::SLOWSPIN_COUNT)];

        if (!spin_counts.empty() && spin_duration) {
            auto rotations = spin_counts[g_state.spin_current % spin_counts.size()];
            std::swprintf(buffer, std::size(buffer), L"spin: %g° %s in %g s", rotations * 360.0, direction_right ? L"right" : L"left", *spin_duration);
        } else {
            std::swprintf(buffer, std::size(buffer), L"spin: off (fix the values in red)");
        }
        if (slowspin_count && slowspin_duration) {
            auto len = std::wcslen(buffer);
            std::swprintf(buffer + len, std::size(buffer) - len, L"   ·   slow spin: %g° in %g s", *slowspin_count * 360.0, *slowspin_duration);
        } else {
            auto len = std::wcslen(buffer);
            std::swprintf(buffer + len, std::size(buffer) - len, L"   ·   slow spin: off");
        }
        SetWindowTextW(spin_summary, buffer);

        if (g_state.count > 0) {
            auto sensitivity = g_state.current_cycle_var<double>(CycleVar::SENSITIVITY);
            auto yaw = g_state.current_cycle_var<double>(CycleVar::YAW);
            if (sensitivity * yaw > 0.0) {
                std::swprintf(buffer, std::size(buffer), L"One 360° = %.1f mouse counts at sensitivity %g, m_yaw %g", 360.0 / (sensitivity * yaw), sensitivity, yaw);
            } else {
                std::swprintf(buffer, std::size(buffer), L"sensitivity and m_yaw must be above 0 to spin");
            }
        } else {
            buffer[0] = L'\0';
        }
        SetWindowTextW(spin_summary2, buffer);

        if (!g_state.valid) {
            SetWindowTextW(status_text, L"Error: fix the values shown in red. Turnbinds are paused until then.");
        } else if (!g_state.switches[std::to_underlying(Switch::ENABLED)]) {
            SetWindowTextW(status_text, L"Disabled. Tick \"enabled\" to turn it back on.");
        } else {
            SetWindowTextW(status_text, L"Ready. Turnbinds and spins work in-game (whenever the game hides the cursor).");
        }

        for (auto controls : {std::begin(var_edits), std::begin(var_labels)}) {
            for (int i = 0; i < std::to_underlying(Var::COUNT); ++i) {
                InvalidateRect(controls[i], nullptr, true);
            }
        }
        for (auto controls : {std::begin(cycle_edits), std::begin(cycle_labels)}) {
            for (int i = 0; i < std::to_underlying(CycleVar::COUNT); ++i) {
                InvalidateRect(controls[i], nullptr, true);
            }
        }
    }

    void read_edit(HWND edit, wchar_t *text, size_t count)
    {
        wchar_t buffer[128];
        GetWindowTextW(edit, buffer, std::size(buffer));
        std::wcsncpy(text, wcsstrip(buffer), count - 1);
        text[count - 1] = L'\0';
    }

    friend struct App;
};

struct App
{
    inline static std::optional<VersionInfo> version_info;
    inline static std::optional<Gui> gui;
    inline static std::optional<Config> config;
    inline static wchar_t ini_path[PATHCCH_MAX_CCH];

    static void run()
    {
        auto instance = GetModuleHandle(nullptr);

        wchar_t image_path[PATHCCH_MAX_CCH];
        GetModuleFileNameW(nullptr, image_path, std::size(image_path));

        version_info.emplace(image_path);
        auto icon = load_icon_resource(instance);

        std::wcscpy(ini_path, image_path);
        PathCchRenameExtension(ini_path, std::size(ini_path), L"ini");

        INITCOMMONCONTROLSEX icc = {};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
        InitCommonControlsEx(&icc);

        config.emplace(ini_path, version_info->name);
        g_state.on_config_loaded(*config);

        WNDCLASSEXW cls = {};
        cls.cbSize = sizeof(WNDCLASSEX);
        cls.lpfnWndProc = [](HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            return App::gui->handle(hwnd, uMsg, wParam, lParam);
        };
        cls.hInstance = instance;
        cls.hIcon = icon;
        cls.hIconSm = icon;
        cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        cls.lpszClassName = version_info->name;
        RegisterClassExW(&cls);

        gui.emplace();
        gui->create(instance, version_info->name, *version_info);
        reset_window_title();
        ShowWindow(gui->hwnd, SW_SHOWNORMAL);

        win32::CursorVisibilityObserver cursor_visibility_observer;

        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        win32::set_timer_resolution(1);

        bool active = false;
        std::array<bool, std::to_underlying(Bind::COUNT)> binds_down;
        MouseMoveState mouse_move_state;
        SpinState spin_state;

        do {
            bool quit = false;
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    quit = true;
                    break;
                }
                if (!IsDialogMessageW(gui->hwnd, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            if (quit) {
                break;
            }

            gui->run();

            auto prev_active = active;
            active =
                !gui->capturing &&
                g_state.switches[std::to_underlying(Switch::ENABLED)] &&
                g_state.valid &&
                !cursor_visibility_observer.visible();

            if (!active) {
                spin_state.cancel();

                MsgWaitForMultipleObjects(0, nullptr, false, INFINITE, QS_ALLINPUT);
                continue;
            }

            decltype(binds_down) prev_binds_down;
            if (!prev_active) {
                for (int i = 0; i < std::size(g_state.binds); ++i) {
                    binds_down[i] = win32::is_key_down(g_state.binds[i]);
                }
                prev_binds_down = binds_down;
            } else {
                prev_binds_down = binds_down;
                for (int i = 0; i < std::size(g_state.binds); ++i) {
                    binds_down[i] = win32::is_key_down(g_state.binds[i]);
                }
            }

            if (!prev_binds_down[std::to_underlying(Bind::CYCLE)] && binds_down[std::to_underlying(Bind::CYCLE)]) {
                g_state.current = (g_state.current + 1) % g_state.count;

                if (g_state.spin_counts.size() > 1) {
                    g_state.spin_current = (g_state.spin_current + 1) % g_state.spin_counts.size();
                }

                g_state.on_current_changed();
            }

            // Fork addition: spin binds (presses while a spin is in progress are ignored)
            if (!spin_state.spinning()) {
                auto spin_pressed = [&](Bind bind) {
                    return g_state.binds[std::to_underlying(bind)] != 0 && !prev_binds_down[std::to_underlying(bind)] && binds_down[std::to_underlying(bind)];
                };

                std::optional<double> rotations;
                std::optional<double> duration;
                if (spin_pressed(Bind::SPIN)) {
                    if (g_state.vars[std::to_underlying(Var::SPIN_DURATION)] && g_state.vars[std::to_underlying(Var::SPIN_COUNT)]) {
                        rotations = g_state.spin_counts[g_state.spin_current % g_state.spin_counts.size()];
                        duration = g_state.vars[std::to_underlying(Var::SPIN_DURATION)];
                    }
                } else if (spin_pressed(Bind::SLOWSPIN)) {
                    if (g_state.vars[std::to_underlying(Var::SLOWSPIN_DURATION)] && g_state.vars[std::to_underlying(Var::SLOWSPIN_COUNT)]) {
                        rotations = g_state.vars[std::to_underlying(Var::SLOWSPIN_COUNT)];
                        duration = g_state.vars[std::to_underlying(Var::SLOWSPIN_DURATION)];
                    }
                }

                if (rotations) {
                    // Currently active (cycled) sensitivity and m_yaw values
                    auto sensitivity = g_state.current_cycle_var<double>(CycleVar::SENSITIVITY);
                    auto yaw = g_state.current_cycle_var<double>(CycleVar::YAW);

                    if (sensitivity * yaw > 0.0) {
                        auto counts = (360.0 * *rotations) / (sensitivity * yaw);
                        if (!g_state.switches[std::to_underlying(Switch::SPIN_DIRECTION)]) {
                            counts = -counts;
                        }
                        spin_state.start(counts, *duration);
                    }
                }
            }

            auto spin_amount = spin_state.update(*g_state.vars[std::to_underlying(Var::RATE)]);

            auto amount = mouse_move_state.update(
                !prev_active,
                binds_down[std::to_underlying(Bind::LEFT)],
                binds_down[std::to_underlying(Bind::RIGHT)],
                binds_down[std::to_underlying(Bind::SPEED)],
                *g_state.vars[std::to_underlying(Var::RATE)],
                g_state.current_cycle_var<double>(CycleVar::YAWSPEED),
                g_state.current_cycle_var<double>(CycleVar::ANGLESPEEDKEY),
                g_state.current_cycle_var<double>(CycleVar::SENSITIVITY),
                g_state.current_cycle_var<double>(CycleVar::YAW));
            amount += spin_amount;
            if (amount != 0) {
                win32::move_mouse_by(amount, 0);
            }

            win32::delay_execution_by(*g_state.vars[std::to_underlying(Var::SLEEP)]);
        } while (true);

        save();
        gui->destroy_resources();
    }

    static void save()
    {
        g_state.on_config_save(*config);
        config->save(ini_path, version_info->name);
    }

    static void save_placement()
    {
        WINDOWPLACEMENT placement = {};
        placement.length = sizeof(placement);
        if (gui && gui->hwnd && GetWindowPlacement(gui->hwnd, &placement)) {
            g_state.placement = placement;
        }
    }

    static void reset_window_title()
    {
        if (!gui || !gui->hwnd) {
            return;
        }

        wchar_t buffer[128];
        std::swprintf(buffer, std::size(buffer), L"%s%s", version_info->title, (!g_state.valid ? L" (error)" : (g_state.switches[std::to_underlying(Switch::ENABLED)] ? L"" : L" (disabled)")));
        SetWindowTextW(gui->hwnd, buffer);
    }
};

LRESULT Gui::handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        {
            if (loading) {
                break;
            }

            auto id = LOWORD(wParam);
            auto code = HIWORD(wParam);

            if (capturing) {
                return 0;
            }

            if (id >= ID_BIND && id < ID_BIND + std::to_underlying(Bind::COUNT) && code == BN_CLICKED) {
                start_capture(static_cast<Bind>(id - ID_BIND));
                return 0;
            }

            if (id >= ID_BIND_CLEAR && id < ID_BIND_CLEAR + std::to_underlying(Bind::COUNT) && code == BN_CLICKED) {
                auto bind = static_cast<Bind>(id - ID_BIND_CLEAR);
                g_state.binds[std::to_underlying(bind)] = 0;
                update_bind_text(bind);
                return 0;
            }

            if (id >= ID_VAR && id < ID_VAR + std::to_underlying(Var::COUNT) && code == EN_CHANGE) {
                auto var = static_cast<Var>(id - ID_VAR);
                read_edit(var_edits[id - ID_VAR], g_state.vars_text[std::to_underlying(var)], std::size(g_state.vars_text[0]));
                g_state.on_var_changed(var);
                return 0;
            }

            if (id >= ID_CYCLE_VAR && id < ID_CYCLE_VAR + std::to_underlying(CycleVar::COUNT) && code == EN_CHANGE) {
                read_edit(cycle_edits[id - ID_CYCLE_VAR], g_state.cycle_vars_text[id - ID_CYCLE_VAR], std::size(g_state.cycle_vars_text[0]));
                g_state.on_cycle_vars_changed();
                return 0;
            }

            switch (id) {
            case ID_CURRENT_PREV:
            case ID_CURRENT_NEXT:
                if (code == BN_CLICKED && g_state.count > 0) {
                    bool next = (id == ID_CURRENT_NEXT);
                    g_state.current = (next ? (g_state.current + 1) : (g_state.current - 1 + g_state.count)) % g_state.count;
                    g_state.on_current_changed();
                }
                return 0;
            case ID_SPIN_PREV:
            case ID_SPIN_NEXT:
                if (code == BN_CLICKED && !g_state.spin_counts.empty()) {
                    bool next = (id == ID_SPIN_NEXT);
                    auto size = static_cast<int>(g_state.spin_counts.size());
                    g_state.spin_current = (next ? (g_state.spin_current + 1) : (g_state.spin_current - 1 + size)) % size;
                    g_state.on_spin_current_changed();
                }
                return 0;
            case ID_DIRECTION:
                if (code == CBN_SELCHANGE) {
                    g_state.switches[std::to_underlying(Switch::SPIN_DIRECTION)] = (SendMessageW(direction_combo, CB_GETCURSEL, 0, 0) == 1);
                    g_state.on_switch_changed(Switch::SPIN_DIRECTION);
                }
                return 0;
            case ID_ENABLED:
                if (code == BN_CLICKED) {
                    g_state.switches[std::to_underlying(Switch::ENABLED)] = (SendMessageW(enabled_check, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    g_state.on_switch_changed(Switch::ENABLED);
                }
                return 0;
            }
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        {
            auto control = reinterpret_cast<HWND>(lParam);
            if (is_invalid_control(control)) {
                auto dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, RGB(220, 20, 20));
                if (msg == WM_CTLCOLOREDIT) {
                    SetBkColor(dc, GetSysColor(COLOR_WINDOW));
                    return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
                }
                SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            }
            if (control == credit_text) {
                auto dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
                SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            }
        }
        break;
    case WM_TIMER:
        // Only used to keep the main loop polling while a bind is being captured
        return 0;
    case WM_CLOSE:
        App::save_placement();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_ENDSESSION:
        if (wParam) {
            App::save_placement();
            App::save();
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void State::on_config_loaded(Config &config)
{
    g_state.developer = config.developer;
    g_state.binds[std::to_underlying(Bind::LEFT)] = config.bind_left;
    g_state.binds[std::to_underlying(Bind::RIGHT)] = config.bind_right;
    g_state.binds[std::to_underlying(Bind::SPEED)] = config.bind_speed;
    g_state.binds[std::to_underlying(Bind::CYCLE)] = config.bind_cycle;
    g_state.binds[std::to_underlying(Bind::SPIN)] = config.bind_spin;
    g_state.binds[std::to_underlying(Bind::SLOWSPIN)] = config.bind_slowspin;
    std::wcscpy(g_state.vars_text[std::to_underlying(Var::RATE)], config.rate);
    std::wcscpy(g_state.vars_text[std::to_underlying(Var::SLEEP)], config.sleep);
    std::wcscpy(g_state.vars_text[std::to_underlying(Var::SPIN_DURATION)], config.spin_duration);
    std::wcscpy(g_state.vars_text[std::to_underlying(Var::SPIN_COUNT)], config.spin_count);
    std::wcscpy(g_state.vars_text[std::to_underlying(Var::SLOWSPIN_DURATION)], config.slowspin_duration);
    std::wcscpy(g_state.vars_text[std::to_underlying(Var::SLOWSPIN_COUNT)], config.slowspin_count);
    g_state.switches[std::to_underlying(Switch::SPIN_DIRECTION)] = config.spin_right;
    g_state.spin_current = config.spin_current;
    std::wcscpy(g_state.cycle_vars_text[std::to_underlying(CycleVar::YAWSPEED)], config.cl_yawspeed);
    std::wcscpy(g_state.cycle_vars_text[std::to_underlying(CycleVar::SENSITIVITY)], config.sensitivity);
    std::wcscpy(g_state.cycle_vars_text[std::to_underlying(CycleVar::ANGLESPEEDKEY)], config.cl_anglespeedkey);
    std::wcscpy(g_state.cycle_vars_text[std::to_underlying(CycleVar::YAW)], config.m_yaw);
    g_state.switches[std::to_underlying(Switch::ENABLED)] = config.enabled;
    g_state.current = config.current;
    g_state.selected = config.selected;
    g_state.placement = config.placement;

    for (int i = 0; i < std::to_underlying(Var::COUNT); ++i) {
        parse_var(static_cast<Var>(i));
    }
    if (g_state.spin_current < 0 || g_state.spin_counts.empty() || g_state.spin_current > static_cast<int>(g_state.spin_counts.size()) - 1) {
        g_state.spin_current = 0;
    }

    parse_cycle_vars();
    if (g_state.count > 0 && (g_state.current < 0 || g_state.current > g_state.count - 1)) {
        g_state.current = 0;
    }

    update_valid();
}

void State::on_config_save(Config &config)
{
    config.developer = g_state.developer;
    config.bind_left = g_state.binds[std::to_underlying(Bind::LEFT)];
    config.bind_right = g_state.binds[std::to_underlying(Bind::RIGHT)];
    config.bind_speed = g_state.binds[std::to_underlying(Bind::SPEED)];
    config.bind_cycle = g_state.binds[std::to_underlying(Bind::CYCLE)];
    config.bind_spin = g_state.binds[std::to_underlying(Bind::SPIN)];
    config.bind_slowspin = g_state.binds[std::to_underlying(Bind::SLOWSPIN)];
    std::wcscpy(config.rate, g_state.vars_text[std::to_underlying(Var::RATE)]);
    std::wcscpy(config.sleep, g_state.vars_text[std::to_underlying(Var::SLEEP)]);
    std::wcscpy(config.spin_duration, g_state.vars_text[std::to_underlying(Var::SPIN_DURATION)]);
    std::wcscpy(config.spin_count, g_state.vars_text[std::to_underlying(Var::SPIN_COUNT)]);
    std::wcscpy(config.slowspin_duration, g_state.vars_text[std::to_underlying(Var::SLOWSPIN_DURATION)]);
    std::wcscpy(config.slowspin_count, g_state.vars_text[std::to_underlying(Var::SLOWSPIN_COUNT)]);
    config.spin_right = g_state.switches[std::to_underlying(Switch::SPIN_DIRECTION)];
    config.spin_current = g_state.spin_current;
    std::wcscpy(config.cl_yawspeed, g_state.cycle_vars_text[std::to_underlying(CycleVar::YAWSPEED)]);
    std::wcscpy(config.sensitivity, g_state.cycle_vars_text[std::to_underlying(CycleVar::SENSITIVITY)]);
    std::wcscpy(config.cl_anglespeedkey, g_state.cycle_vars_text[std::to_underlying(CycleVar::ANGLESPEEDKEY)]);
    std::wcscpy(config.m_yaw, g_state.cycle_vars_text[std::to_underlying(CycleVar::YAW)]);
    config.enabled = g_state.switches[std::to_underlying(Switch::ENABLED)];
    config.current = g_state.current;
    config.selected = g_state.selected;
    config.placement = g_state.placement;
}

void State::on_var_changed(Var var)
{
    auto prev_spin_counts = g_state.spin_counts.size();
    parse_var(var);
    if (g_state.spin_counts.size() != prev_spin_counts || g_state.spin_current > static_cast<int>(g_state.spin_counts.size()) - 1) {
        g_state.spin_current = 0;
    }
    update_valid();
}

void State::on_cycle_vars_changed()
{
    auto prev_count = g_state.count;
    parse_cycle_vars();
    if (g_state.count != prev_count) {
        g_state.current = 0;
    }

    update_valid();
}

void State::on_switch_changed(Switch switch_)
{
    App::gui->on_switch_changed(switch_);

    switch (switch_) {
    case Switch::ENABLED:
        App::reset_window_title();
        break;
    }
}

void State::on_current_changed()
{
    App::gui->on_current_changed();
}

void State::on_spin_current_changed()
{
    App::gui->on_spin_current_changed();
}

void State::on_valid_updated()
{
    App::reset_window_title();
    if (App::gui && App::gui->hwnd) {
        App::gui->on_valid_updated();
    }
}

void State::update_valid()
{
    g_state.valid = (g_state.count > 0);
    for (int i = 0; (i < std::to_underlying(Var::COUNT)) && (g_state.valid); ++i) {
        if (is_spin_var(static_cast<Var>(i))) {
            continue;
        }
        g_state.valid = (g_state.valid && g_state.vars[i]);
    }
    on_valid_updated();
}

void State::parse_var(Var var)
{
    // Fork addition: spin_count is a space-delimited list of rotation counts (each > 0)
    if (var == Var::SPIN_COUNT) {
        g_state.spin_counts.clear();
        g_state.vars[std::to_underlying(var)] = std::nullopt;

        const wchar_t *p = g_state.vars_text[std::to_underlying(var)];
        do {
            double d;
            p = parse_double(p, d);

            if (std::isnan(d) || !std::isfinite(d) || d <= 0.0) {
                g_state.spin_counts.clear();
                return;
            }

            g_state.spin_counts.push_back(d);

            if (*p == L'\0') {
                break;
            }

            if (!std::isspace(*(p - 1))) {
                g_state.spin_counts.clear();
                return;
            }
        } while (true);

        g_state.vars[std::to_underlying(var)] = g_state.spin_counts[0];
        return;
    }

    double d;
    auto p = parse_double(g_state.vars_text[std::to_underlying(var)], d);

    if (std::isnan(d) || d < 0.0 || *p != L'\0') {
        g_state.vars[std::to_underlying(var)] = std::nullopt;
        return;
    }

    switch (var) {
    case Var::RATE:
        if (d == 0.0) {
            g_state.vars[std::to_underlying(var)] = std::nullopt;
            return;
        }
        break;
    case Var::SLEEP:
        if (d >= 1000000.0) {
            g_state.vars[std::to_underlying(var)] = std::nullopt;
            return;
        }
        break;
    case Var::SPIN_DURATION:
    case Var::SLOWSPIN_DURATION:
        if (!std::isfinite(d) || d > 3600.0) {
            g_state.vars[std::to_underlying(var)] = std::nullopt;
            return;
        }
        break;
    case Var::SLOWSPIN_COUNT:
        if (!std::isfinite(d) || d == 0.0) {
            g_state.vars[std::to_underlying(var)] = std::nullopt;
            return;
        }
        break;
    }

    g_state.vars[std::to_underlying(var)] = d;
}

void State::parse_cycle_vars()
{
    g_state.count = 0;
    for (int i = 0; i < std::to_underlying(CycleVar::COUNT); ++i) {
        g_state.cycle_vars[i].clear();
        const wchar_t *p = g_state.cycle_vars_text[i];
        do {
            double d;
            p = parse_double(p, d);

            if (std::isnan(d) || d < 0.0) {
                g_state.count = 0;
                return;
            }

            g_state.cycle_vars[i].push_back(d);

            if (*p == L'\0') {
                if (g_state.count <= 1) {
                    g_state.count = g_state.cycle_vars[i].size();
                } else if (g_state.cycle_vars[i].size() != 1 && g_state.count != g_state.cycle_vars[i].size()) {
                    g_state.count = 0;
                    return;
                }
                break;
            }

            if (!std::isspace(*(p - 1))) {
                g_state.count = 0;
                return;
            }
        } while (true);
    }
}

int main(int argc, char *argv[])
{
    App::run();
    return 0;
}
