#include "lib/activity_gate.h"
#include "drivers/intr.h"
#include <sys/mutex.hpp>

namespace sched {
Task* current();
}

ActivityGuard::~ActivityGuard() {
    if (gate_) {
        gate_->release(*this);
    }
}

Error ActivityGuard::acquire(ActivityGate& gate) {
    ENSURE(!gate_, Error::Busy);
    return gate.acquire(*this);
}

Error ActivityGate::acquire(ActivityGuard& guard) {
    sys::lock_guard<Spinlock> lock(lock_);
    Task* current = sched::current();
    for (const auto& activity : activities_) {
        if (activity.task_ == current) {
            // The outer lease remains live until all synchronous nested work returns.
            return Error::None;
        }
    }
    ENSURE(!closed_, Error::Busy);
    guard.gate_ = this;
    guard.task_ = current;
    activities_.push_back(guard);
    return Error::None;
}

void ActivityGate::release(ActivityGuard& guard) {
    sys::lock_guard<Spinlock> lock(lock_);
    activities_.erase(activities_.iterator_to(guard));
    guard.gate_ = nullptr;
    guard.task_ = nullptr;
    if (closed_ && activities_.empty()) {
        drained_.wakeup_all();
    }
}

Error ActivityGate::close() {
    {
        sys::lock_guard<Spinlock> lock(lock_);
        ENSURE(!closed_, Error::Busy);
        Task* current = sched::current();
        for (const auto& activity : activities_) {
            ENSURE(activity.task_ != current, Error::Busy);
        }
        closed_ = true;
        owner_ = current;
    }
    while (true) {
        intr::Guard irq_guard;
        {
            sys::lock_guard<Spinlock> lock(lock_);
            if (activities_.empty()) {
                return Error::None;
            }
        }
        drained_.sleep();
    }
}

void ActivityGate::open() {
    sys::lock_guard<Spinlock> lock(lock_);
    assert(closed_ && owner_ == sched::current() && activities_.empty());
    closed_ = false;
    owner_ = nullptr;
}

bool ActivityGate::is_closed() const {
    sys::lock_guard<Spinlock> lock(lock_);
    return closed_;
}

bool ActivityGate::is_owner() const {
    sys::lock_guard<Spinlock> lock(lock_);
    return closed_ && owner_ == sched::current();
}
