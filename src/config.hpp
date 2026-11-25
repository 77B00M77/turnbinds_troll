#pragma once

#include <optional>
#include "common.hpp"

struct Config
{
    bool developer;
    int bind_left;
    int bind_right;
    int bind_speed;
    int bind_cycle;
    int bind_spin;
    int bind_slowspin;
    wchar_t rate[128];
    wchar_t sleep[128];
    wchar_t cl_yawspeed[128];
    wchar_t sensitivity[128];
    wchar_t cl_anglespeedkey[128];
    wchar_t m_yaw[128];
    wchar_t spin_duration[128];
    wchar_t spin_count[128];
    wchar_t slowspin_duration[128];
    wchar_t slowspin_count[128];
    bool spin_right;
    int spin_current;
    bool enabled;
    int current;
    int selected;
    std::optional<WINDOWPLACEMENT> placement;

    Config(const wchar_t *path, const wchar_t *section)
    {
        developer = (GetPrivateProfileIntW(section, L"developer", 0, path) == 1);
        bind_left = GetPrivateProfileIntW(section, L"bind_left", VK_LBUTTON, path);
        bind_right = GetPrivateProfileIntW(section, L"bind_right", VK_RBUTTON, path);
        bind_speed = GetPrivateProfileIntW(section, L"bind_speed", VK_LSHIFT, path);
        bind_cycle = GetPrivateProfileIntW(section, L"bind_cycle", VK_XBUTTON1, path);
        bind_spin = GetPrivateProfileIntW(section, L"bind_spin", 0, path);
        bind_slowspin = GetPrivateProfileIntW(section, L"bind_slowspin", 0, path);
        GetPrivateProfileStringW(section, L"rate", L"1000", rate, std::size(rate), path);
        GetPrivateProfileStringW(section, L"sleep", L"3500", sleep, std::size(sleep), path);
        GetPrivateProfileStringW(section, L"cl_yawspeed", L"75 120 210", cl_yawspeed, std::size(cl_yawspeed), path);
        GetPrivateProfileStringW(section, L"sensitivity", L"1.0", sensitivity, std::size(sensitivity), path);
        GetPrivateProfileStringW(section, L"cl_anglespeedkey", L"0.67", cl_anglespeedkey, std::size(cl_anglespeedkey), path);
        GetPrivateProfileStringW(section, L"m_yaw", L"0.022", m_yaw, std::size(m_yaw), path);
        GetPrivateProfileStringW(section, L"spin_duration", L"0.4", spin_duration, std::size(spin_duration), path);
        GetPrivateProfileStringW(section, L"spin_count", L"1 2", spin_count, std::size(spin_count), path);
        GetPrivateProfileStringW(section, L"slowspin_duration", L"2.5", slowspin_duration, std::size(slowspin_duration), path);
        GetPrivateProfileStringW(section, L"slowspin_count", L"1", slowspin_count, std::size(slowspin_count), path);
        {
            wchar_t direction[16];
            GetPrivateProfileStringW(section, L"spin_direction", L"right", direction, std::size(direction), path);
            spin_right = (_wcsicmp(direction, L"left") != 0);
        }
        spin_current = GetPrivateProfileIntW(section, L"spin_current", 0, path);
        enabled = (GetPrivateProfileIntW(section, L"enabled", 1, path) == 1);
        current = GetPrivateProfileIntW(section, L"current", 0, path);
        selected = GetPrivateProfileIntW(section, L"selected", 0, path);

        WINDOWPLACEMENT placement_;
        if (GetPrivateProfileStructW(section, L"placement", &placement_, sizeof(placement_), path)) {
            placement = placement_;
        }
    }

    void save(const wchar_t *path, const wchar_t *section)
    {
        using common::win32::WritePrivateProfileIntW;
        WritePrivateProfileIntW(section, L"developer", developer, path);
        WritePrivateProfileIntW(section, L"bind_left", bind_left, path);
        WritePrivateProfileIntW(section, L"bind_right", bind_right, path);
        WritePrivateProfileIntW(section, L"bind_speed", bind_speed, path);
        WritePrivateProfileIntW(section, L"bind_cycle", bind_cycle, path);
        WritePrivateProfileIntW(section, L"bind_spin", bind_spin, path);
        WritePrivateProfileIntW(section, L"bind_slowspin", bind_slowspin, path);
        WritePrivateProfileStringW(section, L"rate", rate, path);
        WritePrivateProfileStringW(section, L"sleep", sleep, path);
        WritePrivateProfileStringW(section, L"cl_yawspeed", cl_yawspeed, path);
        WritePrivateProfileStringW(section, L"sensitivity", sensitivity, path);
        WritePrivateProfileStringW(section, L"cl_anglespeedkey", cl_anglespeedkey, path);
        WritePrivateProfileStringW(section, L"m_yaw", m_yaw, path);
        WritePrivateProfileStringW(section, L"spin_duration", spin_duration, path);
        WritePrivateProfileStringW(section, L"spin_count", spin_count, path);
        WritePrivateProfileStringW(section, L"slowspin_duration", slowspin_duration, path);
        WritePrivateProfileStringW(section, L"slowspin_count", slowspin_count, path);
        WritePrivateProfileStringW(section, L"spin_direction", spin_right ? L"right" : L"left", path);
        WritePrivateProfileIntW(section, L"spin_current", spin_current, path);
        WritePrivateProfileIntW(section, L"enabled", enabled, path);
        WritePrivateProfileIntW(section, L"current", current, path);
        WritePrivateProfileIntW(section, L"selected", selected, path);
        WritePrivateProfileStructW(section, L"placement", &(*placement), sizeof(*placement), path);
    }
};
