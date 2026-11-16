#pragma once

#include <debug/assert.h>
#include <sys/intrusive_list.hpp>
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
    struct Entry {
        Task* task{};
        sys::list_hook<Entry> node{*this};
    };
    sys::intrusive_list<Entry, &Entry::node> head_{};
    Spinlock lock_{};
};
