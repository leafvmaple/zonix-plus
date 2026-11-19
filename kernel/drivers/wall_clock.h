#pragma once

#include <base/types.h>
#include "lib/result.h"

// The time of day. The platform's battery-backed clock is read once; after
// that the time follows the kernel's own uptime.
class WallClock {
public:
    // Seconds since 1970-01-01 00:00:00 UTC, or NoDevice on a platform
    // without a clock.
    static Result<uint64_t> now();

    // Seconds since the epoch for a date and time in the proleptic Gregorian
    // calendar, UTC. `month` and `day` start at 1.
    static constexpr uint64_t from_civil(int year, int month, int day, int hour, int minute, int second) {
        // Count years from March so the leap day is the last day of a year.
        const int shifted_year = month <= 2 ? year - 1 : year;
        const int era = shifted_year / 400;
        const int year_of_era = shifted_year - era * 400;
        const int day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
        const int day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
        const int64_t days = static_cast<int64_t>(era) * DAYS_PER_ERA + day_of_era - DAYS_TO_1970;
        return static_cast<uint64_t>(days * 86400 + hour * 3600 + minute * 60 + second);
    }

private:
    static constexpr int DAYS_PER_ERA = 146097;
    // Days from 0000-03-01 to 1970-01-01.
    static constexpr int DAYS_TO_1970 = 719468;

    enum class State : uint8_t { Unread, Valid, Absent };
    inline static State state_ = State::Unread;
    // The time of day when the kernel's uptime was zero.
    inline static uint64_t boot_seconds_{};
};
