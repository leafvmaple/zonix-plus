#include "sched.h"
#include "cons/shell.h"
#include "lib/stdio.h"
#include <asm/arch.h>

extern long user_stack[];

static int init_main(void* arg) {
    auto shell_pid_r = TaskManager::kernel_thread(shell::main, nullptr);
    if (!shell_pid_r.ok()) {
        PANIC("init: failed to create shell process!");
    }
    int shell_pid = shell_pid_r.value();

    Task* shell_proc = TaskManager::find_process(shell_pid);
    if (shell_proc) {
        shell_proc->set_name("shell");
    }

    cprintf("init: started shell process (PID %d)\n", shell_pid);

    while (true) {
        int exit_code{};
        auto pid_r = TaskManager::wait(0, &exit_code);
        if (pid_r.ok() && pid_r.value() > 0) {
            cprintf("init: reaped child PID %d (exit code %d)\n", pid_r.value(), exit_code);
        }
    }

    PANIC("init process exited!");
    return 0;
}

int TaskManager::init() {
    if (init_idle() != 0) {
        cprintf("sched: failed to create idle process\n");
        return -1;
    }

    if (init_init_proc() != 0) {
        cprintf("sched: failed to create init process\n");
        return -1;
    }

    cprintf("sched: policy = %s\n", Scheduler::policy_name());
    cprintf("sched init: idle process PID = 0, init process PID = 1\n");
    return 0;
}

int TaskManager::init_idle() {
    auto idle_proc = sys::unique_ptr<Task>(new (sys::nothrow) Task());
    if (!idle_proc) {
        cprintf("sched: init_idle: failed to allocate Task\n");
        return -1;
    }
    idle_proc->boot_stack_ = reinterpret_cast<uintptr_t>(user_stack);
    idle_proc->files().init();
    idle_proc->priority = sched_prio::IDLE_PRIO;
    idle_proc->time_slice = 0;

    // Kernel threads share the memory manager's kernel address space.
    idle_proc->use_kernel_memory();

    // Establish the active root before switches compare task address spaces.
    arch_load_page_table_root(idle_proc->page_table_root_pa());

    idle_proc->set_name("idle");

    idle_proc->wakeup();
    idle_proc_ = idle_proc.release();
    Scheduler::set_current(idle_proc_);
    add_process(idle_proc_);

    return 0;
}

int TaskManager::init_init_proc() {
    TrapFrame trap_frame{};

    arch_setup_kthread_tf(&trap_frame, reinterpret_cast<uintptr_t>(init_main), 0, 0);

    auto ret = fork(0, 0, &trap_frame);
    if (!ret.ok()) {
        cprintf("sched: init_init_proc: fork failed (%s)\n", error_str(ret.error()));
        return -1;
    }

    init_proc_ = find_process(ret.value());
    if (!init_proc_) {
        cprintf("sched: init_init_proc: find_process(%d) returned null\n", ret.value());
        return -1;
    }

    init_proc_->set_name("init");

    return 0;
}
