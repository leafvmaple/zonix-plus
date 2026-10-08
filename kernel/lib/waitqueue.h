#pragma once

#include "lib/list.h"
#include "lib/spinlock.h"

struct Task;
enum class TaskState : uint8_t;

class WaitQueue {
public:
    void sleep();
    void wakeup_one();
    void wakeup_all();

    [[nodiscard]] bool empty() const { return head_.empty(); }

private:
    ListNode head_{};
    Spinlock lock_{};
};
