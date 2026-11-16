#pragma once

#include "lib/result.h"
#include "lib/waitqueue.h"
#include <sys/intrusive_list.hpp>

struct Task;
class ActivityGate;

// Address-stable lease for a logical I/O operation; nested requests share admission.
class ActivityGuard {
public:
    ActivityGuard() = default;
    ActivityGuard(const ActivityGuard&) = delete;
    ActivityGuard& operator=(const ActivityGuard&) = delete;
    ~ActivityGuard();
    Error acquire(ActivityGate& gate);

private:
    friend class ActivityGate;
    ActivityGate* gate_{};
    Task* task_{};
    sys::list_node<ActivityGuard> node_{*this};
};

class ActivityGate {
public:
    // Reject new callers, let admitted callers finish nested work, then drain.
    Error close();
    void open();
    bool is_closed() const;
    bool is_owner() const;

private:
    friend class ActivityGuard;
    Error acquire(ActivityGuard& guard);
    void release(ActivityGuard& guard);
    sys::intrusive_list<ActivityGuard, &ActivityGuard::node_> activities_{};
    mutable Spinlock lock_{};
    WaitQueue drained_{};
    Task* owner_{};
    bool closed_{};
};
