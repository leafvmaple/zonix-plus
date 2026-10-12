#include "cons/cons.h"
#include "drivers/fbcons.h"
#include "drivers/intr.h"
#include "lib/waitqueue.h"
#include <sys/array.hpp>

namespace {

class Console {
public:
    static void push_input(char c) {
        intr::Guard guard;
        size_t next = (write_ + 1) % input_.size();
        if (next == read_) {
            return;
        }
        input_[write_] = c;
        write_ = next;
        waitq_.wakeup_one();
    }

    static char getc() {
        while (true) {
            // Keep checking the buffer and registering the waiter atomic.
            intr::Guard guard;
            if (read_ != write_) {
                char c = input_[read_];
                read_ = (read_ + 1) % input_.size();
                return c;
            }
            waitq_.sleep();
        }
    }

private:
    inline static sys::array<char, 128> input_{};
    inline static size_t read_{};
    inline static size_t write_{};
    inline static WaitQueue waitq_{};
};

}  // namespace

namespace cons {

int init() {
    return arch_init();
}
int late_init() {
    fbcons::late_init();
    return 0;
}
void push_input(char c) {
    Console::push_input(c);
}
char getc() {
    return Console::getc();
}
void putc(int c) {
    arch_putc(c);
    fbcons::putc(c);
}

}  // namespace cons
