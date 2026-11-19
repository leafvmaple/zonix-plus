#pragma once

#include "lib/result.h"
#include "lib/waitqueue.h"
#include "sched/sched.h"

// A terminal's foreground job: the tasks a shell started for one command line.
// The console and each pseudo-terminal own one. Callers hold the interrupt
// guard and decide who may begin a job; this class owns its state.
class JobControl {
public:
    int owner() const { return owner_pid_; }
    bool active() const { return owner_pid_ != 0; }
    bool cancelled() const { return cancelled_; }
    // Whether the task was started as part of the current job.
    bool includes(const Task* task) const { return active() && task->console_job() == owner_pid_; }

    void begin(int owner_pid) {
        owner_pid_ = owner_pid;
        cancelled_ = false;
        interrupted_ = false;
    }

    // Stops the job's tasks at its owner's request.
    void cancel() {
        cancelled_ = true;
        TaskManager::interrupt_job(owner_pid_);
    }

    // Stops the job's tasks for Ctrl-C, which end() then reports to the owner.
    void interrupt() {
        interrupted_ = true;
        cancel();
    }

    // Waits until every task of the job has exited. `stop()` is checked before
    // each wait and ends it early with the job still active.
    template<typename Stop>
    Error end(Stop stop) {
        while (TaskManager::has_live_job(owner_pid_)) {
            ENSURE(!stop(), Error::Interrupted);
            done_.sleep();
        }
        bool interrupted = interrupted_;
        reset();
        return interrupted ? Error::Interrupted : Error::None;
    }

    // Called for each task of the terminal as it exits.
    void task_exited(const Task* task) {
        if (task->pid == owner_pid_) {
            TaskManager::interrupt_job(owner_pid_);
            reset();
        }
        if (task->console_job() != 0)
            done_.wakeup_all();
    }

    // Releases an owner waiting in end() so it can observe a closed terminal.
    void wake() { done_.wakeup_all(); }

private:
    void reset() {
        owner_pid_ = 0;
        cancelled_ = false;
        interrupted_ = false;
    }

    int owner_pid_{};
    bool cancelled_{};
    bool interrupted_{};
    WaitQueue done_;
};
