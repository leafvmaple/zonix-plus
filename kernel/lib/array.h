#pragma once

#include <base/types.h>

template <typename T, size_t N>
class Array {
public:
    bool push_back(const T& val) {
        if (size_ >= N) return false;
        __builtin_memcpy(&data_[size_], &val, sizeof(T));
        size_++;
        return true;
    }

    void pop_back() {
        if (size_ > 0) size_--;
    }

    T& operator[](size_t i) { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }

    T* data() { return data_; }
    const T* data() const { return data_; }

    [[nodiscard]] size_t size() const { return size_; }
    static constexpr size_t capacity() { return N; }
    [[nodiscard]] bool full() const { return size_ >= N; }
    [[nodiscard]] bool empty() const { return size_ == 0; }

    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }

    void fill(const T& val) {
        for (size_t i = 0; i < N; i++) {
            data_[i] = val;
        }
    }

    void clear() { size_ = 0; }
    void commit_back() {
        if (size_ < N) size_++;
    }

private:
    T data_[N]{};
    size_t size_{};
};
