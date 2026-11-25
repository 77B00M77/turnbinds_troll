#pragma once

#include <cmath>
#include "common.hpp"

// Fork addition: drives a fixed-angle "spin" (e.g. a perfect 360) over a set duration.
//
// The whole spin is converted to an integer number of mouse counts up front, then sent
// in rate-limited steps that follow a linear time curve and always finish on exactly that
// integer. The fraction of a count that can't be sent (360 / (sensitivity * m_yaw) is rarely
// a whole number) is carried into the next spin, so repeated spins never drift more than
// half a mouse count from the true angle.
struct SpinState
{
    bool spinning() const
    {
        return active;
    }

    // counts: exact (fractional, signed) number of mouse counts for the whole spin
    void start(double counts, double duration_seconds)
    {
        auto exact = counts + carry;
        total = std::llround(exact);
        carry = exact - static_cast<double>(total);

        start_time = common::win32::performance_counter();
        last_time = start_time;
        duration = duration_seconds * static_cast<double>(common::win32::performance_counter_frequency());
        sent = 0;
        active = true;
    }

    // Drops an unfinished spin (e.g. the program became inactive mid-spin)
    void cancel()
    {
        active = false;
    }

    long long update(double rate)
    {
        if (!active) {
            return 0;
        }

        auto time = common::win32::performance_counter();
        auto elapsed = static_cast<double>(time - start_time);
        bool done = (elapsed >= duration);

        if (!done && (time - last_time < common::win32::performance_counter_frequency() / rate)) {
            return 0;
        }

        auto target = done ? total : std::llround(static_cast<double>(total) * (elapsed / duration));
        auto amount = target - sent;
        sent = target;
        last_time = time;

        if (done) {
            active = false;
        }

        return amount;
    }

private:
    bool active = false;
    long long start_time = 0;
    long long last_time = 0;
    double duration = 0.0;
    long long total = 0;
    long long sent = 0;
    double carry = 0.0;
};
