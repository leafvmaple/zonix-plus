#include "backtrace.h"
#include "sched/sched.h"
#include <asm/frame.h>
#include <kernel/boot_stack.h>
#include <kernel/log.h>

namespace debug {

size_t collect_backtrace(uintptr_t fp, uintptr_t* addresses, size_t capacity) {
    Task* task = sched::current();
    uintptr_t low = task ? task->kernel_stack() : reinterpret_cast<uintptr_t>(__boot_stack_bottom);
    uintptr_t high = task ? task->kernel_stack_top() : reinterpret_cast<uintptr_t>(__boot_stack_top);
    size_t count = 0;
    while (count < capacity && fp >= FRAME_RECORD_OFFSET) {
        uintptr_t record = fp - FRAME_RECORD_OFFSET;
        if (record < low || record > high - sizeof(FrameRecord) || record % alignof(FrameRecord) != 0)
            break;
        const auto* frame = reinterpret_cast<const FrameRecord*>(record);
        if (frame->return_address == 0)
            break;
        addresses[count++] = frame->return_address;
        if (frame->caller_frame <= fp)
            break;
        fp = frame->caller_frame;
    }
    return count;
}

void log_backtrace(const char* heading, uintptr_t pc, uintptr_t fp) {
    uintptr_t addresses[BACKTRACE_DEPTH];
    size_t count = collect_backtrace(fp, addresses, BACKTRACE_DEPTH);
    klog::error("%s 0x%lx\n", heading, pc);
    for (size_t i = 0; i < count; ++i)
        klog::error("  #%lu 0x%lx\n", i, addresses[i]);
}

}  // namespace debug
