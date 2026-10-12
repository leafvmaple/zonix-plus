#include "sched.h"
#include "drivers/intr.h"
#include "lib/string.h"
#include <asm/arch.h>
#include <asm/mmu.h>

void Task::run() {
    Task* current = Scheduler::current();
    if (this != current) {
        intr::Guard guard;

        Task* prev = current;
        Scheduler::set_current(this);
        mark_running();

        uintptr_t next_root_pa = page_table_root_pa();
        uintptr_t previous_root_pa = prev->page_table_root_pa();
        if (next_root_pa != previous_root_pa) {
            arch_load_page_table_root(next_root_pa);
        }

        arch_set_kernel_stack(kernel_stack() + KSTACK_SIZE);

        switch_to(&(prev->context_), &(context_));
    }
}

void Task::wakeup() {
    assert(state_ != TaskState::Zombie);
    if (state_ != TaskState::Runnable) {
        state_ = TaskState::Runnable;
    }
}

void Task::mark_running() {
    assert(state_ != TaskState::Zombie);
    if (state_ != TaskState::Running) {
        state_ = TaskState::Running;
    }
}

void Task::mark_zombie(int code) {
    state_ = TaskState::Zombie;
    exit_code = code;
}

void Task::set_name(const char* name) {
    if (!name) {
        return;
    }

    strncpy(name_, name, sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = '\0';
}

void Task::sleep() {
    assert(state_ != TaskState::Zombie);
    if (state_ != TaskState::Sleeping) {
        state_ = TaskState::Sleeping;
    }
}

uintptr_t Task::page_table_root_pa() const {
    assert(memory() != nullptr && memory()->pgdir != nullptr);
    return virt_to_phys(memory()->pgdir);
}

void Task::copy_thread(uintptr_t stack_pointer, TrapFrame* src_tf) {
    trap_frame = reinterpret_cast<TrapFrame*>(kernel_stack() + KSTACK_SIZE) - 1;

    *trap_frame = *src_tf;
    arch_fixup_fork_tf(trap_frame, stack_pointer);

    context_.set_entry(reinterpret_cast<uintptr_t>(forkret));
    context_.set_stack(reinterpret_cast<uintptr_t>(trap_frame));
}

Error Task::setup_kernel_stack() {
    ENSURE(kernel_stack_.empty() && boot_stack_ == 0, Error::Busy);
    kernel_stack_ = TRY(KernelBuffer::alloc(KSTACK_SIZE));
    return Error::None;
}

void Task::set_links() {
    TaskManager::add_process(this);
    if (parent) {
        parent->child_list.add(child_node);
    }
}

void Task::remove_links() {
    TaskManager::remove_process(this);
    child_node.unlink();
}

void Task::destroy() {
    assert(state_ == TaskState::Zombie);
    delete this;
}
