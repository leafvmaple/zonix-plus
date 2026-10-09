#pragma once

enum class Error : int {
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

template<typename T>
class [[nodiscard]] Result {
    T val_{};
    Error err_{Error::None};
    bool ok_{false};

public:
    Result(const T& val) : val_(val), err_(Error::None), ok_(true) {}
    Result(Error e) : val_{}, err_(e), ok_(false) {}

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] Error error() const { return err_; }

    T& value() { return val_; }
    const T& value() const { return val_; }
    T value_or(const T& fallback) const { return ok_ ? val_ : fallback; }

    T release_value() { return static_cast<T&&>(val_); }
    Error release_error() { return err_; }
};

template<>
class [[nodiscard]] Result<void> {
    Error err_{Error::None};

public:
    Result() : err_(Error::None) {}
    Result(Error e) : err_(e) {}

    [[nodiscard]] bool ok() const { return err_ == Error::None; }
    [[nodiscard]] Error error() const { return err_; }

    Error release_error() { return err_; }
};

struct ErrorResult {
    Error err;

    ErrorResult(Error e) : err(e) {}

    [[nodiscard]] bool ok() const { return err == Error::None; }
    Error release_error() { return err; }

    struct Void {};
    Void release_value() { return {}; }
};

namespace detail {

inline ErrorResult wrap_tryable(Error e) {
    return e;
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
