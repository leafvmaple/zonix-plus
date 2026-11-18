#pragma once

#include "lib/result.h"
#include "lib/waitqueue.h"

struct Task;

namespace fd {

class Readiness {
public:
    static Result<unsigned int> wait(Task* task, uintptr_t mask);
    static void notify();

private:
    inline static WaitQueue changed_{};
};

}  // namespace fd
