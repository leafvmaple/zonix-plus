#pragma once

// Generic RAII lock guard for any lockable type.
// Requires T to have acquire()/release() or lock()/unlock() methods.

template<typename T>
class LockGuard {
public:
    explicit LockGuard(T& lockable) : ref_(lockable) { ref_.acquire(); }
    ~LockGuard() { ref_.release(); }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    T& ref_;
};
