// The PC's battery-backed clock, read through the CMOS index and data ports.

#include "drivers/wall_clock.h"

#include <asm/arch.h>
#include <asm/io.h>

namespace {

constexpr uint16_t INDEX_PORT = 0x70;
constexpr uint16_t DATA_PORT = 0x71;

constexpr uint8_t REG_SECONDS = 0x00;
constexpr uint8_t REG_MINUTES = 0x02;
constexpr uint8_t REG_HOURS = 0x04;
constexpr uint8_t REG_DAY = 0x07;
constexpr uint8_t REG_MONTH = 0x08;
constexpr uint8_t REG_YEAR = 0x09;
constexpr uint8_t REG_STATUS_A = 0x0A;
constexpr uint8_t REG_STATUS_B = 0x0B;

constexpr uint8_t STATUS_A_UPDATING = 0x80;
constexpr uint8_t STATUS_B_24_HOUR = 0x02;
constexpr uint8_t STATUS_B_BINARY = 0x04;
constexpr uint8_t HOURS_PM = 0x80;

// The registers change together once a second, so a reading is accepted only
// when it is seen twice in a row.
constexpr int ATTEMPTS = 8;
constexpr int UPDATE_SPINS = 100000;

struct Reading {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;

    bool operator==(const Reading&) const = default;
};

uint8_t read_register(uint8_t index) {
    outb(INDEX_PORT, index);
    return inb(DATA_PORT);
}

Reading read_registers() {
    for (int spin = 0; spin < UPDATE_SPINS && (read_register(REG_STATUS_A) & STATUS_A_UPDATING) != 0; ++spin) {}
    return {read_register(REG_SECONDS), read_register(REG_MINUTES), read_register(REG_HOURS),
            read_register(REG_DAY),     read_register(REG_MONTH),   read_register(REG_YEAR)};
}

constexpr int from_bcd(uint8_t value) {
    return (value >> 4) * 10 + (value & 0x0F);
}

}  // namespace

bool arch_read_wall_clock(uint64_t* seconds) {
    Reading reading = read_registers();
    bool stable = false;
    for (int attempt = 0; attempt < ATTEMPTS && !stable; ++attempt) {
        Reading again = read_registers();
        stable = again == reading;
        reading = again;
    }
    if (!stable)
        return false;

    const uint8_t status = read_register(REG_STATUS_B);
    const bool binary = (status & STATUS_B_BINARY) != 0;
    const bool pm = (reading.hour & HOURS_PM) != 0;
    auto decode = [binary](uint8_t value) {
        return binary ? static_cast<int>(value) : from_bcd(value);
    };
    int hour = decode(reading.hour & static_cast<uint8_t>(~HOURS_PM));
    if ((status & STATUS_B_24_HOUR) == 0)
        hour = hour % 12 + (pm ? 12 : 0);
    const int second = decode(reading.second);
    const int minute = decode(reading.minute);
    const int day = decode(reading.day);
    const int month = decode(reading.month);
    // The register holds two digits. This kernel was not written before 2000.
    const int year = 2000 + decode(reading.year);
    if (second > 59 || minute > 59 || hour > 23 || day < 1 || day > 31 || month < 1 || month > 12)
        return false;
    *seconds = WallClock::from_civil(year, month, day, hour, minute, second);
    return true;
}
