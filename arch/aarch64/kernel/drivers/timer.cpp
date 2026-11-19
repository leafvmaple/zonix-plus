// ARM Generic Timer driver (virtual timer, CNTV).

#include "drivers/timer.h"
#include "drivers/gic.h"

namespace {

constexpr uint32_t VTIMER_INTID = 27;  // Virtual timer PPI

class GenericTimer {
public:
    static int start() {
        uint64_t freq{};
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
        if (freq == 0)
            return -1;

        interval_ = freq / 100;  // 100 Hz = 10ms per tick
        if (interval_ == 0)
            return -1;

        gic::enable(VTIMER_INTID);
        arm();
        return 0;
    }

    // Programs the next interrupt one interval from now.
    static void arm() {
        __asm__ volatile("msr cntv_tval_el0, %0" ::"r"(interval_));
        __asm__ volatile("msr cntv_ctl_el0, %0" ::"r"(1UL));
    }

private:
    inline static uint64_t interval_{};
};

}  // namespace

namespace timer {

int init() {
    return GenericTimer::start();
}

void set_next() {
    GenericTimer::arm();
}

}  // namespace timer
