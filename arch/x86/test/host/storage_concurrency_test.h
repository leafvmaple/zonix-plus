#pragma once

#include "block/blk.h"
#include "lib/mutex.h"
#include "lib/string.h"
#include <sys/array.hpp>

// Hosted Linux entry points only; the kernel has no pthread dependency.
extern "C" int pthread_create(unsigned long*, const void*, void* (*)(void*), void*);
extern "C" int pthread_join(unsigned long, void**);
extern "C" int sched_yield();

class StorageConcurrency {
public:
    struct Failure {
        Error error;
        size_t after_blocks;
        bool quarantine;
    };
    struct Request {
        BlockDevice* device{};
        int role{};
        int resource{};
        uint32_t lba{};
        size_t blocks{};
        bool write{};
        size_t completed{};
        int acquisitions{};
        int releases{};
        Mutex* mutex{};
        bool held{};
        Error expected{Error::None};
        Error injected{Error::None};
        size_t commands{};
        size_t hardware_accesses{};
        sys::array<uint8_t, 9 * 512> buffer{};
    };

    static bool active() { return current_ != nullptr; }
    static uint8_t pattern(uint32_t lba) { return static_cast<uint8_t>(lba); }
    static Error command_error() { return active() ? current_->injected : Error::None; }
    static bool quarantine() { return command_error() != Error::None && failure_.quarantine; }
    static void hardware_access() {
        if (active()) {
            assert(current_->held);
            ++current_->hardware_accesses;
        }
    }

    static void before_lock(Mutex* mutex) {
        if (!active()) {
            return;
        }
        assert(!current_->held && current_->acquisitions == 0);
        current_->mutex = mutex;
        if (current_->role == 2) {
            second_mutex_ = mutex;
            __atomic_store_n(&second_attempt_, true, __ATOMIC_RELEASE);
        }
    }
    static void acquired() {
        if (active()) {
            current_->held = true;
            ++current_->acquisitions;
        }
    }
    static void releasing() {
        if (active()) {
            // Catch per-sector locks and unlocking before the final data copy.
            assert(current_->held);
            if (current_->expected == Error::None) {
                assert(current_->completed == current_->blocks && current_->injected == Error::None);
            } else if (current_->expected == Error::NoDevice) {
                assert(current_->completed == 0 && current_->commands == 0 && current_->hardware_accesses == 0);
            } else {
                assert(current_->completed == failure_.after_blocks && current_->injected == current_->expected);
                assert(current_->commands != 0 && current_->hardware_accesses != 0);
            }
            if (!current_->write) {
                verify_buffer(*current_);
            }
            current_->held = false;
            ++current_->releases;
        }
    }

    static void command(int resource, uint32_t lba, size_t blocks) {
        if (!active()) {
            return;
        }
        assert(current_->held && current_->resource == resource);
        assert(lba == current_->lba + current_->completed);
        assert(blocks != 0 && blocks <= current_->blocks - current_->completed);
        if (current_->role == 1 && current_->completed == 0) {
            __atomic_store_n(&paused_, true, __ATOMIC_RELEASE);
            while (!__atomic_load_n(&second_attempt_, __ATOMIC_ACQUIRE)) {
                sched_yield();
            }
            assert((current_->mutex != second_mutex_) == independent_);
            if (independent_) {
                // A different port/controller/channel must finish while this
                // command is paused, rather than waiting on a global lock.
                while (!__atomic_load_n(&second_finished_, __ATOMIC_ACQUIRE)) {
                    sched_yield();
                }
            }
        }
        ++current_->commands;
        if (current_->role == 1 && failure_.error != Error::None && current_->completed >= failure_.after_blocks) {
            current_->injected = failure_.error;
            return;
        }
        current_->completed += blocks;
    }

    static void run(BlockDevice* first, BlockDevice* second, int first_resource, int second_resource, size_t blocks,
                    bool first_write, bool second_write, bool independent,
                    const Failure& failure = {Error::None, 0, false}) {
        Request requests[2]{};
        requests[0].device = first;
        requests[1].device = second;
        requests[0].expected = failure.error;
        requests[1].expected = failure.error != Error::None && first == second ? Error::NoDevice : Error::None;
        for (int i = 0; i < 2; ++i) {
            requests[i].role = i + 1;
            requests[i].resource = i == 0 ? first_resource : second_resource;
            requests[i].lba = i == 0 ? 10 : 40;
            requests[i].blocks = blocks;
            requests[i].write = i == 0 ? first_write : second_write;
            if (requests[i].write) {
                for (size_t sector = 0; sector < blocks; ++sector) {
                    memset(requests[i].buffer.data() + sector * 512, pattern(requests[i].lba + sector), 512);
                }
            }
        }
        independent_ = independent;
        failure_ = failure;
        paused_ = second_attempt_ = second_finished_ = false;
        second_mutex_ = nullptr;
        unsigned long threads[2]{};
        assert(pthread_create(&threads[0], nullptr, worker, &requests[0]) == 0);
        while (!__atomic_load_n(&paused_, __ATOMIC_ACQUIRE)) {
            sched_yield();
        }
        assert(pthread_create(&threads[1], nullptr, worker, &requests[1]) == 0);
        assert(pthread_join(threads[0], nullptr) == 0);
        assert(pthread_join(threads[1], nullptr) == 0);
        for (const auto& request : requests) {
            assert(request.acquisitions == 1 && request.releases == 1 && !request.held);
        }
    }

private:
    static void verify_buffer(const Request& request) {
        for (size_t sector = 0; sector < request.blocks; ++sector) {
            uint8_t expected = sector < request.completed ? pattern(request.lba + sector) : 0;
            for (size_t byte = 0; byte < 512; ++byte) {
                assert(request.buffer[sector * 512 + byte] == expected);
            }
        }
    }
    static void* worker(void* arg) {
        current_ = static_cast<Request*>(arg);
        Error result = current_->write
                           ? current_->device->write(current_->lba, current_->buffer.data(), current_->blocks)
                           : current_->device->read(current_->lba, current_->buffer.data(), current_->blocks);
        assert(result == current_->expected);
        if (!current_->write) {
            verify_buffer(*current_);
        }
        if (current_->role == 2) {
            __atomic_store_n(&second_finished_, true, __ATOMIC_RELEASE);
        }
        current_ = nullptr;
        return nullptr;
    }

    inline static thread_local Request* current_{};
    inline static Mutex* second_mutex_{};
    inline static bool independent_{};
    inline static bool paused_{};
    inline static bool second_attempt_{};
    inline static bool second_finished_{};
    inline static Failure failure_{};
};
