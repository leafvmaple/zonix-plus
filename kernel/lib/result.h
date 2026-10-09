#pragma once

#include "debug/assert.h"
#include "lib/memory.h"

enum class [[nodiscard]] Error : int {
    None = 0,
    Io = -1,
    NoMem = -2,
    Invalid = -3,
    NotFound = -4,
    BadFs = -5,
    Exists = -6,
    NotEmpty = -7,
    Timeout = -8,
    Busy = -9,
    NoDevice = -10,
    NotSupported = -11,
    Full = -12,
    Fail = -13,
};

inline const char* error_str(Error e) {
    switch (e) {
        case Error::None: return "success";
        case Error::Io: return "I/O error";
        case Error::NoMem: return "out of memory";
        case Error::Invalid: return "invalid argument";
        case Error::NotFound: return "not found";
        case Error::BadFs: return "bad filesystem";
        case Error::Exists: return "already exists";
        case Error::NotEmpty: return "not empty";
        case Error::Timeout: return "timeout";
        case Error::Busy: return "busy";
        case Error::NoDevice: return "no device";
        case Error::NotSupported: return "not supported";
        case Error::Full: return "full";
        case Error::Fail: return "failed";
        default: return "unknown error";
    }
}

namespace result_detail {

enum class State : uint8_t { Value, Error, Consumed };

template<typename T>
concept TrivialValue = __is_trivially_copyable(T);

// A Result is copyable only if its value is copy-constructible.
template<typename T>
class Storage {
protected:
    union ValueStorage {
        char empty;
        T value;

        ValueStorage() : empty{} {}
        ValueStorage(const ValueStorage&) = default;
        ValueStorage& operator=(const ValueStorage&) = default;
        ValueStorage(ValueStorage&&) = default;
        ValueStorage& operator=(ValueStorage&&) = default;
        ~ValueStorage()
            requires(__is_trivially_destructible(T))
        = default;
        ~ValueStorage() {}
    } storage_;
    Error error_{Error::None};
    State state_{State::Consumed};

    Storage(const T& value)
        requires(__is_constructible(T, const T&))
        : state_(State::Value) {
        new (&storage_.value) T(value);
    }
    Storage(T&& value)
        requires(__is_constructible(T, T &&))
        : state_(State::Value) {
        new (&storage_.value) T(static_cast<T&&>(value));
    }
    Storage(Error error) : error_(error), state_(State::Error) { assert(error != Error::None); }

    Storage(const Storage& other)
        requires(TrivialValue<T> && __is_constructible(T, const T&))
    = default;
    Storage(const Storage& other)
        requires(!TrivialValue<T> && __is_constructible(T, const T&))
        : error_(other.error_), state_(other.state_) {
        if (state_ == State::Value) {
            new (&storage_.value) T(other.storage_.value);
        }
    }

    Storage& operator=(const Storage& other)
        requires(TrivialValue<T> && __is_trivially_assignable(T&, const T&))
    = default;
    Storage& operator=(const Storage& other)
        requires(__is_constructible(T, const T&) && !(TrivialValue<T> && __is_trivially_assignable(T&, const T&)))
    {
        if (this != &other) {
            reset();
            error_ = other.error_;
            state_ = other.state_;
            if (state_ == State::Value) {
                new (&storage_.value) T(other.storage_.value);
            }
        }
        return *this;
    }

    Storage(Storage&& other)
        requires(TrivialValue<T> && __is_constructible(T, T &&))
    = default;
    Storage(Storage&& other)
        requires(!TrivialValue<T> && __is_constructible(T, T &&))
        : error_(other.error_), state_(other.state_) {
        if (state_ == State::Value) {
            new (&storage_.value) T(static_cast<T&&>(other.storage_.value));
        }
        other.reset();
    }

    Storage& operator=(Storage&& other)
        requires(TrivialValue<T> && __is_trivially_assignable(T&, T &&))
    = default;
    Storage& operator=(Storage&& other)
        requires(__is_constructible(T, T &&) && !(TrivialValue<T> && __is_trivially_assignable(T&, T &&)))
    {
        if (this != &other) {
            reset();
            error_ = other.error_;
            state_ = other.state_;
            if (state_ == State::Value) {
                new (&storage_.value) T(static_cast<T&&>(other.storage_.value));
            }
            if constexpr (!TrivialValue<T>) {
                other.reset();
            }
        }
        return *this;
    }

    ~Storage()
        requires(__is_trivially_destructible(T))
    = default;
    ~Storage() { reset(); }

    void reset() {
        if constexpr (!__is_trivially_destructible(T)) {
            if (state_ == State::Value) {
                storage_.value.~T();
            }
        }
        error_ = Error::None;
        state_ = State::Consumed;
    }
};

}  // namespace result_detail

// Owns a successful value or a non-None error. Nontrivial moves and all releases consume the source.
// value() borrows from a live lvalue; release_value() transfers ownership.
template<typename T>
class [[nodiscard]] Result : private result_detail::Storage<T> {
    static_assert(!__is_reference(T) && !__is_const(T) && !__is_volatile(T),
                  "Result values must be unqualified object types; use pointers for borrowed values");
    static_assert(!__is_same(T, Error), "Use Error directly for status-only operations");
    using Base = result_detail::Storage<T>;

public:
    Result(const T& value)
        requires(__is_constructible(T, const T&))
        : Base(value) {}
    Result(T&& value)
        requires(__is_constructible(T, T &&))
        : Base(static_cast<T&&>(value)) {}
    Result(Error error) : Base(error) {}

    [[nodiscard]] bool ok() const { return this->state_ == result_detail::State::Value; }
    [[nodiscard]] bool is_consumed() const { return this->state_ == result_detail::State::Consumed; }
    [[nodiscard]] Error error() const {
        assert(!is_consumed());
        return this->error_;
    }

    T& value() & {
        assert(ok());
        return this->storage_.value;
    }
    const T& value() const& {
        assert(ok());
        return this->storage_.value;
    }
    T& value() && = delete;
    const T& value() const&& = delete;

    T value_or(const T& fallback) const&
        requires(__is_constructible(T, const T&))
    {
        assert(!is_consumed());
        return ok() ? this->storage_.value : fallback;
    }
    T value_or(T fallback) &&
        requires(__is_constructible(T, T&&)) {
            assert(!is_consumed());
            if (ok()) {
                return release_value();
            }
            this->reset();
            return static_cast<T&&>(fallback);
        }

        [[nodiscard]] T release_value()
            requires(__is_constructible(T, T &&))
    {
        assert(ok());
        T value(static_cast<T&&>(this->storage_.value));
        this->reset();
        return value;
    }
    [[nodiscard]] Error release_error() {
        assert(this->state_ == result_detail::State::Error);
        Error error = this->error_;
        this->reset();
        return error;
    }
};

// Status-only compatibility wrapper: default construction and Error::None mean success.
template<>
class [[nodiscard]] Result<void> {
    Error err_{Error::None};
    result_detail::State state_{result_detail::State::Value};

public:
    Result() = default;
    Result(Error error)
        : err_(error), state_(error == Error::None ? result_detail::State::Value : result_detail::State::Error) {}
    Result(const Result&) = default;
    Result& operator=(const Result&) = default;
    Result(Result&&) = default;
    Result& operator=(Result&&) = default;

    [[nodiscard]] bool ok() const { return state_ == result_detail::State::Value; }
    [[nodiscard]] bool is_consumed() const { return state_ == result_detail::State::Consumed; }
    [[nodiscard]] Error error() const {
        assert(!is_consumed());
        return err_;
    }
    void release_value() {
        assert(ok());
        reset();
    }
    [[nodiscard]] Error release_error() {
        assert(state_ == result_detail::State::Error);
        Error error = err_;
        reset();
        return error;
    }

private:
    void reset() {
        err_ = Error::None;
        state_ = result_detail::State::Consumed;
    }
};

namespace detail {

inline Result<void> wrap_tryable(Error error) {
    return error;
}

template<typename T>
Result<T> wrap_tryable(Result<T> r) {
    return r;
}

}  // namespace detail

#define TRY(expr)                                             \
    __extension__({                                           \
        auto zonix_try_result = ::detail::wrap_tryable(expr); \
        if (!zonix_try_result.ok()) [[unlikely]]              \
            return zonix_try_result.release_error();          \
        zonix_try_result.release_value();                     \
    })

#define TRY_LOG(expr, fmt, ...)                               \
    __extension__({                                           \
        auto zonix_try_result = ::detail::wrap_tryable(expr); \
        if (!zonix_try_result.ok()) [[unlikely]] {            \
            cprintf(fmt "\n" __VA_OPT__(, ) __VA_ARGS__);     \
            return zonix_try_result.release_error();          \
        }                                                     \
        zonix_try_result.release_value();                     \
    })

// ENSURE(cond) — return Error::Invalid if cond is false.
// ENSURE(cond, err) — return err if cond is false.
// ENSURE_LOG(cond, err, fmt, ...) — log + return err if cond is false.

#define ZONIX_ENSURE1(cond)        \
    do {                           \
        if (!(cond)) [[unlikely]]  \
            return Error::Invalid; \
    } while (0)

#define ZONIX_ENSURE2(cond, err)  \
    do {                          \
        if (!(cond)) [[unlikely]] \
            return (err);         \
    } while (0)

#define ZONIX_ENSURE_SELECT(_1, _2, NAME, ...) NAME
#define ENSURE(...)                            ZONIX_ENSURE_SELECT(__VA_ARGS__, ZONIX_ENSURE2, ZONIX_ENSURE1)(__VA_ARGS__)

#define ENSURE_LOG(cond, err, fmt, ...)                   \
    do {                                                  \
        if (!(cond)) [[unlikely]] {                       \
            cprintf(fmt "\n" __VA_OPT__(, ) __VA_ARGS__); \
            return (err);                                 \
        }                                                 \
    } while (0)
